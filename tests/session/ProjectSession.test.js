// tests/session/ProjectSession.test.js

import { afterEach, describe, expect, it } from 'vitest'
import { mkdtemp, readFile, rm } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { ProjectSession } from '../../src/session/ProjectSession.js'

const base = {
  id: 'project:test',
  nodes: [
    { id: 'in', type: 'Input', ports: { audioOutputs: 1 } },
    { id: 'out', type: 'Output', ports: { audioInputs: 1 } }
  ],
  connections: [{ from: 'in', to: 'out', kind: 'audio' }]
}

let directories = []
afterEach(async () => {
  await Promise.all(directories.map(directory => rm(directory, { recursive: true, force: true })))
  directories = []
})

describe('ProjectSession', () => {
  it('supports validated updates, undo, and redo', () => {
    const session = new ProjectSession()
    session.open({ ...base, transport: { tempoMap: [{ beat: 0, bpm: 100 }], positionBeats: 2 } })
    session.update(definition => ({ ...definition, label: 'Updated' }))
    expect(session.graph.label).toBe('Updated')
    expect(session.undo()).toBe(true)
    expect(session.graph.label).toBe('')
    expect(session.redo()).toBe(true)
    expect(session.graph.label).toBe('Updated')
  })

  it('saves and loads project JSON atomically', async () => {
    const directory = await mkdtemp(join(tmpdir(), 'transmission-session-'))
    directories.push(directory)
    const filePath = join(directory, 'project.json')
    const session = new ProjectSession()
    session.open({ ...base, transport: { tempoMap: [{ beat: 0, bpm: 100 }], positionBeats: 2 } })
    await session.save(filePath)
    const loaded = await ProjectSession.load(filePath)
    expect(JSON.parse(await readFile(filePath, 'utf8')).id).toBe('project:test')
    expect(loaded.compiledGraph.executionOrder).toEqual(['in', 'out'])
    expect(loaded.transport.tempoAt(0)).toBe(100)
    expect(loaded.transport.positionBeats).toBe(2)
  })

  it('round-trips the canonical Turtle format', async () => {
    const directory = await mkdtemp(join(tmpdir(), 'transmission-session-'))
    directories.push(directory)
    const filePath = join(directory, 'project.ttl')
    const session = new ProjectSession()
    session.open({ ...base, transport: { tempoMap: [{ beat: 0, bpm: 90 }, { beat: 4, bpm: 120 }], loop: { startBeat: 2, endBeat: 6 } } })
    await session.save(filePath)
    const loaded = await ProjectSession.load(filePath)
    expect(loaded.graph.nodes.size).toBe(2)
    expect(loaded.compiledGraph.executionOrder).toEqual(['in', 'out'])
    expect(loaded.transport.tempoAt(0)).toBe(90)
    expect(loaded.transport.tempoAt(4)).toBe(120)
    expect(loaded.transport.loop).toMatchObject({ startBeat: 2, endBeat: 6, enabled: true })
  })
})
