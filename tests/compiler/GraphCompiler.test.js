// tests/compiler/GraphCompiler.test.js

import { describe, expect, it } from 'vitest'
import { compileGraph, GraphCompilationError } from '../../src/compiler/GraphCompiler.js'

const nodes = [
  { id: 'input', type: 'AudioInput', ports: { audioOutputs: 1 } },
  { id: 'gain', type: 'VST3Plugin', ports: { audioInputs: 1, audioOutputs: 1 } },
  { id: 'output', type: 'AudioOutput', ports: { audioInputs: 1 } }
]

describe('compileGraph', () => {
  it('returns deterministic execution order', () => {
    const compiled = compileGraph({
      id: 'project:test', nodes,
      connections: [
        { from: 'input', to: 'gain', kind: 'audio' },
        { from: 'gain', to: 'output', kind: 'audio' }
      ]
    })
    expect(compiled.executionOrder).toEqual(['input', 'gain', 'output'])
    expect(Object.isFrozen(compiled)).toBe(true)
  })

  it('rejects dangling connections', () => {
    expect(() => compileGraph({
      id: 'project:test', nodes,
      connections: [{ from: 'gain', to: 'missing', kind: 'audio' }]
    })).toThrow(GraphCompilationError)
  })

  it('rejects audio cycles', () => {
    expect(() => compileGraph({
      id: 'project:test', nodes,
      connections: [
        { from: 'input', to: 'gain', kind: 'audio' },
        { from: 'gain', to: 'input', kind: 'audio' }
      ]
    })).toThrow('Audio graph contains a cycle')
  })

  it('rejects incompatible ports', () => {
    expect(() => compileGraph({
      id: 'project:test', nodes,
      connections: [{ from: 'output', to: 'input', kind: 'audio' }]
    })).toThrow('Invalid audio output port 0 on output')
  })
})
