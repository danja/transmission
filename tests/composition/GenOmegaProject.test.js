import { describe, expect, it } from 'vitest'
import { createGenOmegaProject } from '../../scripts/gen-omega-project.js'
import { parseTurtle, arrangementFromDataset, graphFromDataset, serializeGraph } from '../../src/rdf/TransmissionRdf.js'

describe('Gen Omega Transmission project', () => {
  it('contains the complete deterministic performance and master fade', async () => {
    const { graph, transport, arrangement } = createGenOmegaProject()
    expect(arrangement.midiClips).toHaveLength(17)
    expect(arrangement.midiClips.flatMap(clip => clip.notes)).toHaveLength(2059)
    expect(arrangement.lengthBeats).toBe(420)
    expect(arrangement.gainLanes[0].points.at(-1)).toMatchObject({ beat: 420, valueDb: -120 })
    const amboNodes = [...graph.nodes.values()].filter(node => node.settings?.pluginPath?.endsWith('/ambo.vst3'))
    expect(amboNodes).toHaveLength(1)
    expect(amboNodes[0].label).toBe('Shared Space — Ambo')
    expect(graph.node('http://purl.org/stuff/transmissions/midimix-input')?.settings.externalPort)
      .toBe('Midi-Bridge:MIDI Mix MIDI 1 (capture)')
    expect(graph.connections.filter(connection => connection.kind === 'midi')).toHaveLength(7)
    expect(graph.metadata.midiMappings).toHaveLength(13)
    expect(graph.metadata.midiMappings.at(-1)).toMatchObject({
      targetNodeId: 'http://purl.org/stuff/transmissions/master-gain',
      parameterId: 0,
      controller: 62
    })
    const dataset = await parseTurtle(serializeGraph(graph, transport, arrangement))
    const restoredGraph = graphFromDataset(dataset, graph.id)
    expect(restoredGraph.metadata.midiMappings).toEqual(graph.metadata.midiMappings)
    expect(arrangementFromDataset(dataset, graph.id, restoredGraph).toJSON()).toEqual(arrangement.toJSON())
  }, 15_000)
})
