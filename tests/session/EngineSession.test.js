// tests/session/EngineSession.test.js

import { describe, expect, it, vi } from 'vitest'
import { EngineSession } from '../../src/session/EngineSession.js'

const definition = {
  id: 'project:test',
  nodes: [
    { id: 'in', type: 'Input', ports: { audioOutputs: 1 } },
    { id: 'out', type: 'Output', ports: { audioInputs: 1 } }
  ],
  connections: [{ from: 'in', to: 'out', kind: 'audio' }]
}

function bridge() {
  return {
    loadProject: vi.fn(), configureTransport: vi.fn(), startAudio: vi.fn(), stopAudio: vi.fn(), dispose: vi.fn()
  }
}

describe('EngineSession', () => {
  it('compiles before loading and controls lifecycle', () => {
    const native = bridge()
    const session = new EngineSession({ bridge: native })
    session.open(definition)
    session.start()
    session.stop()
    session.dispose()
    expect(native.loadProject).toHaveBeenCalledOnce()
    expect(native.configureTransport).toHaveBeenCalledOnce()
    expect(native.startAudio).toHaveBeenCalledOnce()
    expect(native.stopAudio).toHaveBeenCalledOnce()
    expect(session.state).toBe('disposed')
  })

  it('does not permit graph edits while running', () => {
    const native = bridge()
    const session = new EngineSession({ bridge: native })
    session.open(definition)
    session.start()
    expect(() => session.update(graph => graph)).toThrow('Stop audio')
  })
})
