#!/usr/bin/env node

import { readFile } from 'node:fs/promises'
import { writeSync } from 'node:fs'
import { Graph } from '../src/model/Graph.js'
import { ProjectSession } from '../src/session/ProjectSession.js'

const BASE = 'http://purl.org/stuff/transmissions/'
const [command, filePath, interchangePath] = process.argv.slice(2)

if (!['load', 'save'].includes(command) || !filePath ||
    (command === 'save' && !interchangePath)) {
  process.stderr.write('Usage: native-ui-project.js <load|save> <project.ttl> [interchange-file]\n')
  process.exit(2)
}

try {
  if (command === 'save') {
    const interchange = await readFile(interchangePath, 'utf8')
    const project = decodeProject(interchange)
    const session = new ProjectSession()
    session.open({
      id: project.id,
      label: project.label,
      nodes: project.nodes,
      connections: project.connections,
      metadata: project.metadata,
      transport: project.transport
    })
    await session.save(filePath)
  } else {
    await readFile(filePath, 'utf8')
    const session = await ProjectSession.load(filePath)
    writeSync(1, encodeProject(session))
  }
} catch (error) {
  writeSync(2, `${error instanceof Error ? error.message : String(error)}\n`)
  process.exitCode = 1
}

function decodeProject(text) {
  const project = {
    id: `${BASE}main`,
    label: 'Transmission',
    nodes: [],
    connections: [],
    metadata: {
      systemInputConnections: ['system:capture_1', 'system:capture_2'],
      systemOutputConnections: ['system:playback_1', 'system:playback_2']
    },
    transport: {
      tempoMap: [{ beat: 0, bpm: 120 }],
      loop: { startBeat: 0, endBeat: 16, enabled: false }
    }
  }
  let header = false
  let ended = false
  for (const [index, line] of text.split(/\r?\n/).entries()) {
    if (!line) continue
    const fields = line.split('\t')
    const invalid = () => { throw new Error(`Invalid native UI project interchange at line ${index + 1}`) }
    if (!header) {
      if (fields.length !== 2 || fields[0] !== 'TRANSMISSION_UI' || fields[1] !== '1') invalid()
      header = true
      continue
    }
    if (fields[0] === 'END') {
      if (fields.length !== 1) invalid()
      ended = true
      break
    }
    if (fields[0] === 'PROJECT' && fields.length === 3) {
      project.id = fullId(hexDecode(fields[1]))
      project.label = hexDecode(fields[2])
    } else if (fields[0] === 'TRANSPORT' && fields.length === 4) {
      const tempo = finiteNumber(fields[1])
      const loopBars = finiteNumber(fields[2])
      if (tempo <= 0 || loopBars <= 0 || !['0', '1'].includes(fields[3])) invalid()
      project.transport = {
        tempoMap: [{ beat: 0, bpm: tempo }],
        loop: { startBeat: 0, endBeat: loopBars * 4, enabled: fields[3] === '1' }
      }
    } else if ((fields[0] === 'INPUT' || fields[0] === 'OUTPUT') && fields.length === 3) {
      const route = fields[0] === 'INPUT'
        ? project.metadata.systemInputConnections
        : project.metadata.systemOutputConnections
      const channel = integer(fields[1])
      if (channel < 0 || channel >= route.length) invalid()
      route[channel] = hexDecode(fields[2])
    } else if (fields[0] === 'NODE' && fields.length === 11) {
      const kind = integer(fields[3])
      const id = hexDecode(fields[1])
      if (kind < 0 || kind > 3 || !id) invalid()
      const pluginPath = hexDecode(fields[10])
      project.nodes.push({
        id: fullId(id),
        label: hexDecode(fields[2]),
        type: `${BASE}${['AudioInput', 'AudioOutput', 'PassThrough', 'VST3Plugin'][kind]}`,
        ports: {
          audioInputs: integer(fields[4]),
          audioOutputs: integer(fields[5]),
          midiInputs: integer(fields[6]),
          midiOutputs: integer(fields[7])
        },
        settings: pluginPath ? { pluginPath } : {},
        metadata: { x: finiteNumber(fields[8]), y: finiteNumber(fields[9]) }
      })
    } else if (fields[0] === 'EDGE' && fields.length === 6) {
      const kind = integer(fields[3])
      if (kind < 0 || kind > 1) invalid()
      project.connections.push({
        from: fullId(hexDecode(fields[1])),
        to: fullId(hexDecode(fields[2])),
        kind: ['audio', 'midi'][kind],
        fromPort: integer(fields[4]),
        toPort: integer(fields[5])
      })
    } else {
      invalid()
    }
  }
  if (!header || !ended || !project.nodes.length) throw new Error('Native UI project interchange is incomplete')
  return project
}

