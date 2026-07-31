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
    const dataset = await parseTurtle(serializeGraph(graph, transport, arrangement))
    const restoredGraph = graphFromDataset(dataset, graph.id)
    expect(arrangementFromDataset(dataset, graph.id, restoredGraph).toJSON()).toEqual(arrangement.toJSON())
  }, 15_000)
})
