# Transmission architecture

Transmission is a Linux VST3 graph host with a C++ audio engine and a Node.js
project-control layer. RDF Turtle is the canonical project format. The native
engine never parses RDF and no audio buffer crosses into JavaScript.

The source for the accompanying architectural diagram is
[architecture.mermaid](architecture.mermaid).

## System boundaries

The system is divided into four main areas:

1. Project and session services in Node.js own the canonical graph model,
   Turtle parsing and serialization, validation, undo/redo, and atomic project
   file writes.
2. The GTK application owns the current interactive graph canvas, menus,
   transport controls, native editor windows, and an editor-oriented project
   snapshot.
3. The native control layer validates an execution snapshot, constructs
   processors, configures transport and JACK, and manages engine lifecycle.
4. The real-time layer advances transport, moves bounded audio and MIDI blocks,
   invokes VST3 processors, and reports atomic diagnostics.

There are currently two control surfaces over the native engine:

- The GTK executable calls `GraphRuntimeController` directly in the same C++
  process. For project persistence it invokes `scripts/native-ui-project.js` as
  a control-thread subprocess.
- A programmatic Node application can use `ProjectSession`, `EngineSession`,
  `NativeBridge`, and the optional N-API addon. This path does not carry audio
  buffers through Node; it supplies lifecycle and control-rate commands only.

Both paths ultimately construct native processors and run them through
`AudioEngine`.

## Project model and persistence

`src/model/Graph.js` represents nodes, typed audio/MIDI connections, ports,
settings, normalized parameters, opaque plugin state, and editor metadata.
`src/transport/Transport.js` owns the editable tempo map, loop, and project
position. Editor coordinates remain metadata and do not affect graph
execution.

`src/rdf/TransmissionRdf.js` maps this model to and from RDF datasets using the
vocabulary in `src/rdf/Vocabulary.js`. Turtle files preserve:

- node identity, kind, label, plugin path, and port counts;
- port-addressed audio and MIDI connections;
- GTK editor positions;
- tempo and loop settings;
- requested external JACK connections;
- normalized VST3 parameter values;
- base64-encoded VST3 component and controller state.

`ProjectSession` validates by compiling the graph before accepting it. Saves
are written to a temporary file and atomically renamed over the destination.
RDF parsing, serialization, filesystem access, and process execution all stay
on a control thread.

### GTK persistence adapter

The GTK application does not link an RDF library. It captures a `UiProject`,
encodes it with the versioned `UiProjectCodec`, and passes that interchange file
to `scripts/native-ui-project.js`.

On save, the helper decodes the interchange into a Node `Graph` and asks
`ProjectSession` to serialize Turtle. On open, it loads Turtle through
`ProjectSession` and returns the versioned interchange to GTK. Version 2 carries
parameters and opaque state; the decoder remains compatible with version 1.
GTK validates node identifiers, endpoints, and port indices before replacing
the visible project.

Saving or closing first stops audio and captures processor state. This ensures
that VST3 state methods are never called concurrently with audio processing.

## Editing and runtime compilation

The GTK canvas owns mutable editor state: visible nodes, cables, positions,
requested external ports, current parameter values, and cached plugin state.
It never mutates a running `RoutedAudioGraph`.

When Play is pressed:

1. GTK inspects installed VST3 topology and produces a
   `RuntimeGraphSnapshot`.
2. `GraphRuntimeController` asks `GraphRuntimeCompiler` to validate node IDs,
   endpoints, port indices, and audio/MIDI cycles and to normalize duplicate
   connections.
3. A processor factory creates a `Vst3Processor`, `PassThroughProcessor`, or
   `MidiEndpointProcessor` for each node.
4. Opaque plugin state is restored before explicit parameter values.
5. `RoutedAudioGraph::prepare` computes dependency levels and preallocates node
   audio buffers, MIDI storage, channel pointers, and routing data.
6. `AudioEngine` receives the graph, transport configuration, render-ahead
   depth, and processing-thread selection.
7. `JackAudioDevice` registers and activates the configured audio and MIDI
   ports.
8. `JackConnectionManager` applies requested external connections after the
   ports become visible to the JACK server.

Execution-affecting edits stop the runtime and require Play to compile a fresh
graph. Moving a node changes editor metadata only.

The Node control path performs an analogous separation:
`compileGraph` validates the immutable JavaScript model, `EngineSession`
enforces loaded/running lifecycle states, and `NativeBridge` sends the compiled
representation to the N-API addon.

## Native processing graph

`RoutedAudioGraph` is a preallocated directed acyclic graph. Every node owns an
`AudioProcessor`, input/output buffers, fixed-capacity MIDI arrays, and route
metadata.

For each block the graph:

1. clears the preallocated node buffers;
2. copies external audio and MIDI into endpoint nodes;
3. applies queued parameter changes;
4. processes nodes in dependency order;
5. routes MIDI events and mixes port-addressed audio into downstream inputs;
6. sums explicit output nodes into the device outputs;
7. copies bounded external MIDI output events back to the device.

