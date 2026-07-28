# Transmission

Modular Linux VST3 transmission host.

The project is split between a Node.js control plane and a C++ real-time engine. RDF/Turtle projects are parsed into a typed graph, compiled and then supplied to the native engine through a control-rate bridge.

## Development

```sh
npm install
npm test
npm run check
cmake -S native -B native/build
cmake --build native/build
ctest --test-dir native/build --output-on-failure
```

The native engine currently provides lifecycle contracts and tests. VST3 hosting and JACK/PipeWire integration are subsequent implementation slices.
