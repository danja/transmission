# Transmission DAW Implementation Plan

## Current implementation status

The repository now contains the modular Node/C++ foundation, typed graph compiler, RDF/Turtle round trips, project sessions, plugin registry and bundle discovery, allocation-free native audio device abstractions, transport with tempo maps and looping, linear and routed DAG graph execution, SDK-backed VST3 metadata inspection, deterministic offline instance processing, an optional JACK device adapter with rate/period negotiation, physical-port routing, MIDI input forwarding, `AudioEngine` device/graph lifecycle wiring, VST3 input and output note-event conversion, an optional N-API control addon, compiled-graph-to-native routed node construction, bounded real-time VST3 parameter queuing, a Node-controlled JACK configuration path, and a native GTK live graph host with embedded VST3 editors and persistent JACK external-port settings. The VST3 path has been verified against the SDK’s real `again-sample-accurate.vst3` bundle and against a transport-driven `drumgen.vst3 -> drumkit.vst3` chain in both deterministic offline processing and a live dummy-JACK callback. The remaining major runtime work is processor factory coverage beyond pass-through/VST3, broader MIDI mapping/SysEx, persisted project/UI synchronization, plugin state synchronization between processing and editor instances, and plugin browser metadata.

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
4. Implement VST3 discovery and metadata. *(bundle discovery, SDK metadata inspector, and CLI complete)*
5. Implement deterministic offline native processing. *(processor, graph, fake device, transport, SDK-backed VST3 probe, and reusable graph-integrated VST3 processor complete)*
6. Add JACK/PipeWire audio and MIDI I/O. *(optional JACK callback/device adapter, rate/period negotiation, physical-port auto-connect, bounded MIDI input forwarding, engine lifecycle wiring, live probe, automated MIDI smoke test, and VST3 note event conversion complete; broader MIDI mapping, live-server validation in this execution environment, and PipeWire remain)*
7. Implement native graph routing and multi-plugin processing. *(routed DAG, fan-out/fan-in mixing, cycle rejection, engine integration, compiled graph construction, and VST3 instance loading are complete; processor factory coverage and multi-bus routing remain)*
8. Add the N-API bridge and Node session lifecycle. *(control addon, transport/MIDI/diagnostics calls, EngineSession engine creation, compiled graph construction, bounded MIDI and parameter submission, optional JACK device construction, and VST3 parameter application complete; richer device selection and notifications remain)*
9. Build graph editing and plugin browsing. *(native GTK live host complete: typed audio/MIDI ports and colored routes, control-thread snapshot compilation, real JACK-backed Play/Stop, persistent per-channel external connections, visible runtime failures, engine diagnostics transport display, bounded generator-to-instrument MIDI routing, node context menus, node dragging, type-compatible arc creation, modal `~/.vst3` browsing, and double-click native VST3 editor embedding; persisted project synchronization, richer editing commands, MIDI device selection, and plugin browser metadata remain)*
10. Add parameter/state management, diagnostics, packaging, and project/UI synchronization. *(Linux VST3 editor embedding is complete in the native GTK prototype)*

## Testing and acceptance

Node tests cover RDF round trips, model validation, graph compilation, nested transmission flattening, channel/MIDI rules, sessions, bridge lifecycle, and UI graph editing. C++ tests cover plugin lifecycle, offline buffers, MIDI conversion, parameters, channel negotiation, routing, state, queues, and device failure.

The first live-host acceptance path is: load a known VST3 plugin, compile a valid project, process an offline buffer, start JACK/PipeWire processing, route MIDI between plugins, and report graph, plugin, device, or routing failures without destabilizing the engine. The current implementation reaches this live graph milestone. Persisted UI/project synchronization, processing/editor state sharing, richer parameter editing, and broader device/MIDI mapping remain.

## Native GTK live-host completion plan

The GTK graph must become a client of the native control plane rather than a
second audio implementation. The work is split into these bounded stages:

1. Define an immutable native graph snapshot containing execution node data,
   typed connections, and system endpoints. Keep canvas positions and widgets
   out of this snapshot.
2. Add a control-thread graph compiler that validates node ownership,
   connection endpoints, duplicate edges, and cycles before constructing a
   `RoutedAudioGraph`. Processor loading and graph buffer preparation remain
   outside the JACK callback.
3. Extend routed execution with fixed-capacity MIDI buffers and typed MIDI
   edges. VST3 output note events must be converted back to bounded native MIDI
   events so generator-to-instrument routes such as `drumgen -> drumkit` work.
4. Supply tempo and musical position to VST3 processors for every block without
   allocating. Support VST3 instruments with no audio input bus.
5. Add a runtime controller that configures `JackAudioDevice` from the active
   JACK server, compiles the current snapshot, loads it into `AudioEngine`, and
   owns start/stop/rebuild sequencing.
6. Make GTK editing mutate only editor/model state. Starting playback compiles
   that state; execution-affecting edits stop the old runtime and require a new
   start. Node dragging remains editor metadata and does not rebuild audio.
7. Persist desired system input/output selections independently from live JACK
   connection success. Apply them after the engine registers its ports and
   surface failures in the window instead of silently discarding them.
8. Verify compiler valid/invalid/factory-failure cases, deterministic routed
   MIDI generation, native configure/build/CTest, and a live JACK/VST3 smoke
   path using `drumgen.vst3 -> drumkit.vst3 -> system output`.

Acceptance requires the Play button to start the native JACK callback, visible
transport position to come from `AudioEngine` diagnostics, `transmission`
audio/MIDI ports to exist while running, the Output dialog to reopen with the
last requested selections, and all configuration or plugin failures to remain
recoverable and visible.

Status: implemented and verified. The deterministic `drumgen -> drumkit` probe
produces nonzero audio, and the final JACK callback and system-output routing
probe processed 99 blocks with zero underruns.

## Assumptions

- Linux and VST3 are the v1 target.
- JACK/PipeWire is the initial audio backend.
- Plugin processing and embedded Linux plugin editors are included in the first live-host milestone.
- The native engine consumes a compiled representation; it does not parse RDF.
- The old event-driven runtime is architectural reference material, not a runtime dependency.
