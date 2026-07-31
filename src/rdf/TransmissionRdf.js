// src/rdf/TransmissionRdf.js

import { Readable } from 'node:stream'
import rdf from 'rdf-ext'
import Parser from '@rdfjs/parser-n3'
import { Graph } from '../model/Graph.js'
import { Arrangement } from '../model/Arrangement.js'
import { vocabulary as ns } from './Vocabulary.js'

export async function parseTurtle(text) {
  const parser = new Parser({ format: 'text/turtle' })
  const dataset = rdf.dataset()
  const stream = parser.import(Readable.from([text]))
  for await (const quad of stream) dataset.add(quad)
  return dataset
}

export function graphFromDataset(dataset, transmissionId) {
  const subject = rdf.namedNode(transmissionId)
  const typeValues = terms(dataset, subject, ns.rdf.type)
  if (!typeValues.some(term => [ns.trn.transmission, ns.trn.entryTransmission].includes(term.value))) {
    throw new Error(`Not a transmission: ${transmissionId}`)
  }

  const nodes = list(dataset, first(dataset, subject, ns.trn.pipe))
    .map(node => ({
      id: node.value,
      type: first(dataset, node, ns.rdf.type)?.value,
      label: first(dataset, node, ns.rdfs.label)?.value ?? '',
      ports: {
        audioInputs: numeric(first(dataset, node, ns.trn.audioInputs)),
        audioOutputs: numeric(first(dataset, node, ns.trn.audioOutputs)),
        midiInputs: numeric(first(dataset, node, ns.trn.midiInputs)),
        midiOutputs: numeric(first(dataset, node, ns.trn.midiOutputs))
      },
      settings: settingsObject(dataset, first(dataset, node, ns.trn.settings)),
      parameters: list(dataset, first(dataset, node, ns.trn.parameters))
        .map(parameter => ({
          id: numeric(first(dataset, parameter, ns.trn.parameterId)),
          normalizedValue: numeric(first(dataset, parameter, ns.trn.normalizedValue))
        })),
      state: {
        component: first(dataset, node, ns.trn.componentState)?.value ?? '',
        controller: first(dataset, node, ns.trn.controllerState)?.value ?? ''
      },
      metadata: {
        x: numeric(first(dataset, node, ns.trn.editorX)),
        y: numeric(first(dataset, node, ns.trn.editorY))
      }
    }))

  const connectionHead = first(dataset, subject, ns.trn.connections)
  const connections = connectionHead
    ? list(dataset, connectionHead).map(connection => ({
        from: first(dataset, connection, ns.trn.from)?.value,
        to: first(dataset, connection, ns.trn.to)?.value,
        kind: first(dataset, connection, ns.trn.kind)?.value,
        fromPort: numeric(first(dataset, connection, ns.trn.fromPort)),
        toPort: numeric(first(dataset, connection, ns.trn.toPort))
      }))
    : nodes.slice(0, -1).map((node, index) => ({
        from: node.id,
        to: nodes[index + 1].id,
        kind: 'audio'
      }))
  return new Graph({
    id: transmissionId,
    label: first(dataset, subject, ns.rdfs.label)?.value ?? '',
    nodes,
    connections,
    metadata: {
      systemInputConnections: literalList(dataset, first(dataset, subject, ns.trn.systemInputConnections)),
      systemOutputConnections: literalList(dataset, first(dataset, subject, ns.trn.systemOutputConnections))
    }
  })
}

export function transportFromDataset(dataset, transmissionId) {
  const subject = rdf.namedNode(transmissionId)
  const state = { tempoMap: [] }
  const tempoHead = first(dataset, subject, ns.trn.tempoMap)
  for (const changeNode of list(dataset, tempoHead)) {
    const beat = numeric(first(dataset, changeNode, ns.trn.atBeat))
    const bpm = numeric(first(dataset, changeNode, ns.trn.bpm))
    if (bpm > 0) state.tempoMap.push({ beat, bpm })
  }
  const loopStart = first(dataset, subject, ns.trn.loopStart)
  const loopEnd = first(dataset, subject, ns.trn.loopEnd)
  if (loopStart && loopEnd) {
    state.loop = {
      startBeat: numeric(loopStart),
      endBeat: numeric(loopEnd),
      enabled: first(dataset, subject, ns.trn.loopEnabled)?.value !== 'false'
    }
  }
  return state
}

