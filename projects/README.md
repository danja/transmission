# Example and test patches

Transmission projects are RDF Turtle files. Open them with **File → Open** and
double-click System Output if the saved JACK destinations do not match the
current machine.

## Generative-suite tests

The following patches exercise the ten plugins added to Downspout's generative
suite:

| Patch | New plugins exercised | Expected result |
| --- | --- | --- |
| `generative-suite-harmony.ttl` | Conductor, Harmonic Atlas, Orbit, Guardian | Conductor section notes change Harmonic Atlas roots; Canticle plays evolving chords through spatial motion and output protection. |
| `generative-suite-rhythm-memory.ttl` | Polymeter, Mnemosyne, Guardian | Polymeter directly drives DrumKit and also feeds Mnemosyne, whose transformed output plays Canticle; T-Mix combines both branches. |
| `generative-suite-reactive.ttl` | Harmonic Atlas, Oracle, Resonance Garden, Drift, Orbit, Guardian | Oracle analyses Canticle and passes/responds with MIDI to tune Resonance Garden; Drift emits CC to an intentionally disconnected monitor while passing resonant audio onward. |
| `generative-suite-mosaic.ttl` | Drift, Mosaic, Guardian | Load one or more WAV files in Mosaic's editor, then hear autonomous slices mixed with Drift's optional live-input pass-through. |

The Mosaic patch is intentionally silent until at least one supported PCM16 or
float32 WAV file has been loaded. Save the project after loading samples to
persist Mosaic's sample paths in plugin state.

Every autonomous audio chain ends in Guardian. The patches use
`system:playback_1` and `system:playback_2` as portable defaults, except Mosaic,
which also requests the standard capture ports for its optional live input.

## Performance patch

`hardcore-melodic-techno.ttl` is a 158 BPM performance patch with programmed
four-on-the-floor drums, a driven D-minor techno bass, high-tension evolving
chords, and a sixteenth-note plucked arpeggio. T-Mix balances the four
stereo branches before Guardian protects the final output.

`kraurock-jazz.ttl` combines a 126 BPM motorik drum grid with a jazz/Dorian
bass ostinato, evolving electric keys, and a learned contrapuntal reed line.
The four stereo branches meet at T-Mix and Guardian.

Transmission routes VST3 note, pressure, and legacy MIDI CC output through
bounded MIDI edges. Incoming CC, channel pressure, and pitch bend use each
plugin's cached VST3 MIDI-controller assignments, preserving channel and sample
offset. The disconnected Drift monitor therefore reports activity without
requiring an external MIDI destination.
