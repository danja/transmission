import { pathToFileURL } from 'node:url'

const DEFAULT_TEMPO = 140
const BEATS_PER_BAR = 4

const SECTION_DEFINITIONS = [
  ['Distant Battalion', 0, 16, 'A distant fife and field drum approach over the hill.'],
  ['Techno Build', 16, 16, 'The march resolves into an accelerating four-on-the-floor build.'],
  ['Wub Dub Half-Time', 32, 16, 'A half-time drop introduces wobble bass and dub percussion.'],
  ['Driving Acid', 48, 24, 'Four-on-the-floor bass and a repetitive acid figure take control.'],
  ['Industrial Krautrock', 72, 24, 'The groove degrades into hardcore machinery over motorik chords.'],
  ['Fade to Oblivion', 96, 9, 'Rhythm fragments disappear into a long dub-and-noise tail.']
]

export function createGenOmegaArrangement({ tempo = DEFAULT_TEMPO } = {}) {
  if (!Number.isFinite(tempo) || tempo <= 0) {
    throw new RangeError('tempo must be a positive finite number')
  }

  const secondsPerBeat = 60 / tempo
  const seconds = beats => round(beats * secondsPerBeat)
  const sections = SECTION_DEFINITIONS.map(([name, startBar, bars, intent]) => ({
    name,
    startBar,
    bars,
    start: seconds(startBar * BEATS_PER_BAR),
    length: seconds(bars * BEATS_PER_BAR),
    intent
  }))

  const makeClip = (section, notes, name = section) => {
    const definition = sections.find(candidate => candidate.name === section)
    return {
      name,
      section,
      startBeat: definition.startBar * BEATS_PER_BAR,
      lengthBeats: definition.bars * BEATS_PER_BAR,
      start_position: definition.start,
      length: definition.length,
      notes: notes.map(note => ({
        ...note,
        startBeat: note.start,
        durationBeats: note.length,
        start: seconds(note.start),
        length: seconds(note.length)
      }))
    }
  }

  return {
    title: 'Gen Omega',
    tempo,
    timeSignature: [4, 4],
    bars: 105,
    length: seconds(105 * BEATS_PER_BAR),
    tonalCenter: 'D minor / D Dorian',
    sections,
    tracks: [
      {
        name: 'Field Drums',
        role: 'military introduction and dub percussion',
        instrument: 'drumkit.vst3',
        source: 'authored MIDI; DrumGen can provide variations for later capture',
        fx: ['ambo.vst3 (distant space)', 'paunchlad.vst3 (dub throws)'],
        clips: [
          makeClip('Distant Battalion', fieldDrums(16), 'Approaching battalion'),
          makeClip('Wub Dub Half-Time', dubPercussion(16), 'Dub percussion loop'),
          makeClip('Fade to Oblivion', sparseEchoPercussion(9), 'Echo remnants')
        ]
      },
      {
        name: 'Fife',
        role: 'high monophonic battalion melody',
        instrument: 'canticle.vst3',
        source: 'authored MIDI',
        fx: ['ambo.vst3 (long distant reflection)', 'rift.vst3 (exit transition)'],
        clips: [makeClip('Distant Battalion', fifeMelody(16), 'Dorian fife call')]
      },
      {
        name: 'Main Drums',
        role: 'techno, half-time, and hardcore drum progression',
        instrument: 'drumkit.vst3',
        source: 'authored MIDI; Xoxolo/DrumGen can replace individual sections',
        fx: [],
        clips: [
          makeClip('Techno Build', technoBuild(16), 'Escalating build'),
          makeClip('Wub Dub Half-Time', halfTimeDrums(16), 'Half-time drop'),
          makeClip('Driving Acid', fourOnFloor(24), 'Driving four-on-the-floor'),
          makeClip('Industrial Krautrock', industrialDrums(24), 'Hardcore machine'),
          makeClip('Fade to Oblivion', fadeDrums(9), 'Collapsing pulse')
        ]
      },
      {
        name: 'Bass',
        role: 'wobble, driving techno, and industrial pedal bass',
        instrument: 'basilico.vst3',
        source: 'authored MIDI',
        fx: ['e_mix.vst3 (rhythmic gate)', 'paunchlad.vst3 (dub space)'],
        clips: [
          makeClip('Wub Dub Half-Time', wubBass(16), 'Half-time wobble'),
          makeClip('Driving Acid', drivingBass(24), 'Driving D pedal'),
          makeClip('Industrial Krautrock', industrialBass(24), 'Distorted pedal'),
          makeClip('Fade to Oblivion', fadeBass(9), 'Sub decay')
        ]
      },
      {
        name: 'Acid Lead',
        role: 'hypnotic repetitive acid melody',
        instrument: 'basilico.vst3',
        source: 'authored MIDI',
        fx: ['e_mix.vst3 (accent gate)', 'rift.vst3 (industrial mutation)'],
        clips: [
          makeClip('Driving Acid', acidLine(24), 'Dorian acid cycle'),
          makeClip('Industrial Krautrock', acidFragments(24), 'Corroded acid fragments')
        ]
      },
      {
        name: 'Kraut Chords',
        role: 'motorik harmonic base and terminal drone',
        instrument: 'canticle.vst3',
        source: 'authored MIDI',
        fx: ['orchid.vst3 (harmonic bloom)', 'ambo.vst3 (oblivion tail)'],
        clips: [
          makeClip('Industrial Krautrock', krautChords(24), 'Motorik power chords'),
          makeClip('Fade to Oblivion', oblivionChord(9), 'Terminal D minor drone')
        ]
      }
    ],
    productionCues: [
      cue(0, 'Field Drums', 'distance', 0.95, 'Very quiet, filtered, and mostly wet.'),
      cue(12, 'Field Drums', 'distance', 0.45, 'Bring the battalion into focus.'),
      cue(28, 'Main Drums', 'build', 1, 'Open filter and increase snare density.'),
      cue(32, 'Bass', 'wobbleRate', 0.35, 'Start the half-time drop with a slow wobble.'),
      cue(40, 'Bass', 'wobbleRate', 0.75, 'Double the apparent wobble rate.'),
      cue(48, 'Acid Lead', 'filterCutoff', 0.25, 'Introduce the acid line dark.'),
      cue(64, 'Acid Lead', 'filterCutoff', 0.85, 'Peak the acid filter before mutation.'),
      cue(72, 'Main Drums', 'distortion', 0.85, 'Enter the industrial section abruptly.'),
      cue(92, 'Kraut Chords', 'reverbSend', 0.9, 'Flood the last chord cycle.'),
      cue(96, 'Master', 'fade', 1, 'Begin a nine-bar fade to silence.'),
      cue(105, 'Master', 'fade', 0, 'Reach digital silence at exactly three minutes.')
    ],
    limitations: [
      'Transmission persists and schedules this authored MIDI, but does not yet provide a timeline editor.',
      'Transmission cannot yet persist sample-accurate VST3 parameter or bypass automation.',
      'Transmission cannot yet capture or freeze MIDI generator performances as editable clips.'
    ]
  }
}

