# Transmission DAW Implementation Plan

## Current implementation status

The repository now contains the modular Node/C++ foundation, typed graph compiler, RDF/Turtle round trips, project sessions, plugin registry and bundle discovery, allocation-free native audio graph/device abstractions, transport with tempo maps and looping, and an optional SDK-backed VST3 metadata inspector. The remaining major runtime work is VST3 instance processing, JACK/PipeWire I/O, N-API packaging, UI integration, and plugin editor embedding.

## Summary

Transmission is being rebuilt as a modular Linux digital audio workstation. Node.js owns project control, RDF/Turtle persistence, graph editing, and the user interface. A small C++ N-API bridge owns VST3 hosting and JACK/PipeWire real-time processing.

The reference `transmissions` project supplies useful concepts—transmissions, processors, connectors, nested graphs, and factory registration—but its mutable message queues and asynchronous `EventEmitter` execution are not suitable for a real-time audio callback.

## Architecture

### Node.js control plane

Keep these concerns independent:

- `rdf`: Turtle parsing, namespaces, validation, and serialization.
- `model`: transmissions, processors, connectors, ports, parameters, and device settings.
- `compiler`: convert RDF into a validated immutable runtime graph.
- `registry`: plugin and processor factories.
- `session`: project lifecycle, persistence, and undo/redo.
- `bridge`: the narrow native control API.
- `ui`: graph editor, plugin browser, mixer, transport, and diagnostics.
- `cli`: headless development and inspection commands.

RDF parsing and graph compilation must never run on the audio thread.

### Native engine

The C++ layer owns VST3 module discovery, plugin lifecycle, bus configuration, audio/MIDI routing, parameter updates, state, device callbacks, and real-time diagnostics. The callback uses preallocated buffers and bounded lock-free queues. It must not allocate, perform I/O, acquire unpredictable locks, or call JavaScript.

The native engine receives a compiled graph rather than RDF. Nested transmissions may be flattened during compilation.

### Transport

Transport is shared between the project/session layer and native processing. It supports start, stop, seek, tempo changes scheduled at musical beats, and bounded looping. The Node model owns editable project state; the native `TransportClock` advances per audio block and returns fixed-capacity timing segments so callback execution does not allocate.

### Persisted model

RDF/Turtle remains canonical. The vocabulary covers projects, transmissions, plugin instances, audio/MIDI ports, connections, bus layouts, parameters, automation, devices, plugin state, and editor metadata. Editor layout properties remain separate from execution properties.

## Native bridge API

The bridge exposes control-rate operations only:

- `createEngine(options)`
- `scanPlugins(paths)`
- `loadProject(compiledGraph)`
- `configureTransport(transportState)`
- `startAudio()` / `stopAudio()`
- `setParameter(nodeId, parameterId, value, sampleOffset?)`
- `sendMidi(nodeId, event)`
- `getPluginMetadata(nodeId)`
- `savePluginState(nodeId)` / `restorePluginState(nodeId, state)`
- `getDiagnostics()`
- `disposeEngine()`

Native notifications are limited to lifecycle, plugin errors, underruns, device changes, MIDI monitoring, and performance statistics.

## Delivery phases

1. Establish Node, CMake, CTest, Vitest, declarations, and CI conventions.
2. Define the RDF vocabulary and typed JavaScript model.
3. Implement RDF loading, validation, compilation, and round-trip serialization.
4. Implement VST3 discovery and metadata. *(metadata inspector complete; instance hosting remains)*
5. Implement deterministic offline native processing. *(processor, graph, fake device, and transport foundations complete)*
6. Add JACK/PipeWire audio and MIDI I/O.
7. Implement native graph routing and multi-plugin processing.
8. Add the N-API bridge and Node session lifecycle.
9. Build graph editing and plugin browsing.
10. Add Linux plugin-editor embedding, parameter/state management, diagnostics, and packaging.

## Testing and acceptance

Node tests cover RDF round trips, model validation, graph compilation, nested transmission flattening, channel/MIDI rules, sessions, bridge lifecycle, and UI graph editing. C++ tests cover plugin lifecycle, offline buffers, MIDI conversion, parameters, channel negotiation, routing, state, queues, and device failure.

The first live-host acceptance path is: load a known VST3 plugin, compile a valid RDF project, process an offline buffer, start JACK/PipeWire processing, send MIDI, change a parameter while running, save/reload state, and report invalid RDF or audio underruns without destabilizing the engine. The current implementation reaches graph compilation, fake-device processing, transport timing, project persistence, and VST3 metadata inspection; live plugin processing and device I/O are still pending.

## Assumptions

- Linux and VST3 are the v1 target.
- JACK/PipeWire is the initial audio backend.
- Plugin processing and embedded Linux plugin editors are planned for the first live-host milestone.
- The native engine consumes a compiled representation; it does not parse RDF.
- The old event-driven runtime is architectural reference material, not a runtime dependency.
