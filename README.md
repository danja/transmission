# Transmission

**A Generative Audio Workstation for Linux.**

Transmission is a graph-based environment for building musical systems from
VST3 plugins. Instead of arranging every note on a timeline, you connect MIDI
generators, instruments, effects, mixers, hardware ports, and system audio into
patches that can evolve while the transport runs.

It is especially well suited to generative plugins such as
[Downspout](https://danja.github.io/downspout/), while remaining a general
Linux VST3 host.

## What you can do

- Build typed audio and MIDI graphs by connecting visible nodes.
- Use autonomous, probabilistic, and audio-reactive VST3 plugins.
- Open plugins' native editors and hear parameter changes during playback.
- Connect JACK or PipeWire-JACK audio and MIDI ports.
- Save complete patches as readable RDF Turtle (`.ttl`) files.
- Render a patch offline to WAV or MP3.
- Ask an LLM through MCP to discover plugins, compare their behaviour, design
  compatible chains, edit projects, and validate the resulting graph.

## Quick start

Transmission is currently built from source. Follow the
[installation guide](docs/installation.md) for system packages, the Steinberg
VST3 SDK, audio setup, and MCP registration.

With the prerequisites installed:

```sh
npm install
./build.sh
./transmission
```

Transmission discovers VST3 bundles installed in `~/.vst3`.

## Basic workflow

1. Right-click the graph background to add a VST3 plugin or MIDI endpoint.
2. Drag between matching audio or MIDI ports to connect nodes.
3. Double-click a plugin to open its native editor.
4. Double-click System Input or System Output to select JACK connections.
5. Set tempo and loop length, then press **Play**.
6. Use **File → Save** to store the graph as Turtle.
7. Use **File → Render Audio** to export a chosen number of bars.

WAV rendering is built in. MP3 rendering requires `ffmpeg`.

Example projects are available in [`patches/`](patches/), including a drum and
bass system, an ensemble, and a long-form vibratone patch.

## Generative control through MCP

Transmission includes an MCP server and an RDF-backed plugin catalogue. This
lets an MCP client reason about musical behaviour rather than relying only on
plugin names and bus counts.

For example:

> Search the Transmission catalogue for installed MIDI drum generators.
> Compare their behaviour, choose a compatible instrument, and validate the
> proposed chain.

> Create a slowly evolving generative patch, connect it to System Output, and
> save it as RDF Turtle.

The current MCP server can inspect, create, validate, open, and save projects;
apply revision-checked graph changes; configure transport; control an optional
native engine; read diagnostics; and search plugin profiles. It does not yet
attach to an already-running GTK window.

See [MCP setup](docs/installation.md#mcp-setup) and the
[plugin catalogue guide](docs/plugin-profiles.md).

## Documentation

- [Installation, building, MCP setup, and troubleshooting](docs/installation.md)
- [Architecture](docs/architecture.md) and
  [Mermaid diagram](docs/architecture.mermaid)
- [RDF plugin profiles](docs/plugin-profiles.md)
- [Implementation plan and status](docs/plan.md)
- [Open work](docs/todo.md)

Transmission is under active development. Project files are intended to remain
portable and inspectable, but interfaces may still evolve.
