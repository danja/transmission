// tests/bridge/NativeBridge.test.js

import { describe, expect, it } from 'vitest'
import { NativeBridge } from '../../src/bridge/NativeBridge.js'

describe('NativeBridge', () => {
  it('keeps control calls narrow and forwards arguments', () => {
    const calls = []
    const bridge = new NativeBridge({
      setParameter: (...args) => calls.push(args),
      disposeEngine: () => calls.push(['dispose'])
    })
    bridge.setParameter('node', 'gain', 0.5, 32)
    bridge.dispose()
    expect(calls).toEqual([['node', 'gain', 0.5, 32], ['dispose']])
  })

  it('rejects calls after disposal', () => {
    const bridge = new NativeBridge({ getDiagnostics: () => ({}) })
    bridge.dispose()
    expect(() => bridge.getDiagnostics()).toThrow('disposed')
  })

  it('forwards offline render calls with graph and options', () => {
    const calls = []
    const bridge = new NativeBridge({
      renderAudio: (...args) => calls.push(args)
    })
    const graph = { nodes: [], connections: [] }
    const options = { outputPath: '/tmp/out.wav', totalBeats: 4 }
    bridge.renderAudio(graph, options)
    expect(calls).toEqual([[graph, options]])
  })
})