Audio fan-out reads one processor output into multiple routes. Fan-in sums
sources deterministically in graph order. Audio paths with different node
counts still describe the same block; declared plugin latency compensation is
not yet implemented.

## Audio callback and render ahead

`JackAudioDevice` implements the `AudioDevice` interface. JACK supplies buffers
to its callback, which forwards them to `AudioEngine` without allocating.

Transmission has two processing modes:

- With render ahead disabled, the JACK callback advances transport and invokes
  the graph directly. The routed graph is forced to one processing thread.
- With render ahead enabled, the callback copies complete audio/MIDI blocks
  into a preallocated single-producer/single-consumer input queue. A render
  coordinator processes the graph and writes sequence-tagged results to a
  preallocated output queue. The callback releases the result after the
  selected common delay.

Render ahead gives a block a later deadline and absorbs transient scheduling or
plugin CPU spikes. It does not make a graph sustainable when its average
critical-path cost exceeds real time.

Independent nodes in one dependency level can run concurrently. Automatic
thread selection starts from `std::thread::hardware_concurrency()` and caps the
result at the widest dependency level, so it adapts to both the machine and the
graph. Each plugin remains assigned to a stable processing lane. Routes are
merged only after the level completes, preserving deterministic mixing and
avoiding concurrent writes to downstream buffers.

The JACK callback does not call plugin code in render-ahead mode. It also does
not allocate, access files or the network, call JavaScript, log, perform timing
queries, or take the engine control mutex.

## Transport and MIDI

The editable Node transport and the native `TransportClock` have different
roles. Node owns project transport state. Before playback, the control layer
configures the native clock. During playback, the native clock advances by the
exact frame count of each block and publishes the resulting beat position
through diagnostics.

JACK MIDI input is converted into bounded `MidiEvent` records with a frame
offset, port, and up to three bytes of channel-message data. Routed MIDI edges
carry these records between graph nodes. `Vst3Processor` converts supported
note-on and note-off messages to VST3 events and converts plugin output events
back to bounded native MIDI. Broader MIDI mapping and SysEx are not currently
implemented.

Control-originated MIDI and parameter changes use bounded queues. Parameters
are applied at the beginning of a processing block. A full queue produces a
recoverable control error rather than allocating or blocking.

## VST3 hosting and native editors

`Vst3Inspector` loads bundle metadata and flattens VST3 buses into the graph's
port-addressed channel model. `Vst3Processor` owns the processing component,
maps graph channel pointers back to VST3 bus/channel locations, supplies
transport context, and handles parameters, MIDI, and opaque state.

The GTK editor host is deliberately separate from the processing instance. It
embeds the plugin's `IPlugView` in an X11 `GtkSocket`, supplies the VST3 Linux
run-loop interfaces, and installs an `IComponentHandler`. Editor
`performEdit` notifications update GTK's model and are forwarded through the
bounded runtime parameter queue when audio is running.

When an editor opens, cached component/controller state and normalized
parameters are supplied to it. When the editor host closes the instance, its
latest state is returned to the project model. When playback stops, state from
the processing instance is also captured. Non-empty state sections are merged
so controller-only state is not lost when a processor supplies component state
only.

## JACK ports and external routing

`JackAudioDevice` owns the `transmission` JACK client and its audio/MIDI ports.
It validates the sample rate and period during configuration, subscribes to
buffer-size and xrun notifications, and activates only after callbacks and
ports are ready.

`JackConnectionManager` uses a separate control client to enumerate sources and
destinations and to connect requested external ports. Requested connections
are persisted independently from live connection success. Stable port
identities remove generated numeric JACK client suffixes where a unique match
exists, allowing saved PipeWire/JACK routes to survive client renaming.

## Diagnostics and failure handling

Atomic engine diagnostics expose running state, beat position, processed
blocks, MIDI events, underruns, late rendered blocks, queue drops, render-ahead
depth, active processing threads, and average/maximum render time. Per-node
timings identify the slowest processor.

Graph, plugin, device, project, and connection errors are returned on the
control thread and shown by GTK where appropriate. Invalid projects are
rejected before replacing the current editor graph. Plugin construction or
state-restoration failure aborts graph compilation without starting JACK.

## Test seams

The architecture is exercised without requiring an audio device wherever
possible:

- Vitest covers the JavaScript graph, RDF, registry, session, bridge, and
  native-UI helper contracts.
- CTest covers the native engine, processors, graph compiler, routed graph,
  project codec, transport, device contracts, and failure cases.
- `FakeAudioDevice` provides deterministic block processing.
- VST3 probes exercise metadata, offline processing, graph integration, state
  capture/restoration, and plugin chains.
- JACK probes cover live callback lifecycle, MIDI, routing, underruns, and late
  render diagnostics.

## Offline audio export

