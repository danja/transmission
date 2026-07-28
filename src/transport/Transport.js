// src/transport/Transport.js

export class Transport {
  constructor({ sampleRate = 48000, tempo = 120 } = {}) {
    if (!Number.isFinite(sampleRate) || sampleRate <= 0) throw new RangeError('sampleRate must be positive')
    this.sampleRate = sampleRate
    this.tempoMap = [{ beat: 0, bpm: validateTempo(tempo) }]
    this.positionBeats = 0
    this.running = false
    this.loop = null
  }

  start() { this.running = true }

  stop({ reset = false } = {}) {
    this.running = false
    if (reset) this.positionBeats = 0
  }

  seek(beat) {
    if (!Number.isFinite(beat) || beat < 0) throw new RangeError('beat must be a non-negative number')
    this.positionBeats = beat
  }

  setTempo(bpm, atBeat = this.positionBeats) {
    bpm = validateTempo(bpm)
    if (!Number.isFinite(atBeat) || atBeat < 0) throw new RangeError('atBeat must be non-negative')
    const existing = this.tempoMap.findIndex(change => change.beat === atBeat)
    if (existing >= 0) this.tempoMap[existing] = { beat: atBeat, bpm }
    else this.tempoMap.push({ beat: atBeat, bpm })
    this.tempoMap.sort((a, b) => a.beat - b.beat)
  }

  tempoAt(beat = this.positionBeats) {
    let current = this.tempoMap[0].bpm
    for (const change of this.tempoMap) {
      if (change.beat > beat) break
      current = change.bpm
    }
    return current
  }

  setLoop(startBeat, endBeat, enabled = true) {
    if (!Number.isFinite(startBeat) || !Number.isFinite(endBeat) || startBeat < 0 || endBeat <= startBeat) {
      throw new RangeError('loop must have a non-negative start before its end')
    }
    this.loop = { startBeat, endBeat, enabled }
    if (this.positionBeats >= endBeat) this.positionBeats = startBeat
  }

  clearLoop() { this.loop = null }

  advance(frames) {
    if (!Number.isInteger(frames) || frames < 0) throw new RangeError('frames must be a non-negative integer')
    const startBeat = this.positionBeats
    if (!this.running || frames === 0) return { startBeat, endBeat: startBeat, segments: [], wrapped: false }

    let remainingFrames = frames
    const segments = []
    let wrapped = false
    while (remainingFrames > 0) {
      const bpm = this.tempoAt(this.positionBeats)
      const beatsPerFrame = bpm / (60 * this.sampleRate)
      const nextTempo = this.tempoMap.find(change => change.beat > this.positionBeats)
      const nextBoundary = Math.min(
        nextTempo?.beat ?? Infinity,
        this.loop?.enabled ? this.loop.endBeat : Infinity
      )
      const framesToBoundary = Number.isFinite(nextBoundary)
        ? Math.max(1, Math.ceil((nextBoundary - this.positionBeats) / beatsPerFrame))
        : remainingFrames
      const segmentFrames = Math.min(remainingFrames, framesToBoundary)
      const segmentStart = this.positionBeats
      const segmentEnd = segmentStart + segmentFrames * beatsPerFrame
      segments.push({ startFrame: frames - remainingFrames, frames: segmentFrames, startBeat: segmentStart, endBeat: segmentEnd, bpm })
      this.positionBeats = segmentEnd
      remainingFrames -= segmentFrames

      if (this.loop?.enabled && this.positionBeats >= this.loop.endBeat) {
        this.positionBeats = this.loop.startBeat
        wrapped = true
      }
    }
    return { startBeat, endBeat: this.positionBeats, segments, wrapped }
  }

  toJSON() {
    return {
      sampleRate: this.sampleRate,
      tempoMap: this.tempoMap.map(change => ({ ...change })),
      positionBeats: this.positionBeats,
      loop: this.loop ? { ...this.loop } : null
    }
  }

  load(state = {}) {
    if (state.sampleRate !== undefined) {
      if (!Number.isFinite(state.sampleRate) || state.sampleRate <= 0) throw new RangeError('sampleRate must be positive')
      this.sampleRate = state.sampleRate
    }
    if (Array.isArray(state.tempoMap) && state.tempoMap.length) {
      this.tempoMap = []
      for (const change of state.tempoMap) this.setTempo(change.bpm, change.beat)
    }
    if (state.loop) this.setLoop(state.loop.startBeat, state.loop.endBeat, state.loop.enabled !== false)
    else this.clearLoop()
    if (state.positionBeats !== undefined) this.seek(state.positionBeats)
  }
}

function validateTempo(bpm) {
  if (!Number.isFinite(bpm) || bpm <= 0) throw new RangeError('tempo must be positive')
  return bpm
}
