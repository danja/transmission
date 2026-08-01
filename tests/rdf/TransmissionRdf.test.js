// tests/rdf/TransmissionRdf.test.js

import { describe, expect, it } from 'vitest'
import { Graph } from '../../src/model/Graph.js'
import { arrangementFromDataset, graphFromDataset, parseTurtle, serializeGraph, transportFromDataset } from '../../src/rdf/TransmissionRdf.js'
import { Arrangement } from '../../src/model/Arrangement.js'

describe('Transmission RDF adapter', () => {
  it('parses an RDF list into a typed graph', async () => {
    const dataset = await parseTurtle(`
      @prefix : <http://purl.org/stuff/transmissions/> .
      @prefix rdf: <http://www.w3.org/1999/02/22-rdf-syntax-ns#> .
      :main a :Transmission ; :pipe ( :input :output ) .
      :input a :AudioInput .
      :output a :AudioOutput .
    `)
    const graph = graphFromDataset(dataset, 'http://purl.org/stuff/transmissions/main')
    expect(graph.nodes.size).toBe(2)
    expect(graph.connections[0]).toMatchObject({ from: expect.stringContaining('input'), kind: 'audio' })
  })

  it('round trips explicit graph, editor, device, and transport state', async () => {
    const base = 'http://purl.org/stuff/transmissions/'
    const graph = new Graph({
      id: `${base}main`,
      label: 'GTK project',
      nodes: [
        {
          id: `${base}input`, type: `${base}AudioInput`, label: 'System Input',
          ports: { audioOutputs: 2, midiOutputs: 1 }, metadata: { x: 20, y: 30 }
        },
        {
          id: `${base}drumgen`, type: `${base}VST3Plugin`, label: 'drumgen',
          ports: { audioOutputs: 2, midiOutputs: 1 },
          settings: { pluginPath: '/home/test/drumgen.vst3' },
          parameters: [{ id: 42, normalizedValue: 0.75 }],
          state: { component: 'AAEC/w==', controller: 'ECA=' },
          metadata: { x: 240, y: 30 }
        },
        {
          id: `${base}output`, type: `${base}AudioOutput`, label: 'System Output',
          ports: { audioInputs: 2, midiInputs: 1 }, metadata: { x: 480, y: 30 }
        }
      ],
      connections: [
        { from: `${base}input`, to: `${base}drumgen`, kind: 'midi', fromPort: 0, toPort: 0 },
        { from: `${base}drumgen`, to: `${base}output`, kind: 'audio', fromPort: 1, toPort: 1 }
      ],
      metadata: {
        systemInputConnections: ['capture:left', 'capture:right'],
        systemOutputConnections: ['playback:left', 'playback:right'],
        midiMappings: [
          { targetNodeId: `${base}drumgen`, parameterId: 42, channel: -1, controller: 19, consume: true }
        ]
      }
    })
    const transport = {
      tempoMap: [{ beat: 0, bpm: 128 }],
      loop: { startBeat: 0, endBeat: 32, enabled: true }
    }
    const arrangement = new Arrangement({
      lengthBeats: 16,
      midiClips: [{ id: 'intro', targetNodeId: `${base}drumgen`, startBeat: 0, lengthBeats: 4,
        notes: [{ startBeat: 0.5, durationBeats: 0.25, pitch: 36, velocity: 100, channel: 9 }] }],
      gainLanes: [{ targetNodeId: `${base}output`, points: [
        { beat: 12, valueDb: 0, shape: 'linear' }, { beat: 16, valueDb: -120, shape: 'linear' }
      ] }]
    }, graph)
    const dataset = await parseTurtle(serializeGraph(graph, transport, arrangement))
    const restored = graphFromDataset(dataset, graph.id)
    expect(restored.connections).toEqual(graph.connections)
    expect(restored.node(`${base}drumgen`).settings[`${base}pluginPath`]).toEqual(['/home/test/drumgen.vst3'])
    expect(restored.node(`${base}drumgen`).parameters).toEqual([
      { id: 42, normalizedValue: 0.75 }
    ])
    expect(restored.node(`${base}drumgen`).state).toEqual({
      component: 'AAEC/w==', controller: 'ECA='
    })
    expect(restored.node(`${base}drumgen`).metadata).toEqual({ x: 240, y: 30 })
    expect(restored.metadata.systemOutputConnections).toEqual(['playback:left', 'playback:right'])
    expect(restored.metadata.midiMappings).toEqual(graph.metadata.midiMappings)
    expect(transportFromDataset(dataset, graph.id)).toEqual(transport)
    expect(arrangementFromDataset(dataset, graph.id, graph).toJSON()).toEqual(arrangement.toJSON())
  })
})
