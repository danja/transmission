# Example: Generative Beat with Conductor + PCB

A transport-driven patch with evolving structure and automated mixing.

## Plugin graph

```
Conductor ──(ControlMidi)──────────────────────────────┐
                                                        ▼
DrumGen ──(DrumMidi)──► DrumKit ──(Audio)──► T-Mix ◄──┤
BassGen ──(BassMidi)──► Basilico ─(Audio)──► T-Mix    │
MelGen  ──(MelodyMidi)─► Canticle ─(Audio)──► T-Mix   │
                                                        │
Mixgen ──(ControlMidi)─────────────────────────────────┘
       ──(ControlMidi)──► Loopdelay ◄──(Audio)── T-Mix
                          Loopdelay ─(Audio)──► Lightverb
                                     Lightverb ─(Audio)──► Guardian
Mixgen ──(Audio passthrough)──► (anywhere in audio chain)
```

## Key wiring notes

- Conductor → DrumGen: set `conductor_ch` on DrumGen to Conductor's channel
- Conductor → BassGen: set `conductor_ch` on BassGen
- Conductor → Harmonic-Atlas (optional): set `conductor_ch`
- Mixgen MIDI out fans to T-Mix, Loopdelay, and Lightverb (or chain serially)
- T-Mix sums the three instrument strips; audio flows out → Loopdelay → Lightverb → Guardian

## MCP authoring notes

```
plugins: conductor, drumgen, drumkit, bassgen, basilico, melgen, canticle, t-mix, mixgen, loopdelay, lightverb, guardian
```

Before wiring: inspect port counts on all plugins with `transmission_vst3_inspect`.
DrumKit expects `DrumMidi` on port 0; Basilico expects `BassMidi` on port 0.
T-Mix has 8 audio inputs (one per strip) and ControlMidi input.
Loopdelay and Lightverb each have 1 audio stereo in + ControlMidi in.
