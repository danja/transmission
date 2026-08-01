#!/usr/bin/env node

import { writeFile } from 'node:fs/promises'
import { resolve } from 'node:path'
import { Arrangement } from '../src/model/Arrangement.js'
import { Graph } from '../src/model/Graph.js'
import { serializeGraph } from '../src/rdf/TransmissionRdf.js'
import { createGenOmegaArrangement } from './gen-omega-arrangement.js'

const BASE = 'http://purl.org/stuff/transmissions/'
const VST = '/home/danny/.vst3'
const MIDIMIX_PORT = 'Midi-Bridge:MIDI Mix MIDI 1 (capture)'
const MIDIMIX_STRIPS = [
  { target: 'field-gain', gain: 19, pan: 18 },
  { target: 'fife-gain', gain: 23, pan: 22 },
  { target: 'main-gain', gain: 27, pan: 26 },
  { target: 'bass-gain', gain: 31, pan: 30 },
  { target: 'acid-gain', gain: 49, pan: 48 },
  { target: 'chords-gain', gain: 53, pan: 52 }
]

export function createGenOmegaProject() {
  const recipe = createGenOmegaArrangement()
  const nodes = []
  const connections = []
  const positions = new Map()

  const plugin = (id, label, bundle, x, y, instrument = false) => {
    nodes.push({ id: `${BASE}${id}`, type: `${BASE}VST3Plugin`, label,
      ports: { audioInputs: instrument ? 0 : 2, audioOutputs: 2, midiInputs: instrument ? 1 : 0 },
      settings: { pluginPath: `${VST}/${bundle}.vst3` }, metadata: { x, y } })
    positions.set(id, `${BASE}${id}`)
  }
  const gain = (id, label, gainDb, x, y) => {
    nodes.push({ id: `${BASE}${id}`, type: `${BASE}Gain`, label,
      ports: { audioInputs: 2, audioOutputs: 2, midiInputs: 1 }, settings: { gainDb }, metadata: { x, y } })
    positions.set(id, `${BASE}${id}`)
  }
  const stereo = (from, to) => {
    for (let port = 0; port < 2; ++port)
      connections.push({ from: positions.get(from), to: positions.get(to), kind: 'audio', fromPort: port, toPort: port })
  }

  nodes.push({ id: `${BASE}midimix-input`, type: `${BASE}MidiInput`, label: 'Akai MIDImix',
    ports: { midiOutputs: 1 }, settings: { externalPort: MIDIMIX_PORT }, metadata: { x: 40, y: 700 } })
  positions.set('midimix-input', `${BASE}midimix-input`)

  plugin('field-drums', 'Field Drums — DrumKit', 'drumkit', 40, 40, true)
  gain('field-gain', 'Field Drums Gain', -12, 270, 40)
  stereo('field-drums', 'field-gain')

  plugin('fife', 'Fife — Canticle', 'canticle', 40, 170, true)
  gain('fife-gain', 'Fife Gain', -12, 270, 170)
  stereo('fife', 'fife-gain')

  plugin('main-drums', 'Main Drums — DrumKit', 'drumkit', 40, 300, true)
  gain('main-gain', 'Main Drums Gain', -6, 500, 300)
  stereo('main-drums', 'main-gain')

  plugin('bass', 'Bass — Basilico Dub', 'basilico', 40, 430, true)
  gain('bass-gain', 'Bass Gain', -9, 500, 430)
  stereo('bass', 'bass-gain')

  plugin('acid', 'Acid Lead — Basilico Acid', 'basilico', 40, 560, true)
  gain('acid-gain', 'Acid Gain', -12, 500, 560)
  stereo('acid', 'acid-gain')

  plugin('chords', 'Kraut Chords — Canticle', 'canticle', 40, 690, true)
  plugin('chord-bloom', 'Chord Bloom — Orchid', 'orchid', 270, 690)
  gain('chords-gain', 'Kraut Chords Gain', -12, 500, 690)
  stereo('chords', 'chord-bloom'); stereo('chord-bloom', 'chords-gain')

  plugin('shared-space', 'Shared Space — Ambo', 'ambo', 650, 170)
  for (const role of ['field-gain', 'fife-gain', 'chords-gain'])
    stereo(role, 'shared-space')

  gain('master-gain', 'Master Gain / Fade', -3, 850, 330)
  nodes.push({ id: `${BASE}system-output`, type: `${BASE}AudioOutput`, label: 'System Output',
    ports: { audioInputs: 2 }, metadata: { x: 1080, y: 330 } })
  positions.set('system-output', `${BASE}system-output`)
  for (const role of ['shared-space', 'main-gain', 'bass-gain', 'acid-gain'])
    stereo(role, 'master-gain')
  stereo('master-gain', 'system-output')

  for (const { target } of MIDIMIX_STRIPS)
    connections.push({ from: positions.get('midimix-input'), to: positions.get(target),
      kind: 'midi', fromPort: 0, toPort: 0 })
  connections.push({ from: positions.get('midimix-input'), to: positions.get('master-gain'),
    kind: 'midi', fromPort: 0, toPort: 0 })

  const midiMappings = MIDIMIX_STRIPS.flatMap(({ target, gain: gainCc, pan: panCc }) => [
    { targetNodeId: positions.get(target), parameterId: 0, channel: -1, controller: gainCc, consume: true },
    { targetNodeId: positions.get(target), parameterId: 1, channel: -1, controller: panCc, consume: true }
  ])
  midiMappings.push({ targetNodeId: positions.get('master-gain'), parameterId: 0,
    channel: -1, controller: 62, consume: true })

  const graph = new Graph({ id: `${BASE}gen-omega`, label: 'Gen Omega', nodes, connections,
    metadata: {
      systemOutputConnections: ['system:playback_1', 'system:playback_2'],
      midiMappings
    } })
  const trackTargets = ['field-drums', 'fife', 'main-drums', 'bass', 'acid', 'chords']
  const midiClips = recipe.tracks.flatMap((track, trackIndex) => track.clips.map((clip, clipIndex) => ({
    id: `${trackTargets[trackIndex]}-${clipIndex + 1}`,
    targetNodeId: `${BASE}${trackTargets[trackIndex]}`,
    startBeat: clip.startBeat,
    lengthBeats: clip.lengthBeats,
    notes: clip.notes.map(note => ({ startBeat: note.startBeat, durationBeats: note.durationBeats,
      pitch: note.pitch, velocity: note.velocity, channel: note.channel }))
  })))
  const arrangement = new Arrangement({ lengthBeats: 420, midiClips,
    gainLanes: [{ targetNodeId: `${BASE}master-gain`, points: [
      { beat: 0, valueDb: 0, shape: 'step' },
      { beat: 384, valueDb: 0, shape: 'linear' },
      { beat: 420, valueDb: -120, shape: 'linear' }
    ] }] }, graph)
  return { graph, transport: { tempoMap: [{ beat: 0, bpm: 140 }] }, arrangement }
}

if (process.argv[1] && resolve(process.argv[1]) === resolve(new URL(import.meta.url).pathname)) {
  const output = resolve(process.argv[2] ?? 'projects/gen-omega.ttl')
  const project = createGenOmegaProject()
  await writeFile(output, serializeGraph(project.graph, project.transport, project.arrangement), 'utf8')
  process.stdout.write(`${output}\n`)
}
