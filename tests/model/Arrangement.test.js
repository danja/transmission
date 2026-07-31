import { describe, expect, it } from 'vitest'
import { Arrangement } from '../../src/model/Arrangement.js'
import { Graph } from '../../src/model/Graph.js'

const graph = new Graph({
  id: 'project:test',
  nodes: [{ id: 'synth', type: 'Plugin' }, { id: 'gain', type: 'Gain' }],
  connections: []
})

describe('Arrangement', () => {
  it('validates and copies beat-domain MIDI clips and gain lanes', () => {
    const arrangement = new Arrangement({
      lengthBeats: 16,
      midiClips: [{ id: 'clip', targetNodeId: 'synth', startBeat: 4, lengthBeats: 4,
        notes: [{ startBeat: 0, durationBeats: 1, pitch: 60, velocity: 100, channel: 0 }] }],
      gainLanes: [{ targetNodeId: 'gain', points: [
        { beat: 0, valueDb: -6, shape: 'step' }, { beat: 16, valueDb: -120, shape: 'linear' }
      ] }]
    }, graph)
    expect(arrangement.toJSON().midiClips[0].notes[0].pitch).toBe(60)
  })

  it('rejects duplicate clips, missing targets, invalid MIDI, and unordered gain points', () => {
    const clip = { id: 'same', targetNodeId: 'synth', startBeat: 0, lengthBeats: 4, notes: [] }
    expect(() => new Arrangement({ lengthBeats: 8, midiClips: [clip, clip] }, graph)).toThrow('duplicate')
    expect(() => new Arrangement({ lengthBeats: 8, midiClips: [{ ...clip, targetNodeId: 'missing' }] }, graph)).toThrow('missing')
    expect(() => new Arrangement({ lengthBeats: 8, midiClips: [{ ...clip,
      notes: [{ startBeat: 0, durationBeats: 1, pitch: 128, velocity: 1 }] }] }, graph)).toThrow('pitch')
    expect(() => new Arrangement({ lengthBeats: 8, gainLanes: [{ targetNodeId: 'gain', points: [
      { beat: 2, valueDb: 0 }, { beat: 1, valueDb: -1 }
    ] }] }, graph)).toThrow('increasing')
  })
})
