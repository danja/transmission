import { describe, expect, it } from 'vitest'
import { createGenOmegaArrangement } from '../../scripts/gen-omega-arrangement.js'

describe('Gen Omega arrangement recipe', () => {
  it('defines a contiguous 105-bar, three-minute form at 140 BPM', () => {
    const arrangement = createGenOmegaArrangement()

    expect(arrangement.tempo).toBe(140)
    expect(arrangement.bars).toBe(105)
    expect(arrangement.length).toBe(180)
    expect(arrangement.sections.map(section => section.bars))
      .toEqual([16, 16, 16, 24, 24, 9])

    let nextBar = 0
    let nextSecond = 0
    for (const section of arrangement.sections) {
      expect(section.startBar).toBe(nextBar)
      expect(section.start).toBeCloseTo(nextSecond, 8)
      nextBar += section.bars
      nextSecond += section.length
    }
    expect(nextBar).toBe(105)
    expect(nextSecond).toBeCloseTo(180, 8)
  })

  it('produces deterministic REAPER-compatible clips with valid MIDI notes', () => {
    const first = createGenOmegaArrangement()
    const second = createGenOmegaArrangement()

    expect(second).toEqual(first)
    expect(first.tracks.map(track => track.name)).toEqual([
      'Field Drums', 'Fife', 'Main Drums', 'Bass', 'Acid Lead', 'Kraut Chords'
    ])

    for (const track of first.tracks) {
      expect(track.clips.length).toBeGreaterThan(0)
      for (const clip of track.clips) {
        expect(clip.start_position).toBeGreaterThanOrEqual(0)
        expect(clip.start_position + clip.length).toBeLessThanOrEqual(first.length)
        expect(clip.notes.length).toBeGreaterThan(0)
        for (const note of clip.notes) {
          expect(note.pitch).toBeGreaterThanOrEqual(0)
          expect(note.pitch).toBeLessThanOrEqual(127)
          expect(note.velocity).toBeGreaterThanOrEqual(1)
          expect(note.velocity).toBeLessThanOrEqual(127)
          expect(note.channel).toBeGreaterThanOrEqual(0)
          expect(note.channel).toBeLessThanOrEqual(15)
          expect(note.start).toBeGreaterThanOrEqual(0)
          expect(note.length).toBeGreaterThan(0)
          expect(note.start + note.length).toBeLessThanOrEqual(clip.length)
        }
      }
    }
  })

  it('uses MIDI channel 10 for every authored drum event', () => {
    const arrangement = createGenOmegaArrangement()
    const drumTracks = arrangement.tracks.filter(track => track.name.includes('Drums'))
    const drumNotes = drumTracks.flatMap(track =>
      track.clips.flatMap(clip => clip.notes))

    expect(drumNotes.length).toBeGreaterThan(0)
    expect(new Set(drumNotes.map(note => note.channel))).toEqual(new Set([9]))
  })

  it('rejects invalid tempos and preserves the 105-bar form at other tempos', () => {
    expect(() => createGenOmegaArrangement({ tempo: 0 })).toThrow(RangeError)
    expect(() => createGenOmegaArrangement({ tempo: Number.NaN })).toThrow(RangeError)

    const slower = createGenOmegaArrangement({ tempo: 120 })
    expect(slower.bars).toBe(105)
    expect(slower.length).toBe(210)
  })
})
