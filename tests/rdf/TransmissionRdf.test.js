// tests/rdf/TransmissionRdf.test.js

import { describe, expect, it } from 'vitest'
import { Graph } from '../../src/model/Graph.js'
import { graphFromDataset, parseTurtle, serializeGraph, transportFromDataset } from '../../src/rdf/TransmissionRdf.js'

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
          settings: { pluginPath: '/home/test/drumgen.vst3' }, metadata: { x: 240, y: 30 }
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
        systemOutputConnections: ['playback:left', 'playback:right']
      }
    })
    const transport = {
      tempoMap: [{ beat: 0, bpm: 128 }],
      loop: { startBeat: 0, endBeat: 32, enabled: true }
    }
    const dataset = await parseTurtle(serializeGraph(graph, transport))
    const restored = graphFromDataset(dataset, graph.id)
    expect(restored.connections).toEqual(graph.connections)
    expect(restored.node(`${base}drumgen`).settings[`${base}pluginPath`]).toEqual(['/home/test/drumgen.vst3'])
    expect(restored.node(`${base}drumgen`).metadata).toEqual({ x: 240, y: 30 })
    expect(restored.metadata.systemOutputConnections).toEqual(['playback:left', 'playback:right'])
    expect(transportFromDataset(dataset, graph.id)).toEqual(transport)
  })
})
