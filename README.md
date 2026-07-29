# Transmission

Modular Linux VST3 transmission host.

This is primarily intended for generative plugins such as those of [Downspout](https://danja.github.io/downspout/)

The project is split between a Node.js control plane and a C++ real-time engine. RDF/Turtle projects are parsed into a typed graph, compiled and then supplied to the native engine through a control-rate bridge.

## Implemented foundation

- RDF/Turtle graph parsing, validation, compilation, and persistence
- Modular plugin registry and VST3 bundle discovery
- Native preallocated audio processor and graph contracts
- Native routed DAG graph with fan-out/fan-in mixing and cycle rejection
- Deterministic fake audio device for offline testing
- DAW transport with start/stop, seeking, tempo maps, and looping
- Optional Steinberg SDK-backed VST3 metadata inspection, offline block processing, and graph integration
- Optional JACK audio-device backend with callback-safe port bridging
- JACK MIDI input events with bounded channel-message forwarding
- VST3 note-on/note-off event conversion
- Optional Node N-API control addon
- Optional native GTK graph editor with system input/output nodes
- Node project sessions with undo/redo and a control-plane engine session

## Development

Download the [Steinberg VST 3 SDK](https://www.steinberg.net/developers/vstsdk/)
and extract it into `~/VST_SDK`. The build expects the SDK source tree at
`~/VST_SDK/vst3sdk`.

```sh
npm install
npm test
npm run check
cmake -S native -B native/build
cmake --build native/build
ctest --test-dir native/build --output-on-failure
```

The complete local build, including JavaScript checks/tests, native CTest, JACK tools, and the GTK/JACK/VST3 UI, is available as:

```sh
./build.sh
./transmission
```

The native engine currently provides lifecycle, transport, linear and routed graph execution, fake-device, VST3 discovery, metadata inspection, a deterministic VST3 processing probe, a reusable VST3 processor inside `AudioGraph`, and an optional N-API control addon. The VST3-enabled addon can construct routed processors directly from compiled node settings. The native UI includes a modular graph canvas, VST3 browsing/editor embedding, and JACK-backed external connection management for system I/O nodes.

Build and smoke-test the optional Node addon:

```sh
npm run native:configure:napi
npm run native:build:napi
node -e "const n=require('./native/build-napi/transmission_native.node'); n.createEngine(); n.loadProject({}); n.configureTransport({sampleRate:48000,tempoMap:[{beat:0,bpm:120}]}); console.log(n.getDiagnostics()); n.disposeEngine()"
```

The addon exposes control-rate lifecycle, transport, MIDI, and diagnostics calls. Audio buffers and real-time callbacks stay in native code.

To try the first native graph UI slice:

```sh
cmake -S native -B native/build-ui -DTRANSMISSION_WITH_GTK_UI=ON
cmake --build native/build-ui --target transmission_graph_ui
native/build-ui/transmission_graph_ui
```

The native canvas renders typed audio and MIDI topology, compiles execution data through a control-thread graph compiler, starts the VST3 graph on JACK from the Play button, preserves and applies external system-port choices, and reports runtime failures in the window. VST3 MIDI output is routed through bounded native buffers, so generator-to-instrument chains execute without callback allocation. It also discovers bundles from `~/.vst3`, opens native plugin editors, provides node context menus, supports dragging nodes, and creates type-compatible arcs.

Right-clicking the canvas also offers Add MIDI Input and Add MIDI Output.
Each action lists the compatible JACK MIDI ports, creates a typed graph
endpoint, and persists the selected external port. MIDI endpoints use dedicated
bounded JACK buffers and never contribute audio to the graph.

The File menu provides New, Open, Save, Save As, and Quit, with the usual
keyboard shortcuts. Transmission projects use RDF Turtle (`.ttl`) as their
canonical format. Saving and loading preserve plugin bundle paths, typed graph
connections and ports, editor positions, tempo/loop settings, and requested
JACK input/output connections. GTK exchanges a small validated snapshot with
the existing Node project session, which performs RDF parsing and atomic file
writes on the control thread; the native audio callback never parses RDF or
accesses the filesystem.

Build the VST3-enabled UI to open native plugin editors by double-clicking plugin nodes:

```sh
cmake -S native -B native/build-ui-vst3 \
  -DTRANSMISSION_WITH_GTK_UI=ON -DTRANSMISSION_WITH_VST3=ON \
  -DTRANSMISSION_BUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build native/build-ui-vst3 --target transmission_graph_ui
native/build-ui-vst3/transmission_graph_ui
```

For the GTK UI with native VST3 editors and JACK external-port management:

```sh
cmake -S native -B native/build-ui-jack-vst3 \
  -DTRANSMISSION_WITH_GTK_UI=ON -DTRANSMISSION_WITH_JACK=ON \
  -DTRANSMISSION_WITH_VST3=ON -DTRANSMISSION_BUILD_TESTS=OFF \
  -DCMAKE_BUILD_TYPE=Release
cmake --build native/build-ui-jack-vst3 --target transmission_graph_ui
native/build-ui-jack-vst3/transmission_graph_ui
```

The System Input and System Output dialogs enumerate JACK ports on demand and apply connections to `transmission:in_N` and `transmission:out_N`. The Transmission engine must already be running in the same JACK server for routes to apply.

On Linux the editor host embeds the VST3 `IPlugView` through X11 and supplies the SDK run-loop interface using GTK timers. Double-clicking system nodes has no editor action.
The `./transmission` launcher selects GTK's X11 backend so native plugin editors
can be embedded when the desktop session itself uses Wayland.

To build the addon with VST3 node construction enabled:

```sh
cmake -S native -B native/build-napi-vst3 \
  -DTRANSMISSION_WITH_NAPI=ON -DTRANSMISSION_WITH_VST3=ON
cmake --build native/build-napi-vst3 --target transmission_native
```

Compiled nodes with `settings.pluginPath` are instantiated as VST3 processors; other nodes currently use native pass-through processors until their processor factories are registered.

Stopped-engine parameter routing can be smoke-tested against a VST3 bundle:

```sh
scripts/test-napi-vst3-parameter.sh /path/to/Plugin.vst3
```

`setParameter(nodeId, parameterId, normalizedValue, sampleOffset)` accepts numeric or string parameter IDs. Changes are submitted through a bounded native queue and applied at the start of the next audio block; updates are rejected only when the queue is full or the target processor has no parameter support.

For a Node-controlled JACK engine, enable JACK as well and pass `{ device: 'jack', blockSize: 1024, sampleRate: 48000 }` to `createEngine`:

```sh
cmake -S native -B native/build-napi-jack \
  -DTRANSMISSION_WITH_NAPI=ON -DTRANSMISSION_WITH_JACK=ON
cmake --build native/build-napi-jack --target transmission_native
```

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