export function arrangementFromDataset(dataset, transmissionId, graph = null) {
  const subject = rdf.namedNode(transmissionId)
  const definition = {
    lengthBeats: numeric(first(dataset, subject, ns.trn.arrangementLength)),
    midiClips: list(dataset, first(dataset, subject, ns.trn.midiClips)).map(clip => ({
      id: first(dataset, clip, ns.trn.clipId)?.value ?? '',
      targetNodeId: first(dataset, clip, ns.trn.targetNode)?.value ?? '',
      startBeat: numeric(first(dataset, clip, ns.trn.startBeat)),
      lengthBeats: numeric(first(dataset, clip, ns.trn.lengthBeats)),
      notes: list(dataset, first(dataset, clip, ns.trn.notes)).map(note => ({
        startBeat: numeric(first(dataset, note, ns.trn.startBeat)),
        durationBeats: numeric(first(dataset, note, ns.trn.durationBeats)),
        pitch: numeric(first(dataset, note, ns.trn.pitch)),
        velocity: numeric(first(dataset, note, ns.trn.velocity)),
        channel: numeric(first(dataset, note, ns.trn.channel))
      }))
    })),
    gainLanes: list(dataset, first(dataset, subject, ns.trn.gainLanes)).map(lane => ({
      targetNodeId: first(dataset, lane, ns.trn.targetNode)?.value ?? '',
      points: list(dataset, first(dataset, lane, ns.trn.points)).map(point => ({
        beat: numeric(first(dataset, point, ns.trn.atBeat)),
        valueDb: numeric(first(dataset, point, ns.trn.valueDb)),
        shape: first(dataset, point, ns.trn.shape)?.value ?? 'linear'
      }))
    }))
  }
  return new Arrangement(definition, graph)
}

export function serializeGraph(graph, transport = null, arrangement = null) {
  const base = 'http://purl.org/stuff/transmissions/'
  const nodeNames = new Map([...graph.nodes.keys()].map(id => [id, compact(id, base)]))
  const subject = compact(graph.id, base)
  const lines = [`@prefix : <${base}> .`, '@prefix rdf: <http://www.w3.org/1999/02/22-rdf-syntax-ns#> .', '']
  lines.push(`${subject} a :Transmission ;`)
  if (graph.label) lines.push(`    <http://www.w3.org/2000/01/rdf-schema#label> ${literal(graph.label)} ;`)
  if (transport?.tempoMap?.length) {
    const changes = transport.tempoMap.map(change => `[ :atBeat ${change.beat} ; :bpm ${change.bpm} ]`).join(' ')
    lines.push(`    :tempoMap ( ${changes} ) ;`)
  }
  if (transport?.loop) {
    lines.push(`    :loopStart ${transport.loop.startBeat} ;`)
    lines.push(`    :loopEnd ${transport.loop.endBeat} ;`)
    lines.push(`    :loopEnabled ${transport.loop.enabled !== false} ;`)
  }
  const performance = arrangement?.toJSON?.() ?? arrangement
  if (performance?.lengthBeats > 0)
    lines.push(`    :arrangementLength ${performance.lengthBeats} ;`)
  if (performance?.midiClips?.length) {
    const clips = performance.midiClips.map(clip => {
      const notes = clip.notes.map(note =>
        `[ :startBeat ${note.startBeat} ; :durationBeats ${note.durationBeats} ; :pitch ${note.pitch} ; :velocity ${note.velocity} ; :channel ${note.channel} ]`).join(' ')
      return `[ :clipId ${literal(clip.id)} ; :targetNode ${nodeNames.get(clip.targetNodeId) ?? term(clip.targetNodeId, base)} ; :startBeat ${clip.startBeat} ; :lengthBeats ${clip.lengthBeats} ; :notes ( ${notes} ) ]`
    }).join(' ')
    lines.push(`    :midiClips ( ${clips} ) ;`)
  }
  if (performance?.gainLanes?.length) {
    const lanes = performance.gainLanes.map(lane => {
      const points = lane.points.map(point =>
        `[ :atBeat ${point.beat} ; :valueDb ${point.valueDb} ; :shape ${literal(point.shape)} ]`).join(' ')
      return `[ :targetNode ${nodeNames.get(lane.targetNodeId) ?? term(lane.targetNodeId, base)} ; :points ( ${points} ) ]`
    }).join(' ')
    lines.push(`    :gainLanes ( ${lanes} ) ;`)
  }
  const inputConnections = graph.metadata?.systemInputConnections ?? []
  const outputConnections = graph.metadata?.systemOutputConnections ?? []
  if (inputConnections.length)
    lines.push(`    :systemInputConnections ( ${inputConnections.map(literal).join(' ')} ) ;`)
  if (outputConnections.length)
    lines.push(`    :systemOutputConnections ( ${outputConnections.map(literal).join(' ')} ) ;`)
  lines.push(`    :pipe ( ${[...graph.nodes.keys()].map(id => nodeNames.get(id)).join(' ')} ) ;`)
  const connections = graph.connections.map(connection =>
    `[ :from ${nodeNames.get(connection.from) ?? term(connection.from, base)} ; ` +
    `:to ${nodeNames.get(connection.to) ?? term(connection.to, base)} ; ` +
    `:kind ${literal(connection.kind)} ; :fromPort ${connection.fromPort ?? 0} ; ` +
    `:toPort ${connection.toPort ?? 0} ]`)
  lines.push(`    :connections ( ${connections.join(' ')} ) .`, '')
  for (const node of graph.nodes.values()) {
    lines.push(`${nodeNames.get(node.id)} a ${term(node.type, base)} ;`)
    if (node.label) lines.push(`    <http://www.w3.org/2000/01/rdf-schema#label> ${literal(node.label)} ;`)
    const portValues = [
      [':audioInputs', node.ports.audioInputs], [':audioOutputs', node.ports.audioOutputs],
      [':midiInputs', node.ports.midiInputs], [':midiOutputs', node.ports.midiOutputs]
    ].filter(([, value]) => value > 0)
    for (const [predicate, value] of portValues) lines.push(`    ${predicate} ${value} ;`)
    if (Number.isFinite(node.metadata?.x)) lines.push(`    :editorX ${node.metadata.x} ;`)
    if (Number.isFinite(node.metadata?.y)) lines.push(`    :editorY ${node.metadata.y} ;`)
    const settings = Object.entries(node.settings ?? {})
    if (settings.length) {
      const properties = settings.map(([key, values]) => {
        const items = Array.isArray(values) ? values : [values]
        return `${term(key, base)} ${items.map(value => literal(value)).join(', ')}`
      })
      lines.push(`    :settings [ ${properties.join(' ; ')} ] ;`)
    }
    if (node.parameters?.length) {
      const parameters = node.parameters.map(parameter =>
        `[ :parameterId ${parameter.id} ; :normalizedValue ${parameter.normalizedValue} ]`)
      lines.push(`    :parameters ( ${parameters.join(' ')} ) ;`)
    }
    if (node.state?.component)
      lines.push(`    :componentState ${literal(node.state.component)} ;`)
    if (node.state?.controller)
      lines.push(`    :controllerState ${literal(node.state.controller)} ;`)
    lines[lines.length - 1] = lines[lines.length - 1].replace(/ ;$/, ' .')
    lines.push('')
  }
  return `${lines.join('\n').trim()}\n`
}

