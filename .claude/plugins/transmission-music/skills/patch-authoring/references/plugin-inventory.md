# Downspout Plugin Inventory

Source of truth: `/home/danny/github/transmission/profiles/downspout.ttl`
Vocabulary: `/home/danny/github/transmission/vocabs/profile.ttl`
Individual profiles: `/home/danny/github/downspout/plugins/<name>/profile.ttl`

## MIDI Generators

| Plugin | Bundle | Produces | Accepts | Requires | Notes |
|---|---|---|---|---|---|
| BassGen | `bassgen.vst3` | BassMidi, Midi | Midi, ControlMidi | HostTransport | Styles: Straight, Reel, Waltz, Jig, Slip Jig, Jazz, Fugue, Moroder; `conductor_ch` param |
| Ground | `ground.vst3` | BassMidi, Midi | ControlMidi | HostTransport | Long-form bass (Dub, Jazz); arc-tension; `conductor_ch` |
| DrumGen | `drumgen.vst3` | DrumMidi, Midi | ControlMidi | HostTransport | Meter-aware (Breakbeat, Amen, Jungle, Hip Hop, Jazz, Rock, Dub); audio out is silent (by design) |
| MelGen | `melgen.vst3` | MelodyMidi, Midi | Midi | HostTransport | Phrase contour + question/answer structure |
| Polymeter | `polymeter.vst3` | DrumMidi, Midi | — | HostTransport | Four-lane Euclidean; coprime lengths, rotations, probabilities, ratchets |
| Xoxolo | `xoxolo.vst3` | DrumMidi, Midi | — | HostTransport | x0x-style, 11 lanes, up to 32 steps; recommendedBefore DrumKit |
| Sidecar | `sidecar.vst3` | MelodyMidi, Midi | Midi | HostTransport | Phrase player; deterministic local or coordinator mode |
| Lifeform | `lifeform.vst3` | MelodyMidi, DrumMidi, Midi | Midi | HostTransport, Launchpad | Conway Game of Life → MIDI |
| Luma | `luma.vst3` | MultiPartMidi, Midi | Midi | HostTransport, Launchpad | Launchpad cells → bass/chord/melody/drum agents |
| Harmonic-Atlas | `harmonic-atlas.vst3` | HarmonyMidi, Midi | Midi, ControlMidi | HostTransport | Voice-led harmony (tonal, modal, neo-Riemannian); `conductor_ch`; recommendedBefore Canticle |
| TuneyVst | `tuney-vst.vst3` | MelodyMidi, Midi, Audio | Midi | — | Text-to-music; configurable alphabets, scales, microtonal tunings |
| Mosaic | `mosaic.vst3` | Audio | Midi, ControlMidi | HostTransport | Four-slot WAV sampler with autonomous triggering |

## MIDI Processors

| Plugin | Bundle | Produces | Accepts | Requires | Notes |
|---|---|---|---|---|---|
| ArpGen | `arpgen.vst3` | MelodyMidi, Midi | Midi | HostTransport | Chord-capture + scale-derived arpeggiator; recommendedBefore Canticle |
| Cadence | `cadence.vst3` | HarmonyMidi, Midi | Midi | HostTransport | MIDI harmonizer + comping; recommendedBefore Canticle |
| Counterpointer | `counterpointer.vst3` | MelodyMidi, Midi | Midi | — | Learns MIDI, emits counter-melody; recommendedBefore Canticle |
| M-Mix | `m-mix.vst3` | Midi | Midi | HostTransport | MIDI gate: probabilistic + Euclidean blocks |
| Mnemosyne | `mnemosyne.vst3` | MelodyMidi, Midi | MelodyMidi, Midi | HostTransport | Phrase memory with transforms; recommendedBefore Canticle |
| GremlinDriver | `gremlin-driver.vst3` | Midi | Midi | HostTransport | Modulation + action sequencer; companion to Gremlin |
| Oracle | `oracle.vst3` | ControlMidi, MelodyMidi, Audio, Midi | Audio, Midi | — | Extracts audio/MIDI features → mapped CCs; feedback guard enabled; recommendedAfter Resonance-Garden |

## Controllers

