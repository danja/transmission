import { describe, expect, it } from 'vitest'
import {
  TransmissionControlService,
  applyGraphOperations
} from '../../src/control/TransmissionControlService.js'
import { parseChangeSet } from '../../src/http/TurtleCodec.js'

const TRN = 'http://purl.org/stuff/transmissions/'

const graph = {
  id: `${TRN}midi-mapping-test`,
  nodes: [
    { id: 'synth', type: 'VST3Plugin', ports: { audioOutputs: 2 } },
    { id: 'output', type: 'Output', ports: { audioInputs: 2 } }
  ],
  connections: [{ from: 'synth', to: 'output', kind: 'audio' }]
}

const mapping = { targetNodeId: 'synth', parameterId: 3, channel: -1, controller: 19, consume: true }

function controlWithProject() {
  const control = new TransmissionControlService()
  control.newProject(structuredClone(graph))
  return control
}

describe('MIDI CC mapping operations', () => {
  it('adds a mapping with defaults applied and surfaces it from project_get', () => {
    const control = controlWithProject()
    const changed = control.applyGraphChanges({
      expectedRevision: 0,
      operations: [{ type: 'addMidiMapping', mapping: { targetNodeId: 'synth', parameterId: 3, controller: 19 } }]
    })
    expect(changed.graph.metadata.midiMappings).toEqual([mapping])
    expect(control.describeProject().graph.metadata.midiMappings).toEqual([mapping])
    expect(control.status().revision).toBe(1)
  })

  it('rejects a duplicate mapping without changing the project', () => {
    const control = controlWithProject()
    const operations = [{ type: 'addMidiMapping', mapping }]
    control.applyGraphChanges({ expectedRevision: 0, operations })
    expect(() => control.applyGraphChanges({ expectedRevision: 1, operations }))
      .toThrow('MIDI mapping already exists')
    expect(control.status().revision).toBe(1)
    expect(control.describeProject().graph.metadata.midiMappings).toHaveLength(1)
  })

  it('rejects invalid mappings', () => {
    const cases = [
      [{ targetNodeId: 'missing', parameterId: 0, controller: 1 }, 'unknown node'],
      [{ targetNodeId: 'synth', parameterId: -1, controller: 1 }, 'non-negative integer'],
      [{ targetNodeId: 'synth', parameterId: 0, controller: 128 }, '0–127'],
      [{ targetNodeId: 'synth', parameterId: 0, controller: 1, channel: 16 }, '-1'],
      [{ targetNodeId: 'synth', parameterId: 0, controller: 1, consume: 'yes' }, 'boolean'],
      [{ targetNodeId: '', parameterId: 0, controller: 1 }, 'targetNodeId is required'],
      [null, 'must be an object']
    ]
    for (const [bad, message] of cases) {
      expect(() => applyGraphOperations(structuredClone(graph), [{ type: 'addMidiMapping', mapping: bad }]),
        JSON.stringify(bad)).toThrow(message)
    }
  })

  it('removes a mapping and fails when it does not exist', () => {
    const control = controlWithProject()
    control.applyGraphChanges({ expectedRevision: 0, operations: [{ type: 'addMidiMapping', mapping }] })
    control.applyGraphChanges({ expectedRevision: 1, operations: [{ type: 'removeMidiMapping', mapping }] })
    expect(control.describeProject().graph.metadata.midiMappings).toEqual([])
    expect(() => control.applyGraphChanges({
      expectedRevision: 2, operations: [{ type: 'removeMidiMapping', mapping }]
    })).toThrow('MIDI mapping does not exist')
    expect(control.status().revision).toBe(2)
  })

  it('prunes mappings when their target node is removed', () => {
    const next = applyGraphOperations(structuredClone(graph), [
      { type: 'addMidiMapping', mapping },
      { type: 'removeNode', nodeId: 'synth' }
    ])
    expect(next.metadata.midiMappings).toEqual([])
    expect(next.nodes.map(node => node.id)).toEqual(['output'])
  })

  it('validates mappings smuggled in via setProjectMetadata', () => {
    const valid = applyGraphOperations(structuredClone(graph), [
      { type: 'setProjectMetadata', metadata: { midiMappings: [{ ...mapping }] } }
    ])
    expect(valid.metadata.midiMappings).toEqual([mapping])
    expect(() => applyGraphOperations(structuredClone(graph), [
      { type: 'setProjectMetadata', metadata: { midiMappings: 'all' } }
    ])).toThrow('must be an array')
    expect(() => applyGraphOperations(structuredClone(graph), [
      { type: 'setProjectMetadata', metadata: { midiMappings: [{ ...mapping, channel: 99 }] } }
    ])).toThrow('-1')
  })

  it('supports dry-run without persisting', () => {
    const control = controlWithProject()
    const preview = control.applyGraphChanges({
      expectedRevision: 0, dryRun: true, operations: [{ type: 'addMidiMapping', mapping }]
    })
    expect(preview.graph.metadata.midiMappings).toEqual([mapping])
    expect(control.describeProject().graph.metadata.midiMappings ?? []).toEqual([])
    expect(control.status().revision).toBe(0)
  })

  it('round-trips mapping operations through a Turtle ChangeSet', async () => {
    const turtle = [
      `@prefix trn: <${TRN}> .`,
      '[] a trn:ChangeSet ;',
      '   trn:expectedRevision 0 ;',
      '   trn:dryRun false ;',
      '   trn:operations (',
      `     [ a trn:AddMidiMapping ; trn:mappingJson ${JSON.stringify(JSON.stringify(mapping))} ]`,
      '   ) .'
    ].join('\n')
    const parsed = await parseChangeSet(turtle)
    expect(parsed.operations).toEqual([{ type: 'addMidiMapping', mapping }])
    const control = controlWithProject()
    control.applyGraphChanges({ expectedRevision: parsed.expectedRevision, operations: parsed.operations })
    expect(control.describeProject().graph.metadata.midiMappings).toEqual([mapping])
  })
})
