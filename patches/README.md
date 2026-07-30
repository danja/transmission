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

The current Transmission VST3 bridge routes note-on and note-off output but not
VST3 legacy MIDI CC output. Consequently Drift's CC monitor remains at zero,
and the CC portions of Conductor and Oracle are not yet visible downstream.
Their audio paths, section notes, passed notes, and response notes remain
testable. This limitation is tracked in `docs/todo.md`.