File → Render Audio captures the editor's current runtime snapshot after
stopping live JACK processing, then hands that immutable snapshot to
`OfflineAudioRenderer` on a worker thread. The renderer compiles through the
same `GraphRuntimeCompiler` used by live playback, prepares fixed-size planar
buffers, advances VST3 process context from beat zero, and takes audio only from
the graph's System Output endpoint. It does not create or connect a JACK
client.

The render worker interleaves complete output blocks into a temporary stereo
IEEE-float WAV and atomically renames the file after the requested bar count is
complete. Cancellation and write failures remove the temporary file. MP3
export uses this WAV as a lossless intermediate and invokes `ffmpeg` only after
graph processing has finished; the temporary WAV and MP3 are removed on
failure. Progress crosses to GTK through atomics and a UI timer, so neither
plugin processing nor file writing touches GTK.

## Node.js surface: current capabilities and future uses

The Node.js surface is usable today as a headless project and engine control
layer. Its strongest current capabilities are project modeling and
persistence:

- create immutable typed graph models with audio and MIDI ports;
- parse and serialize RDF Turtle projects;
- preserve editor metadata, parameters, opaque state, transport, and requested
  device connections in the project model;
- validate connection endpoints, port indices, duplicate cables, and audio
  cycles before compilation;
- manage project revisions with update, undo, and redo;
- save projects atomically and load them without involving the audio thread;
- edit tempo maps, loop ranges, and transport position;
- maintain a plugin descriptor/factory registry for control-plane components.

With the optional N-API addon, Node can also:

- create and dispose a native engine and, in a JACK-enabled build, optionally
  attach it to a JACK device;
- load a compiled port-addressed audio/MIDI graph;
- instantiate VST3 nodes when the addon was built with VST3 support;
- configure tempo and loop state, then start and stop processing;
- enqueue bounded MIDI channel messages and normalized parameter changes;
- read lifecycle, transport, underrun, render-queue, timing, and
  processing-thread diagnostics.

Without a configured JACK device the addon can exercise lifecycle and control
contracts, but no device callback drives audio blocks. `FakeAudioDevice` is
currently a C++ test seam rather than an N-API device option.

This boundary is deliberately control-rate. JavaScript never receives JACK
audio buffers and is never invoked by the audio callback.

Some methods already present on the JavaScript `NativeBridge` describe intended
capabilities rather than current addon exports. In particular, the current
N-API module does not export `scanPlugins`, `savePluginState`, or
`restorePluginState`; calling those wrapper methods reports that the capability
is unavailable. The addon also does not yet restore each node's persisted
parameter/state fields during `loadProject`, although parameters can be sent
after loading. The GTK path currently has the fuller plugin discovery and
state-restoration integration.

Potential future uses of the Node surface include:

- a supported headless Transmission host for scripts, tests, installations
  without GTK, and unattended generative performances;
- a browser, Electron, or other toolkit-based UI using a stable control API
  while the native engine retains all real-time work;
- declarative composition and live-coding tools that generate or transform RDF
  graphs, schedules, parameters, and MIDI;
- plugin indexing, metadata caching, preset management, and project migration
  services;
- OSC, WebSocket, HTTP, hardware-controller, or session-manager gateways, with
  networking confined to the control plane and bounded messages handed to the
  engine;
- remote diagnostics and performance monitoring;
- deterministic offline rendering, batch validation, and CI regression tools
  once the corresponding native control operations are exposed;
- collaborative or version-controlled project tooling that uses RDF identities
  and graph diffs without coupling editor metadata to execution data.

Those extensions should continue to use immutable compiled snapshots for
structural changes and bounded queues for live events. They must not introduce
JavaScript calls, network access, filesystem work, allocation, or
unpredictable locking on the audio callback.

### MCP control adapter

The local MCP server is the first implemented control-plane adapter. It wraps
`TransmissionControlService`, which in turn owns a `ProjectSession` and may own
an `EngineSession`. The adapter itself contains schemas and protocol metadata,
but no graph, persistence, or audio-processing logic.

The stdio server exposes the current project, Turtle representation, and
diagnostics as resources. Its tools create, open, save, and inspect projects;
apply atomic revision-checked graph transactions; configure transport; persist
parameter values; and control an optional native engine. Project path access is
limited to configured roots.

The plugin catalogue follows the same RDF-oriented control-plane approach.
Curated Turtle profiles describe musical behaviour and meaningful signal
semantics. An isolated VST3 inspector contributes technical identity, topology,
and parameter evidence, which is also serialized as Turtle. `PluginCatalogue`
merges those layers without allowing inferred buses to overwrite curated
behaviour—for example, a compatibility-only silent audio bus does not turn a
MIDI generator into a useful audio source. MCP searches load concise catalogue
results on demand instead of placing the entire plugin corpus in server
instructions.

This initial server is headless and does not attach to an already-running GTK
process. The intended next architectural step is a single local control service
that owns the active project and engine. GTK and MCP would then be clients of
that service, avoiding two independent graphs or two processes competing for
JACK and plugin instances.
