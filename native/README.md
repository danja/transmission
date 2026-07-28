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
