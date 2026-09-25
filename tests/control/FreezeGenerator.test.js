import { afterEach, describe, expect, it } from 'vitest'
import {
  ProjectRevisionError,
  TransmissionControlService,
  freezeClipFromEvents
} from '../../src/control/TransmissionControlService.js'
import { ProjectSession } from '../../src/session/ProjectSession.js'
import { TransmissionHttpServer } from '../../src/http/TransmissionHttpServer.js'
import { TransmissionHttpClient } from '../../src/http/TransmissionHttpClient.js'

const TRN = 'http://purl.org/stuff/transmissions/'

const graph = {
  id: `${TRN}freeze-test`,
  nodes: [
    { id: 'gen', type: 'VST3Plugin', ports: { midiOutputs: 1 } },
    { id: 'syn', type: 'VST3Plugin', ports: { audioOutputs: 2, midiInputs: 1 } }
  ],
  connections: [{ from: 'gen', to: 'syn', kind: 'midi' }]
}

const captured = [
  { nodeId: 'gen', beatPosition: 0, status: 0x90, data1: 36, data2: 100 },
  { nodeId: 'gen', beatPosition: 0.5, status: 0x80, data1: 36, data2: 0 },
  { nodeId: 'gen', beatPosition: 1, status: 0x90, data1: 40, data2: 90 },
  { nodeId: 'gen', beatPosition: 1.5, status: 0x90, data1: 40, data2: 0 },
  { nodeId: 'gen', beatPosition: 2, status: 0x80, data1: 99, data2: 0 },
  { nodeId: 'gen', beatPosition: 3, status: 0x90, data1: 42, data2: 80 },
  { nodeId: 'other', beatPosition: 0, status: 0x90, data1: 60, data2: 100 }
]

function stubbedControl(events = captured) {
  const session = new ProjectSession()
  const calls = []
  const engine = {
    project: session,
    state: 'loaded',
    open: (definition, filePath) => session.open(definition, filePath),
    captureMidi: (durationBeats) => {
      calls.push(durationBeats)
      return events
    }
  }
  const control = new TransmissionControlService({ engine })
  control.newProject(structuredClone(graph))
  return { control, calls }
}

describe('freezeClipFromEvents', () => {
  const base = { sourceNodeId: 'gen', targetNodeId: 'syn', clipId: 'f', startBeat: 0, lengthBeats: 8 }

  it('pairs note on/off events into clip notes', () => {
    const clip = freezeClipFromEvents(captured, base)
    expect(clip.notes).toEqual([
      { startBeat: 0, durationBeats: 0.5, pitch: 36, velocity: 100, channel: 0 },
      { startBeat: 1, durationBeats: 0.5, pitch: 40, velocity: 90, channel: 0 },
      { startBeat: 3, durationBeats: 5, pitch: 42, velocity: 80, channel: 0 }
    ])
  })

  it('treats velocity-zero ons as offs and drops stray offs and other nodes', () => {
    const clip = freezeClipFromEvents([
      { nodeId: 'gen', beatPosition: 0, status: 0x90, data1: 50, data2: 70 },
      { nodeId: 'gen', beatPosition: 1, status: 0x90, data1: 50, data2: 0 },
      { nodeId: 'gen', beatPosition: 2, status: 0x80, data1: 51, data2: 0 },
      { nodeId: 'stranger', beatPosition: 0, status: 0x90, data1: 60, data2: 100 }
    ], base)
    expect(clip.notes).toEqual([
      { startBeat: 0, durationBeats: 1, pitch: 50, velocity: 70, channel: 0 }
    ])
  })

  it('rejects a missing target', () => {
    expect(() => freezeClipFromEvents([], { ...base, targetNodeId: '' })).toThrow('targetNodeId is required')
  })
})

describe('control freezeGenerator', () => {
  it('requires the native engine', async () => {
    const control = new TransmissionControlService()
    control.newProject(structuredClone(graph))
    expect(() => control.freezeGenerator({
      expectedRevision: 0, sourceNodeId: 'gen', targetNodeId: 'syn', durationBeats: 8
    })).toThrow('Native engine is required')
  })

  it('stores the frozen clip and bumps the revision', async () => {
    const { control, calls } = stubbedControl()
    const frozen = await control.freezeGenerator({
      expectedRevision: 0, sourceNodeId: 'gen', targetNodeId: 'syn',
      clipId: 'frozen-groove', durationBeats: 8
    })
    expect(calls).toEqual([8])
    expect(frozen.revision).toBe(1)
    expect(frozen.clip).toMatchObject({ id: 'frozen-groove', targetNodeId: 'syn', startBeat: 0, lengthBeats: 8 })
    expect(frozen.clip.notes).toHaveLength(3)
    expect(frozen.arrangement.midiClips.map(clip => clip.id)).toEqual(['frozen-groove'])
  })

  it('defaults the clip id and validates the source', async () => {
    const { control } = stubbedControl()
    const frozen = await control.freezeGenerator({
      expectedRevision: 0, sourceNodeId: 'gen', targetNodeId: 'syn', durationBeats: 8
    })
    expect(frozen.clip.id).toBe('freeze-gen-0')
    expect(() => control.freezeGenerator({
      expectedRevision: 1, sourceNodeId: '', targetNodeId: 'syn', durationBeats: 8
    })).toThrow('sourceNodeId is required')
    expect(() => control.freezeGenerator({
      expectedRevision: 1, sourceNodeId: 'ghost', targetNodeId: 'syn', durationBeats: 8
    })).toThrow('Freeze source does not exist')
  })

  it('rejects duplicates, bad revisions, and empty captures', async () => {
    const { control } = stubbedControl()
    await control.freezeGenerator({
      expectedRevision: 0, sourceNodeId: 'gen', targetNodeId: 'syn', clipId: 'dup', durationBeats: 8
    })
    expect(() => control.freezeGenerator({
      expectedRevision: 1, sourceNodeId: 'gen', targetNodeId: 'syn', clipId: 'dup', durationBeats: 8
    })).toThrow('already exists')
    expect(() => control.freezeGenerator({
      expectedRevision: 0, sourceNodeId: 'gen', targetNodeId: 'syn', clipId: 'stale', durationBeats: 8
    })).toThrow(ProjectRevisionError)
    const empty = stubbedControl([])
    expect(() => empty.control.freezeGenerator({
      expectedRevision: 0, sourceNodeId: 'gen', targetNodeId: 'syn', durationBeats: 0
    })).toThrow('must be positive')
  })
})

describe('freeze over live HTTP', () => {
  let server

  afterEach(async () => {
    await server?.close()
  })

  it('round-trips a freeze through Turtle', async () => {
    const { control } = stubbedControl()
    server = new TransmissionHttpServer(control, { port: 0, bindAddress: '127.0.0.1' })
    await server.listen()
    const client = new TransmissionHttpClient(`http://127.0.0.1:${server._server.address().port}`)
    const frozen = await client.freezeGenerator({
      expectedRevision: 0, sourceNodeId: 'gen', targetNodeId: 'syn',
      clipId: 'live-freeze', durationBeats: 8
    })
    expect(frozen.clip.notes).toHaveLength(3)
    expect(frozen.arrangement.midiClips.map(clip => clip.id)).toEqual(['live-freeze'])
    const reread = await client.getArrangement()
    expect(reread.arrangement.midiClips).toHaveLength(1)
  })
})