function note(pitch, start, length, velocity = 96, channel = 0) {
  return { pitch, start, length, velocity, channel }
}

function repeatBars(bars, createBar) {
  return Array.from({ length: bars }, (_, bar) => createBar(bar, bar * 4)).flat()
}

function fieldDrums(bars) {
  return repeatBars(bars, (bar, beat) => {
    const velocity = Math.round(42 + (bar / (bars - 1)) * 42)
    return [
      note(36, beat, 0.2, velocity, 9),
      note(38, beat + 1, 0.18, velocity + 4, 9),
      note(38, beat + 2, 0.18, velocity + 1, 9),
      note(38, beat + 3, 0.18, velocity + 5, 9),
      ...(bar % 4 === 3
        ? [note(45, beat + 3.5, 0.18, velocity, 9), note(43, beat + 3.75, 0.18, velocity + 3, 9)]
        : [])
    ]
  })
}

function fifeMelody(bars) {
  const phrase = [[86, 0, 1], [89, 1.5, 0.5], [91, 2, 1], [89, 3, 0.75],
    [86, 4, 1], [84, 5.5, 0.5], [81, 6, 1.5]]
  return Array.from({ length: bars / 2 }, (_, index) => phrase.map(([pitch, start, length]) =>
    note(pitch + (index >= 6 ? 12 : 0), index * 8 + start, length, 58 + index * 4))).flat()
}

function dubPercussion(bars) {
  return repeatBars(bars, (bar, beat) => [
    note(37, beat + 0.75, 0.12, 62 + (bar % 3) * 5, 9),
    note(42, beat + 1.5, 0.1, 55, 9),
    note(39, beat + 2.75, 0.15, 72, 9),
    ...(bar % 4 === 3 ? [note(46, beat + 3.5, 0.2, 68, 9)] : [])
  ])
}

function sparseEchoPercussion(bars) {
  return repeatBars(bars, (bar, beat) => bar % 2 === 0
    ? [note(39, beat + 2.5, 0.15, Math.max(25, 60 - bar * 4), 9)] : [])
}

function technoBuild(bars) {
  return repeatBars(bars, (bar, beat) => {
    const notes = [0, 1, 2, 3].map(offset => note(36, beat + offset, 0.18, 82 + bar, 9))
    const division = bar < 8 ? 1 : bar < 12 ? 0.5 : 0.25
    for (let offset = 0; offset < 4; offset += division) {
      if (bar >= 4) notes.push(note(38, beat + offset, 0.1, 50 + bar * 3, 9))
    }
    if (bar >= 8) [0.5, 1.5, 2.5, 3.5].forEach(offset =>
      notes.push(note(42, beat + offset, 0.08, 65 + bar, 9)))
    return notes
  })
}