function literalList(dataset, head) {
  return head ? list(dataset, head).map(term => term.value) : []
}

function terms(dataset, subject, predicate) {
  return [...dataset.match(subject, rdf.namedNode(predicate))].map(quad => quad.object)
}

function first(dataset, subject, predicate) {
  return subject ? terms(dataset, subject, predicate)[0] : undefined
}

function list(dataset, head) {
  const result = []
  const seen = new Set()
  let current = head
  while (current && current.value !== ns.rdf.nil) {
    if (seen.has(current.value)) throw new Error(`Circular RDF list at ${current.value}`)
    seen.add(current.value)
    const item = first(dataset, current, ns.rdf.first)
    if (!item) throw new Error(`Malformed RDF list at ${current.value}`)
    result.push(item)
    current = first(dataset, current, ns.rdf.rest)
  }
  return result
}

function settingsObject(dataset, settingsNode) {
  if (!settingsNode) return {}
  const values = {}
  for (const quad of dataset.match(settingsNode)) {
    if (quad.predicate.value === ns.rdf.type) continue
    const current = values[quad.predicate.value] ?? []
    current.push(quad.object.value)
    values[quad.predicate.value] = current
  }
  return values
}

function numeric(termValue) {
  const value = Number(termValue?.value ?? 0)
  return Number.isFinite(value) ? value : 0
}

function compact(value, base) {
  return value.startsWith(base) ? `:${value.slice(base.length) || 'project'}` : `<${value}>`
}

function term(value, base) {
  if (!value.includes(':')) return `:${value}`
  return value.startsWith(base) ? `:${value.slice(base.length)}` : `<${value}>`
}

function literal(value) {
  if (typeof value === 'number') return String(value)
  return JSON.stringify(String(value))
}