function encodeProject(session) {
  const graph = session.graph
  const transport = session.transport.toJSON()
  const lines = [
    'TRANSMISSION_UI\t1',
    `PROJECT\t${hexEncode(shortId(graph.id))}\t${hexEncode(graph.label)}`,
    `TRANSPORT\t${transport.tempoMap[0]?.bpm ?? 120}\t${(transport.loop?.endBeat ?? 16) / 4}\t${transport.loop?.enabled ? 1 : 0}`
  ]
  const inputs = graph.metadata.systemInputConnections ?? ['system:capture_1', 'system:capture_2']
  const outputs = graph.metadata.systemOutputConnections ?? ['system:playback_1', 'system:playback_2']
  for (let channel = 0; channel < 2; ++channel) {
    lines.push(`INPUT\t${channel}\t${hexEncode(inputs[channel] ?? 'No connection')}`)
    lines.push(`OUTPUT\t${channel}\t${hexEncode(outputs[channel] ?? 'No connection')}`)
  }
  for (const node of graph.nodes.values()) {
    const type = shortId(node.type)
    const kind = { AudioInput: 0, AudioOutput: 1, PassThrough: 2, VST3Plugin: 3 }[type]
    if (kind === undefined) throw new Error(`Unsupported native UI node type: ${node.type}`)
    const pluginPath = firstSetting(node.settings, 'pluginPath')
    lines.push([
      'NODE', hexEncode(shortId(node.id)), hexEncode(node.label), kind,
      node.ports.audioInputs, node.ports.audioOutputs,
      node.ports.midiInputs, node.ports.midiOutputs,
      finiteNumber(node.metadata.x ?? 0), finiteNumber(node.metadata.y ?? 0),
      hexEncode(pluginPath)
    ].join('\t'))
  }
  for (const connection of graph.connections) {
    lines.push([
      'EDGE', hexEncode(shortId(connection.from)), hexEncode(shortId(connection.to)),
      connection.kind === 'audio' ? 0 : 1,
      connection.fromPort ?? 0, connection.toPort ?? 0
    ].join('\t'))
  }
  lines.push('END')
  return `${lines.join('\n')}\n`
}

function firstSetting(settings, name) {
  const value = settings?.[name] ?? settings?.[`${BASE}${name}`] ?? ''
  return Array.isArray(value) ? String(value[0] ?? '') : String(value)
}

function fullId(value) {
  return value.includes('://') ? value : `${BASE}${value}`
}

function shortId(value) {
  return value.startsWith(BASE) ? value.slice(BASE.length) : value
}

function integer(value) {
  if (!/^\d+$/.test(value)) throw new Error(`Invalid integer: ${value}`)
  return Number(value)
}

function finiteNumber(value) {
  const result = Number(value)
  if (!Number.isFinite(result)) throw new Error(`Invalid number: ${value}`)
  return result
}

function hexEncode(value) {
  const text = String(value)
  return text ? Buffer.from(text, 'utf8').toString('hex') : '-'
}

function hexDecode(value) {
  if (value === '-') return ''
  if (!/^(?:[0-9a-fA-F]{2})+$/.test(value)) throw new Error('Invalid hexadecimal string')
  return Buffer.from(value, 'hex').toString('utf8')
}
