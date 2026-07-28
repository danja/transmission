# Native engine

This directory contains the real-time engine boundary. The initial implementation is a dependency-free lifecycle scaffold so its ownership and control contracts can be tested before VST3 and JACK/PipeWire integration are introduced.

Build with:

```sh
cmake -S native -B native/build
cmake --build native/build
ctest --test-dir native/build --output-on-failure
```

The audio callback must be added in a separate module. It must not use the control mutex or call into Node.js.

`AudioProcessor` is the first real-time processing contract. Its `process` method receives caller-owned, preallocated channel buffers and is required to be `noexcept` and allocation-free.

The SDK-backed metadata inspector is opt-in because it adds the Steinberg SDK build graph:

```sh
cmake -S native -B native/build-vst3 -DTRANSMISSION_WITH_VST3=ON
cmake --build native/build-vst3
```

Inspection is control-plane work and must never run from the audio callback.

`TransportClock` supports beat-based tempo changes, start/stop, seeking, and looping. Its callback-facing advance result uses fixed-capacity storage; edit the tempo map only from the control side.
