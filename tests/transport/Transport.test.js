// tests/transport/Transport.test.js

import { describe, expect, it } from 'vitest'
import { Transport } from '../../src/transport/Transport.js'

describe('Transport', () => {
  it('starts, stops, seeks, and advances in beats', () => {
    const transport = new Transport({ sampleRate: 48000, tempo: 120 })
    transport.start()
    const result = transport.advance(24000)
    expect(result.endBeat).toBeCloseTo(1)
    transport.stop({ reset: true })
    expect(transport.positionBeats).toBe(0)
  })

  it('splits advancement at tempo changes', () => {
    const transport = new Transport({ sampleRate: 48000, tempo: 120 })
    transport.setTempo(60, 1)
    transport.start()
    const result = transport.advance(48000)
    expect(result.segments.length).toBeGreaterThan(1)
    expect(result.segments[0].bpm).toBe(120)
    expect(result.segments.at(-1).bpm).toBe(60)
  })

  it('wraps at an enabled loop boundary', () => {
    const transport = new Transport({ sampleRate: 48000, tempo: 120 })
    transport.setLoop(1, 2)
    transport.seek(1.5)
    transport.start()
    const result = transport.advance(24000)
    expect(result.wrapped).toBe(true)
    expect(transport.positionBeats).toBeCloseTo(1.5)
  })
})