| Plugin | Bundle | Produces | Accepts | Requires | Notes |
|---|---|---|---|---|---|
| Conductor | `conductor.vst3` | ControlMidi, Midi | — | HostTransport | Bar-aligned sections (Intro/Develop/Break/Reprise/Coda); emits Scene/Density/Energy/Mutation/Reset CCs on channels 20–24; feeds BassGen, DrumGen, Ground, Harmonic-Atlas |
| Drift | `drift.vst3` | Audio, ControlMidi, Midi | Audio | HostTransport | Four-lane CC modulator (LFO, S&H, random-walk, chaos, envelope follower); audio transparent |
| Mixgen | `mixgen.vst3` | Audio, ControlMidi | Audio | HostTransport | PCB producer; emits CC 19–33; feeds T-Mix, Loopdelay, Lightverb; audio transparent |

## Instruments

| Plugin | Bundle | Produces | Accepts | Requires | Notes |
|---|---|---|---|---|---|
| Basilico | `basilico.vst3` | Audio | BassMidi, Midi | — | Monophonic bass (Upright, Electric, Dub, Acid, Industrial) |
| Canticle | `canticle.vst3` | Audio | MelodyMidi, HarmonyMidi, Midi | — | Twelve-voice (keys, reed, pad, pluck, glass); only 1 real MIDI input |
| Floozy | `floozy.vst3` | Audio | Midi | — | Eight-voice hybrid physical/modulation synth |
| Gremlin | `gremlin.vst3` | Audio | Midi | — | Chaotic glitch; scenes, macros, actions; companion to GremlinDriver |
| DrumKit | `drumkit.vst3` | Audio | DrumMidi, Midi | — | Synth drums + natural sounds; recommendedAfter DrumGen |
| Campione | `campione.vst3` | Audio | Audio, Midi | — | Zone-based sampler; zones/params as Turtle RDF patches; MCP server on port 7220 |
| Syrinx | `syrinx.vst3` | Audio | Midi | — | Polyphonic avian vocal synth (10 birds); recommendedAfter MelGen |

## Audio Effects

| Plugin | Bundle | Produces | Accepts | Requires | Notes |
|---|---|---|---|---|---|
| Ambo | `ambo.vst3` | Audio | Audio | — | Multi-module ambient (time, spectral, tape, shimmer, delay, drive, feedback) |
| E-Mix | `e-mix.vst3` | Audio | Audio | HostTransport | Deterministic Euclidean stereo gate |
| Gater | `gater.vst3` | Audio | Audio, Midi | — | MIDI-controlled stereo switcher (even/odd note parity) |
| Loopdelay | `loopdelay.vst3` | Audio | Audio, ControlMidi | HostTransport | Stereo delay+loop (20–4000 ms, BBT-synced); CC 30/31; recommendedAfter T-Mix, Before Lightverb |
| Lightverb | `lightverb.vst3` | Audio | Audio, ControlMidi | — | Minimal 4-line FDN reverb; CC 32/33; recommendedAfter Loopdelay, Before Guardian |
| Orchid | `orchid.vst3` | Audio | Audio | HostTransport | Voiced freeze via autocorrelation + grid-sync loops |
| Orbit | `orbit.vst3` | Audio | Audio | HostTransport | Stereo motion (orbit, pendulum, random-walk, figure-eight); recommendedBefore Guardian |
| P-Mix | `p-mix.vst3` | Audio | Audio | HostTransport | Probabilistic stereo gate with rhythmic dropouts |
| Rift | `rift.vst3` | Audio | Audio | HostTransport | Buffer disruptor (chop, stutter, reverse, skip, smear, pitch-slip); caution in dense arrangements |
| Resonance-Garden | `resonance-garden.vst3` | Audio | Audio, Midi, HarmonyMidi | — | Eight-voice resonator bank; ALWAYS place Guardian downstream (feedback risk) |
| PaunchLad | `paunchlad.vst3` | Audio | Audio, Midi | Launchpad | Launchpad dub effect (throws, splashes, chops, freezes) |

## Utilities / Mixer

| Plugin | Bundle | Produces | Accepts | Notes |
|---|---|---|---|---|
| T-Mix | `t-mix.vst3` | Audio | Audio | Eight-input mono-strip mixer; accepts ControlMidi (CC 20–27 via PCB) |
| Guardian | `guardian.vst3` | Audio | Audio | Output safety: DC removal, look-ahead limiting, true-peak, soft clip; always last in chain |
