# Example: Ambient / Textural Patch

A patch built around non-repeating generative sources processed through layered effects.

## Plugin graph

```
Lifeform ──(MelodyMidi)──────────────────────────────► Syrinx ──(Audio)──┐
MelGen   ──(MelodyMidi)──► Counterpointer ─(MelodyMidi)► Canticle ─(Audio)┤
                                                                           ▼
                                                                    Resonance-Garden ─(Audio)──┐
                                                                                               ▼
                                                             Ambo ─(Audio)──► Orbit ─(Audio)──► Guardian
```

## Why these choices

- **Lifeform** provides cellular-automata pattern material — non-repeating and structurally unpredictable
- **MelGen + Counterpointer** adds a responsive counter-voice to the melodic content
- **Syrinx** (avian vocal synth) gives distinctive non-keyboard timbre for Lifeform
- **Canticle** (pad/glass/reed) works well with Counterpointer's MelodyMidi
- **Resonance-Garden** converts both audio streams into pitched resonator output — bridges timbres
- **Ambo** adds shimmer, spectral processing, tape saturation
- **Orbit** adds spatial motion before output
- **Guardian** mandatory after Resonance-Garden (feedback accumulation risk)

## Variations

- Replace Lifeform with TuneyVst + text input for language-derived melody
- Add Drift as a four-lane CC modulator feeding Ambo's parameters
- Add Oracle after Resonance-Garden to extract features and re-inject as control

## Cautions

- Resonance-Garden can accumulate feedback — Guardian must be downstream
- Counterpointer has no HostTransport requirement but works better with transport running
- Syrinx and Canticle both accept generic Midi; either can use Lifeform or MelGen output
