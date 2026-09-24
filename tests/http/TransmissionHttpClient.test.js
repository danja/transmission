import { describe, it, expect } from 'vitest'
import { projectDefinitionToTurtle } from '../../src/http/TransmissionHttpClient.js'
import { parseNewProject } from '../../src/http/TurtleCodec.js'

const TRN = 'http://purl.org/stuff/transmissions/'

function sampleDefinition() {
  return {
    id: `${TRN}main`,
    label: 'round-trip',
    nodes: [
      {
        id: `${TRN}a`, type: `${TRN}VST3Plugin`, label: 'A',
        ports: { audioOutputs: 2 },
        settings: { pluginPath: '/tmp/a.vst3' },
        parameters: [{ id: 0, normalizedValue: 0.5 }]
      },
      { id: `${TRN}b`, type: `${TRN}AudioOutput`, ports: { audioInputs: 2 } }
    ],
    connections: [{ from: `${TRN}a`, to: `${TRN}b`, kind: 'audio', fromPort: 0, toPort: 0 }],
    metadata: { systemOutputConnections: ['playback:left'] },
    transport: { tempoMap: [{ beat: 0, bpm: 120 }] }
  }
}

describe('projectDefinitionToTurtle', () => {
  it('round-trips node types, settings, ports, and connections via parseNewProject', async () => {
    const turtle = projectDefinitionToTurtle(sampleDefinition())
    const parsed = await parseNewProject(turtle)
    expect(parsed.nodes).toHaveLength(2)
    expect(parsed.nodes.map(n => n.type)).toEqual([`${TRN}VST3Plugin`, `${TRN}AudioOutput`])
    expect(parsed.nodes[0].ports.audioOutputs).toBe(2)
    expect(parsed.nodes[0].settings[`${TRN}pluginPath`]).toEqual(['/tmp/a.vst3'])
    expect(parsed.nodes[0].parameters).toEqual([{ id: 0, normalizedValue: 0.5 }])
    expect(parsed.connections).toEqual([
      { from: `${TRN}a`, to: `${TRN}b`, kind: 'audio', fromPort: 0, toPort: 0 }
    ])
    expect(parsed.transport.tempoMap).toEqual([{ beat: 0, bpm: 120 }])
  })

  it('serializes an empty project without throwing', async () => {
    const turtle = projectDefinitionToTurtle({ id: `${TRN}main`, nodes: [], connections: [] })
    const parsed = await parseNewProject(turtle)
    expect(parsed.nodes).toEqual([])
  })

  it('rejects a node without a type', () => {
    const bad = sampleDefinition()
    delete bad.nodes[0].type
    expect(() => projectDefinitionToTurtle(bad)).toThrow(/type is required/)
  })
})
