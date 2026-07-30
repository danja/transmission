# Project origins

Transmission combines ideas from two earlier projects:

- [Transmissions](https://github.com/danja/transmissions), a graph-oriented
  pipeline framework whose RDF model influenced Transmission's project format;
- [Downspout](https://github.com/danja/downspout), a VST3 collection emphasizing
  generative algorithms and interoperable musical systems.

Audio processing required a new native, real-time-safe engine rather than the
asynchronous event-processing model used by Transmissions. The resulting split
between a Node.js control plane and C++ audio engine is described in the
[architecture document](architecture.md).