function halfTimeDrums(bars) {
  return repeatBars(bars, (bar, beat) => [
    note(36, beat, 0.2, 116, 9), note(38, beat + 2, 0.2, 112, 9),
    note(42, beat + 0.5, 0.08, 65, 9), note(42, beat + 1.5, 0.08, 61, 9),
    note(42, beat + 2.5, 0.08, 67, 9), note(42, beat + 3.5, 0.08, 61, 9),
    ...(bar % 4 === 3 ? [note(40, beat + 3.5, 0.15, 94, 9)] : [])
  ])
}

function fourOnFloor(bars) {
  return repeatBars(bars, (bar, beat) => [0, 1, 2, 3].flatMap(offset => [
    note(36, beat + offset, 0.16, 112, 9),
    note(42, beat + offset + 0.5, 0.08, 72 + (bar % 4) * 3, 9),
    ...(offset === 1 || offset === 3 ? [note(38, beat + offset, 0.14, 104, 9)] : [])
  ]))
}

function industrialDrums(bars) {
  return repeatBars(bars, (bar, beat) => Array.from({ length: 8 }, (_, step) => [
    note(step % 2 === 0 ? 36 : 41, beat + step * 0.5, 0.1, 105 + (step % 3) * 7, 9),
    ...(step === 2 || step === 6 ? [note(38, beat + step * 0.5, 0.12, 124, 9)] : []),
    ...(bar >= 12 ? [note(42, beat + step * 0.5 + 0.25, 0.06, 82, 9)] : [])
  ]).flat())
}

function fadeDrums(bars) {
  return repeatBars(bars, (bar, beat) => bar < 6
    ? [note(36, beat, 0.18, 105 - bar * 12, 9), note(38, beat + 2, 0.15, 95 - bar * 10, 9)] : [])
}

function wubBass(bars) {
  return repeatBars(bars, (bar, beat) => [
    note(38, beat, 1.5, 112), note(bar % 4 === 3 ? 36 : 41, beat + 2.5, 1, 104)
  ])
}

function drivingBass(bars) {
  const pitches = [38, 38, 41, 38, 36, 38, 43, 41]
  return repeatBars(bars, (bar, beat) => pitches.map((pitch, step) =>
    note(pitch, beat + step * 0.5, 0.38, step % 4 === 0 ? 116 : 92 + (bar % 3) * 4)))
}

function industrialBass(bars) {
  return repeatBars(bars, (bar, beat) => Array.from({ length: 8 }, (_, step) =>
    note(step === 7 && bar % 4 === 3 ? 37 : 26, beat + step * 0.5, 0.42, step % 2 ? 101 : 120)))
}

function fadeBass(bars) {
  return repeatBars(bars, (bar, beat) => bar < 7
    ? [note(26, beat, 3.5, Math.max(30, 105 - bar * 11))] : [])
}

function acidLine(bars) {
  const pitches = [62, 65, 69, 60, 62, 70, 65, 64, 62, 65, 67, 69, 58, 60, 61, 65]
  return repeatBars(bars, (bar, beat) => pitches.map((pitch, step) =>
    note(pitch + (bar % 8 === 7 && step > 11 ? 12 : 0), beat + step * 0.25, 0.2,
      step % 5 === 0 ? 118 : 78 + (step % 4) * 6)))
}

function acidFragments(bars) {
  return repeatBars(bars, (bar, beat) => [0, 1.5, 2.25, 3.5].map((offset, index) =>
    note([74, 70, 65, 61][(index + bar) % 4], beat + offset, 0.18, Math.max(55, 108 - bar * 2))))
}

function krautChords(bars) {
  const roots = [50, 48, 43, 45]
  return repeatBars(bars, (bar, beat) => {
    const root = roots[Math.floor(bar / 2) % roots.length]
    return [root, root + 7, root + 12].map((pitch, index) =>
      note(pitch, beat, 3.75, 82 + index * 5))
  })
}

function oblivionChord() {
  return [note(50, 0, 35.5, 80), note(53, 0, 35.5, 72), note(57, 0, 35.5, 76)]
}

function cue(bar, track, parameter, value, intent) {
  return { bar, track, parameter, value, intent }
}

function round(value) {
  return Number(value.toFixed(9))
}

function parseArgs(argv) {
  let tempo = DEFAULT_TEMPO
  let pretty = true
  for (let index = 0; index < argv.length; index += 1) {
    if (argv[index] === '--compact') pretty = false
    else if (argv[index] === '--tempo') tempo = Number(argv[++index])
    else throw new Error(`Unknown argument: ${argv[index]}`)
  }
  return { tempo, pretty }
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  try {
    const { tempo, pretty } = parseArgs(process.argv.slice(2))
    process.stdout.write(`${JSON.stringify(createGenOmegaArrangement({ tempo }), null, pretty ? 2 : 0)}\n`)
  } catch (error) {
    process.stderr.write(`${error instanceof Error ? error.message : String(error)}\n`)
    process.exitCode = 1
  }
}
