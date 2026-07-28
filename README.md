# Transmission

Modular Linux VST3 transmission host.

The project is split between a Node.js control plane and a C++ real-time engine. RDF/Turtle projects are parsed into a typed graph, compiled and then supplied to the native engine through a control-rate bridge.

## Implemented foundation

- RDF/Turtle graph parsing, validation, compilation, and persistence
- Modular plugin registry and VST3 bundle discovery
- Native preallocated audio processor and graph contracts
- Deterministic fake audio device for offline testing
- DAW transport with start/stop, seeking, tempo maps, and looping
- Optional Steinberg SDK-backed VST3 metadata inspection
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

The native engine currently provides lifecycle, transport, graph, fake-device, and metadata-inspection contracts. VST3 instance processing, JACK/PipeWire integration, N-API packaging, and the UI are subsequent implementation slices.

To build the optional SDK-backed metadata inspector:

```sh
cmake -S native -B native/build-vst3 \
  -DTRANSMISSION_WITH_VST3=ON \
  -DTRANSMISSION_BUILD_TESTS=OFF \
  -DCMAKE_BUILD_TYPE=Release
cmake --build native/build-vst3 --target transmission_vst3_inspector
```

Project edits should flow through `EngineSession`, which compiles and validates the graph and configures transport state before passing control to the native boundary.
