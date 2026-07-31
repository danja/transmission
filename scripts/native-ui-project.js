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
      transport: project.transport,
      arrangement: project.arrangement
    })
    await session.save(filePath)
  } else {
    await readFile(filePath, 'utf8')
    const session = await ProjectSession.load(filePath)
    writeAll(1, encodeProject(session))
  }
} catch (error) {
  writeSync(2, `${error instanceof Error ? error.message : String(error)}\n`)
  process.exitCode = 1
}

function writeAll(descriptor, content) {
  const buffer = Buffer.from(content, 'utf8')
  const pause = new Int32Array(new SharedArrayBuffer(4))
  let offset = 0
  while (offset < buffer.length) {
    try {
      const written = writeSync(descriptor, buffer, offset, buffer.length - offset)
      if (written <= 0) throw new Error('Unable to write native UI project interchange')
      offset += written
    } catch (error) {
      if (error?.code !== 'EAGAIN' && error?.code !== 'EINTR') throw error
      Atomics.wait(pause, 0, 0, 1)
    }
  }
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
    },
    arrangement: { lengthBeats: 0, midiClips: [], gainLanes: [] }
  }
  let header = false
  let ended = false
  for (const [index, line] of text.split(/\r?\n/).entries()) {
    if (!line) continue
    const fields = line.split('\t')
    const invalid = () => { throw new Error(`Invalid native UI project interchange at line ${index + 1}`) }
    if (!header) {
      if (fields.length !== 2 || fields[0] !== 'TRANSMISSION_UI' ||
          !['1', '2', '3'].includes(fields[1])) invalid()
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
      if (kind < 0 || kind > 6 || !id) invalid()
      const resource = hexDecode(fields[10])
      const type = ['AudioInput', 'AudioOutput', 'PassThrough', 'VST3Plugin', 'MidiInput', 'MidiOutput', 'Gain'][kind]
      project.nodes.push({
        id: fullId(id),
        label: hexDecode(fields[2]),
        type: `${BASE}${type}`,
        ports: {
          audioInputs: integer(fields[4]),
          audioOutputs: integer(fields[5]),
          midiInputs: integer(fields[6]),
          midiOutputs: integer(fields[7])
        },
        settings: resource
          ? { [kind >= 4 ? 'externalPort' : 'pluginPath']: resource }
          : {},
        parameters: [],
        state: { component: '', controller: '' },
        metadata: { x: finiteNumber(fields[8]), y: finiteNumber(fields[9]) }
      })
    } else if (fields[0] === 'PARAM' && fields.length === 4) {
      const node = project.nodes.find(candidate => candidate.id === fullId(hexDecode(fields[1])))
      const id = integer(fields[2])
      const normalizedValue = finiteNumber(fields[3])
      if (!node || normalizedValue < 0 || normalizedValue > 1) invalid()
      node.parameters.push({ id, normalizedValue })
    } else if (fields[0] === 'STATE' && fields.length === 4) {
      const node = project.nodes.find(candidate => candidate.id === fullId(hexDecode(fields[1])))
      if (!node) invalid()
      node.state = {
        component: hexDecodeBuffer(fields[2]).toString('base64'),
        controller: hexDecodeBuffer(fields[3]).toString('base64')
      }
    } else if (fields[0] === 'NODE_GAIN' && fields.length === 3) {
      const node = project.nodes.find(candidate => candidate.id === fullId(hexDecode(fields[1])))
      if (!node || shortId(node.type) !== 'Gain') invalid()
      node.settings.gainDb = finiteNumber(fields[2])
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
    } else if (fields[0] === 'ARRANGEMENT' && fields.length === 2) {
      project.arrangement.lengthBeats = finiteNumber(fields[1])
      if (project.arrangement.lengthBeats < 0) invalid()
    } else if (fields[0] === 'CLIP' && fields.length === 5) {
      const clip = {
        id: hexDecode(fields[1]), targetNodeId: fullId(hexDecode(fields[2])),
        startBeat: finiteNumber(fields[3]), lengthBeats: finiteNumber(fields[4]), notes: []
      }
      if (!clip.id || clip.startBeat < 0 || clip.lengthBeats <= 0) invalid()
      project.arrangement.midiClips.push(clip)
    } else if (fields[0] === 'NOTE' && fields.length === 7) {
      const clip = project.arrangement.midiClips.find(candidate => candidate.id === hexDecode(fields[1]))
      const note = {
        startBeat: finiteNumber(fields[2]), durationBeats: finiteNumber(fields[3]),
        pitch: integer(fields[4]), velocity: integer(fields[5]), channel: integer(fields[6])
      }
      if (!clip || note.startBeat < 0 || note.durationBeats <= 0 ||
          note.startBeat + note.durationBeats > clip.lengthBeats || note.pitch > 127 ||
          note.velocity < 1 || note.velocity > 127 || note.channel > 15) invalid()
      clip.notes.push(note)
    } else if (fields[0] === 'GAIN_LANE' && fields.length === 2) {
      project.arrangement.gainLanes.push({ targetNodeId: fullId(hexDecode(fields[1])), points: [] })
    } else if (fields[0] === 'GAIN_POINT' && fields.length === 5) {
      const targetNodeId = fullId(hexDecode(fields[1]))
      const lane = project.arrangement.gainLanes.find(candidate => candidate.targetNodeId === targetNodeId)
      const point = { beat: finiteNumber(fields[2]), valueDb: finiteNumber(fields[3]),
        shape: fields[4] === '1' ? 'linear' : fields[4] === '0' ? 'step' : invalid() }
      if (!lane || point.beat < 0 || (lane.points.length && point.beat <= lane.points.at(-1).beat)) invalid()
      lane.points.push(point)
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
    'TRANSMISSION_UI\t3',
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
    const kind = {
      AudioInput: 0, AudioOutput: 1, PassThrough: 2,
      VST3Plugin: 3, MidiInput: 4, MidiOutput: 5, Gain: 6
    }[type]
    if (kind === undefined) throw new Error(`Unsupported native UI node type: ${node.type}`)
    const resource = firstSetting(node.settings, kind >= 4 ? 'externalPort' : 'pluginPath')
    lines.push([
      'NODE', hexEncode(shortId(node.id)), hexEncode(node.label), kind,
      node.ports.audioInputs, node.ports.audioOutputs,
      node.ports.midiInputs, node.ports.midiOutputs,
      finiteNumber(node.metadata.x ?? 0), finiteNumber(node.metadata.y ?? 0),
      hexEncode(resource)
    ].join('\t'))
    for (const parameter of node.parameters ?? []) {
      const id = integer(String(parameter.id))
      const normalizedValue = finiteNumber(parameter.normalizedValue)
      if (normalizedValue < 0 || normalizedValue > 1)
        throw new Error(`Invalid normalized parameter value: ${normalizedValue}`)
      lines.push(`PARAM\t${hexEncode(shortId(node.id))}\t${id}\t${normalizedValue}`)
    }
    const component = decodeBase64(node.state?.component ?? '')
    const controller = decodeBase64(node.state?.controller ?? '')
    if (component.length || controller.length) {
      lines.push(
        `STATE\t${hexEncode(shortId(node.id))}\t${hexEncodeBuffer(component)}\t${hexEncodeBuffer(controller)}`)
    }
    if (kind === 6)
      lines.push(`NODE_GAIN\t${hexEncode(shortId(node.id))}\t${finiteNumber(firstSetting(node.settings, 'gainDb') || 0)}`)
  }
  for (const connection of graph.connections) {
    lines.push([
      'EDGE', hexEncode(shortId(connection.from)), hexEncode(shortId(connection.to)),
      connection.kind === 'audio' ? 0 : 1,
      connection.fromPort ?? 0, connection.toPort ?? 0
    ].join('\t'))
  }
  const arrangement = session.arrangement.toJSON()
  lines.push(`ARRANGEMENT\t${arrangement.lengthBeats}`)
  for (const clip of arrangement.midiClips) {
    lines.push(`CLIP\t${hexEncode(clip.id)}\t${hexEncode(shortId(clip.targetNodeId))}\t${clip.startBeat}\t${clip.lengthBeats}`)
    for (const note of clip.notes)
      lines.push(`NOTE\t${hexEncode(clip.id)}\t${note.startBeat}\t${note.durationBeats}\t${note.pitch}\t${note.velocity}\t${note.channel}`)
  }
  for (const lane of arrangement.gainLanes) {
    lines.push(`GAIN_LANE\t${hexEncode(shortId(lane.targetNodeId))}`)
    for (const point of lane.points)
      lines.push(`GAIN_POINT\t${hexEncode(shortId(lane.targetNodeId))}\t${point.beat}\t${point.valueDb}\t${point.shape === 'linear' ? 1 : 0}`)
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

function hexEncodeBuffer(value) {
  return value.length ? value.toString('hex') : '-'
}

function hexDecodeBuffer(value) {
  if (value === '-') return Buffer.alloc(0)
  if (!/^(?:[0-9a-fA-F]{2})+$/.test(value)) throw new Error('Invalid hexadecimal string')
  return Buffer.from(value, 'hex')
}

function decodeBase64(value) {
  if (!value) return Buffer.alloc(0)
  if (!/^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/.test(value))
    throw new Error('Invalid base64 plugin state')
  return Buffer.from(value, 'base64')
}
