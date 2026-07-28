# Transmission

Modular Linux VST3 transmission host.

The project is split between a Node.js control plane and a C++ real-time engine. RDF/Turtle projects are parsed into a typed graph, compiled and then supplied to the native engine through a control-rate bridge.

## Implemented foundation

- RDF/Turtle graph parsing, validation, compilation, and persistence
- Modular plugin registry and VST3 bundle discovery
- Native preallocated audio processor and graph contracts
- Deterministic fake audio device for offline testing
- DAW transport with start/stop, seeking, tempo maps, and looping
- Optional Steinberg SDK-backed VST3 metadata inspection, offline block processing, and graph integration
- Optional JACK audio-device backend with callback-safe port bridging
- JACK MIDI input events with bounded channel-message forwarding
- Node project sessions with undo/redo and a control-plane engine session

## Development

```sh
npm install
npm test
npm run check
cmake -S native -B native/build
cmake --build native/build
ctest --test-dir native/build --output-on-failure
```

The native engine currently provides lifecycle, transport, graph, fake-device, VST3 discovery, metadata inspection, a deterministic VST3 processing probe, and a reusable VST3 processor inside `AudioGraph`. JACK/PipeWire integration, production device hosting, N-API packaging, and the UI are subsequent implementation slices.

The optional JACK backend is built with:

```sh
cmake -S native -B native/build-jack -DTRANSMISSION_WITH_JACK=ON
cmake --build native/build-jack
```

It registers predeclared mono ports, validates the server’s sample rate and period, and forwards JACK buffers directly to the engine callback. Set `AudioDeviceConfig::autoConnect` to `true` to connect physical capture/playback ports during configuration. A running JACK server is required when configuring a `JackAudioDevice` instance.

With a JACK server configured for 48 kHz/1024 frames, run the live lifecycle probe:

```sh
native/build-jack/transmission_jack_engine_probe
```

Add `--auto-connect` when physical hardware ports should be connected automatically.

The automated MIDI smoke test handles timing and port names for you:

```sh
scripts/test-jack-midi.sh
```

To build the optional SDK-backed metadata inspector:

```sh
cmake -S native -B native/build-vst3 \
  -DTRANSMISSION_WITH_VST3=ON \
  -DTRANSMISSION_BUILD_TESTS=OFF \
  -DCMAKE_BUILD_TYPE=Release
cmake --build native/build-vst3 --target transmission_vst3_inspector
```

Then inspect an installed plugin:

```sh
native/build-vst3/transmission_vst3_inspect /path/to/Plugin.vst3
```

To instantiate a real VST3 effect and process a deterministic block:

```sh
native/build-vst3/transmission_vst3_offline /path/to/Plugin.vst3 [frames]
```

The probe reports the selected class, bus channel counts, frame count, and input/output RMS values. It is intended as the executable seam for the later real-time graph host.

To exercise the reusable processor through the native graph:

```sh
native/build-vst3/transmission_vst3_graph_probe /path/to/Plugin.vst3
```

To exercise the complete native engine lifecycle with a VST3 processor and fake device:

```sh
native/build-vst3/transmission_vst3_engine_probe /path/to/Plugin.vst3
```

Project edits should flow through `EngineSession`, which compiles and validates the graph and configures transport state before passing control to the native boundary.
