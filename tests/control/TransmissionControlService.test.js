import { afterEach, describe, expect, it, vi } from 'vitest'
import { mkdtemp, rm } from 'node:fs/promises'
import { join } from 'node:path'
import { tmpdir } from 'node:os'
import {
  ProjectRevisionError,
  TransmissionControlService
} from '../../src/control/TransmissionControlService.js'
import { EngineSession } from '../../src/session/EngineSession.js'

const graph = {
  id: 'http://purl.org/stuff/transmissions/mcp-test',
  nodes: [
    { id: 'input', type: 'Input', ports: { audioOutputs: 1 } },
    { id: 'output', type: 'Output', ports: { audioInputs: 1 } }
  ],
  connections: [{ from: 'input', to: 'output', kind: 'audio' }]
}

let directories = []
afterEach(async () => {
  await Promise.all(directories.map(directory => rm(directory, { recursive: true, force: true })))
  directories = []
})

describe('TransmissionControlService', () => {
  it('dry-runs and atomically applies revision-checked graph operations', () => {
    const control = new TransmissionControlService()
    control.newProject(graph)
    const operations = [{
      type: 'updateNode',
      nodeId: 'output',
      changes: { label: 'Speakers', metadata: { x: 500, y: 100 } }
    }]

    const preview = control.applyGraphChanges({ expectedRevision: 0, operations, dryRun: true })
    expect(preview.graph.nodes[1].label).toBe('Speakers')
    expect(control.status().revision).toBe(0)

    control.applyGraphChanges({ expectedRevision: 0, operations })
    expect(control.describeProject().graph.nodes[1].label).toBe('Speakers')
    expect(control.status().revision).toBe(1)
    expect(() => control.applyGraphChanges({ expectedRevision: 0, operations }))
      .toThrow(ProjectRevisionError)
  })

  it('rejects an invalid transaction without changing the project', () => {
    const control = new TransmissionControlService()
    control.newProject(graph)
    expect(() => control.applyGraphChanges({
      expectedRevision: 0,
      operations: [{
        type: 'addConnection',
        connection: { from: 'missing', to: 'output', kind: 'audio' }
      }]
    })).toThrow('Graph validation failed')
    expect(control.status().revision).toBe(0)
    expect(control.describeProject().graph.connections).toHaveLength(1)
  })

  it('saves Turtle only below an allowed root and tracks dirty state', async () => {
    const directory = await mkdtemp(join(tmpdir(), 'transmission-mcp-'))
    directories.push(directory)
    const control = new TransmissionControlService({ allowedRoots: [directory] })
    control.newProject(graph)
    expect(control.status().dirty).toBe(true)
    await control.saveProject('project.ttl')
    expect(control.status()).toMatchObject({ dirty: false, filePath: join(directory, 'project.ttl') })
    await expect(control.saveProject('../outside.ttl')).rejects.toThrow('outside the allowed roots')
  })

  it('emits status changes to registered listeners and supports cleanup', () => {
    const control = new TransmissionControlService()
    const received = []
    const off = control.onStatusChange(status => received.push(status.revision))
    control.newProject(graph)
    expect(received).toEqual([0])
    control.applyGraphChanges({ expectedRevision: 0, operations: [{ type: 'updateNode', nodeId: 'output', changes: { label: 'Out' } }] })
    expect(received).toEqual([0, 1])
    off()
    control.applyGraphChanges({ expectedRevision: 1, operations: [{ type: 'updateNode', nodeId: 'output', changes: { label: 'Out2' } }] })
    expect(received).toEqual([0, 1])
  })

  it('does not emit status change on dry-run applyGraphChanges', () => {
    const control = new TransmissionControlService()
    control.newProject(graph)
    const received = []
    control.onStatusChange(s => received.push(s.revision))
    control.applyGraphChanges({ expectedRevision: 0, operations: [{ type: 'updateNode', nodeId: 'output', changes: { label: 'Out' } }], dryRun: true })
    expect(received).toEqual([])
  })

  it('persists parameter changes and forwards them to a running native engine', () => {
    const bridge = {
      createEngine: vi.fn(), loadProject: vi.fn(), configureTransport: vi.fn(),
      startAudio: vi.fn(), stopAudio: vi.fn(), setParameter: vi.fn(),
      getDiagnostics: vi.fn(() => ({ processedBlocks: 12 })),
      getPeaks: vi.fn(() => ({ peakL: 0, peakR: 0 })), dispose: vi.fn()
    }
    const engine = new EngineSession({ bridge })
    const control = new TransmissionControlService({ engine })
    control.newProject(graph)
    engine.start()
    const changed = control.setParameter({
      expectedRevision: 0, nodeId: 'input', parameterId: 7, value: 0.25
    })
    expect(changed).toMatchObject({ revision: 1, appliedToRuntime: true })
    expect(bridge.setParameter).toHaveBeenCalledWith('input', 7, 0.25, 0)
    expect(control.describeProject().graph.nodes[0].parameters).toEqual([
      { id: 7, normalizedValue: 0.25 }
    ])
    expect(control.diagnostics().native).toEqual({ processedBlocks: 12 })
  })
})
