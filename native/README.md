# Native engine

This directory contains the real-time engine boundary. The default build is dependency-free; the optional Steinberg SDK build adds VST3 discovery, metadata inspection, deterministic single-block processor hosting, and graph integration while JACK/PipeWire and the production device host are developed.

Build with:

```sh
cmake -S native -B native/build
cmake --build native/build
ctest --test-dir native/build --output-on-failure
```

The audio callback must be added in a separate module. It must not use the control mutex or call into Node.js.

`AudioProcessor` is the first real-time processing contract. Its `process` method receives caller-owned, preallocated channel buffers and is required to be `noexcept` and allocation-free.

`RoutedAudioGraph` provides the DAG form used for project execution. Its control side registers node IDs and edges, `prepare()` resolves a topological order and allocates buffers, and the callback performs only buffer mixing and processor calls.

The SDK-backed metadata inspector is opt-in because it adds the Steinberg SDK build graph:

```sh
cmake -S native -B native/build-vst3 -DTRANSMISSION_WITH_VST3=ON
cmake --build native/build-vst3
```

Inspection is control-plane work and must never run from the audio callback.

The resulting `transmission_vst3_inspect` executable accepts one `.vst3` bundle and prints its factory class IDs, names, vendors, and categories.

The SDK fixture can be built and inspected with:

```sh
cmake -S ~/VST_SDK/vst3sdk -B /tmp/transmission-vst3-sdk-build \
  -DCMAKE_BUILD_TYPE=Release \
  -DSMTG_ENABLE_VSTGUI_SUPPORT=OFF \
  -DSMTG_ENABLE_VST3_HOSTING_EXAMPLES=OFF \
  -DSMTG_ENABLE_VST3_PLUGIN_EXAMPLES=ON \
  -DSMTG_CREATE_PLUGIN_LINK=OFF
cmake --build /tmp/transmission-vst3-sdk-build --target again-sample-accurate
native/build-vst3/transmission_vst3_inspect \
  /tmp/transmission-vst3-sdk-build/VST3/Release/again-sample-accurate.vst3
```

The same build provides an actual processor probe:

```sh
native/build-vst3/transmission_vst3_offline \
  /tmp/transmission-vst3-sdk-build/VST3/Release/again-sample-accurate.vst3
```

Expected output includes `inputChannels=2`, `outputChannels=2`, `frames=512`, and non-zero input/output RMS values. The probe owns setup-time allocations and parameter queues; the eventual audio callback must move those objects into a preallocated instance runtime.

The reusable processor can also be exercised through `AudioGraph`:

```sh
native/build-vst3/transmission_vst3_graph_probe \
  /tmp/transmission-vst3-sdk-build/VST3/Release/again-sample-accurate.vst3
```

This reports `graphProcessors=1` and a non-zero output RMS after processing 512 frames.

The engine/device lifecycle probe uses the same processor through `AudioEngine`:

```sh
native/build-vst3/transmission_vst3_engine_probe \
  /tmp/transmission-vst3-sdk-build/VST3/Release/again-sample-accurate.vst3
```

It reports one processed block, zero underruns, and the processed output RMS.

A transport-driven VST3 MIDI-generator/instrument chain can be verified
deterministically with:

```sh
native/build-ui-jack-vst3/transmission_vst3_chain_probe \
  ~/.vst3/drumgen.vst3 ~/.vst3/drumkit.vst3
```

The corresponding live JACK callback probe is
`transmission_vst3_jack_chain_probe`; it requires a JACK server configured for
48 kHz and 1024 frames.

`TransportClock` supports beat-based tempo changes, start/stop, seeking, and looping. Its callback-facing advance result uses fixed-capacity storage; edit the tempo map only from the control side.

The optional JACK device backend is enabled with:

```sh
cmake -S native -B native/build-jack -DTRANSMISSION_WITH_JACK=ON
cmake --build native/build-jack
```

`JackAudioDevice::configure` performs client and port registration, validates JACK’s actual sample rate and process period, and optionally connects physical capture/playback ports when `AudioDeviceConfig::autoConnect` is enabled. Its process callback only obtains JACK-owned buffers and invokes `AudioCallback`; it does not allocate or take the engine control mutex. The configured block size must match JACK’s process period.

For a live lifecycle check using a 48 kHz/1024-frame JACK server:

```sh
native/build-jack/transmission_jack_engine_probe
native/build-jack/transmission_jack_engine_probe --auto-connect
```

The probe runs for three seconds and reports processed blocks and underruns.

JACK configurations create one MIDI input port by default. Channel messages up to three bytes are copied into fixed-size `MidiEvent` values and delivered to `AudioCallback::handleMidi` on the process thread; larger events are ignored until SysEx storage is added.

`Vst3Processor` converts note-on and note-off messages into preallocated VST3 `IEventList` entries for the current audio block. Other MIDI channel messages remain available to the engine callback for later mapping.

The optional N-API addon is built with:

```sh
cmake -S native -B native/build-napi -DTRANSMISSION_WITH_NAPI=ON
cmake --build native/build-napi --target transmission_native
```

It exposes only control-rate calls; it does not pass audio buffers or JavaScript values through the process callback.

When built with `TRANSMISSION_WITH_VST3=ON` as well, `loadProject` constructs the routed native graph from compiled node and connection arrays. A node with `settings.pluginPath` is loaded as a VST3 processor; generic nodes currently use pass-through processing.

When built with `TRANSMISSION_WITH_JACK=ON`, `createEngine({device: 'jack', blockSize: 1024, sampleRate: 48000})` configures the JACK device before the Node session starts audio. MIDI submitted from JavaScript is placed in a bounded native queue and drained by the audio callback.

The repository smoke test automates the live connection and generates note-on/note-off events:

```sh
scripts/test-jack-midi.sh
```
