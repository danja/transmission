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
- Reopen recent projects from **File → Open Recent** (last 5 files); the most recently used project opens automatically on launch.
- Save complete patches as readable RDF Turtle (`.ttl`) files.
- Render a patch offline to WAV or MP3.
- Auto-arrange the visible graph with **View → Autolayout**, which wraps
  columns to fit the window width.
- Ask an LLM through MCP to discover plugins, compare their behaviour, design
  compatible chains, edit projects, and validate the resulting graph — while
  the GTK application is running.

## Requirements

- Linux with a JACK-compatible audio server (JACK2 or PipeWire)
- C++20 compiler, CMake 3.20+, Node.js 20+
- GTK 3, JACK, and libcurl development headers
- The Steinberg VST3 SDK
- `ffmpeg` for MP3 export (optional)

On Debian or Ubuntu:

```sh
sudo apt install \
  git build-essential cmake pkg-config \
  libgtk-3-dev libjack-jackd2-dev libcurl4-openssl-dev \
  nodejs npm ffmpeg
```

## Quick start

```sh
git clone https://github.com/danja/transmission.git
cd transmission
npm install
./build.sh
```

`./build.sh` runs the JavaScript tests, builds the native engine and tools,
and compiles the GTK/JACK/VST3/curl application. The finished binary is at
`native/build-ui-jack-vst3/transmission_graph_ui`.

## Running the GTK application

Start a JACK server or PipeWire, then:

```sh
./transmission
```

The `./transmission` launcher sets the X11 GTK backend, which is required for
native VST3 editor embedding.

## Basic workflow

1. Right-click the graph background to add a VST3 plugin or MIDI endpoint.
   New nodes are placed at the cursor position.
2. Drag between matching audio or MIDI ports to connect nodes.
3. Double-click a plugin to open its native editor.
4. Double-click System Input or System Output to select JACK connections.
5. Set tempo and loop length, then press **Play**.
6. Use **View → Autolayout** to automatically arrange nodes into a layered
   left-to-right layout that wraps to fit the window.
7. Use **File → Save** to store the graph as Turtle.
8. Use **File → Render Audio** to export a chosen number of bars.

## Built-in diagnostics

The GTK application includes a console (bottom of the window) that accepts
commands for inspecting the running system:

| Command | What it shows |
|---------|---------------|
| `lsp` | All available JACK input/output ports |
| `connections` | What `transmission:out_1/2` are currently wired to |
| `peaks` | Output peak levels; flags silence if both are near zero |
| `status` | Runtime state, configured port names, last connection error |
| `diag` | Engine block counts, timing, and last connection error |
| `reconnect` | Retry JACK port connections without restarting audio |

If a project loads but produces no audio, the typical sequence is: `peaks` to
confirm silence, `connections` to see that outputs are unwired, `lsp` to find
the correct port names, then edit the project's `systemOutputConnections` and
`reconnect`.

## Generative control through MCP

Transmission has two MCP modes:

### Live mode (recommended)

`./transmission` starts the live HTTP control plane automatically and connects
the GTK window to it. The MCP server, the GTK editor, and any other client all
share the same authoritative project state — changes from any side are
reflected everywhere within 500 ms.

```sh
# Start the GTK application (launches the live server automatically)
./transmission

# MCP client configuration — point at the running server
node scripts/transmission-mcp.js --live
```

The live server runs at `http://localhost:7878` by default (configurable in
`config.ttl`). When connected, the GTK title bar shows `[live]`.

Register the MCP server with Claude Code:

```sh
claude mcp add transmission -- node "$PWD/scripts/transmission-mcp.js" --live
```

Or add `.mcp.json` to the project root (created automatically by Claude Code
on first use from this directory):

```json
{
  "mcpServers": {
    "transmission": {
      "command": "node",
      "args": ["/path/to/transmission/scripts/transmission-mcp.js", "--live"]
    }
  }
}
```

Use `-s user` to register it globally across all projects, or `-s local` (the
default) to keep it to the current directory.

### Project-only mode

Runs without a live server or audio device. Suitable for offline project
editing, plugin exploration, and CI.

```sh
node scripts/transmission-mcp.js --project-root "$PWD"
```

Register with Claude Code:

```json
{
  "mcpServers": {
    "transmission": {
      "command": "node",
      "args": [
        "/path/to/transmission/scripts/transmission-mcp.js",
        "--project-root", "/path/to/projects"
      ]
    }
  }
}
```

Project-only mode can inspect, edit, validate, open, and save Turtle projects
and search the installed plugin catalogue, but cannot run audio.

## Live server HTTP API

The live server exposes a REST API at `http://localhost:7878` (configurable in
`config.ttl`). All project operations accept and return RDF Turtle.

| Method | Path | Description |
|--------|------|-------------|
| `GET` | `/status` | Project revision, dirty flag, transport state (Turtle) |
| `GET` | `/project` | Full project description (JSON) |
| `GET` | `/graph` | Full project as Turtle |
| `GET` | `/plugins` | Plugin catalogue (JSON) |
| `GET` | `/peaks` | Current output peak levels L/R (JSON) |
| `GET` | `/diagnostics` | Engine stats, transport, and output peaks (JSON) |
| `GET` | `/jack-ports` | Available JACK playback/capture ports via `jack_lsp` (JSON) |
| `POST` | `/graph/changes` | Apply node/connection operations |
| `POST` | `/transport/play` | Start audio engine |
| `POST` | `/transport/stop` | Stop audio engine |
| `POST` | `/transport/configure` | Set tempo, loop, position |
| `POST` | `/parameters/:node/:id` | Set normalized VST3 parameter |
| `POST` | `/projects/open` | Load project from disk |
| `POST` | `/projects/save` | Save project to disk |
| `POST` | `/projects/new` | Replace with new graph |
| `POST` | `/plugins/scan` | Rescan VST3 bundles |

POST bodies use `Content-Type: text/turtle` with `trn:` action classes defined
in [`vocabs/actions.ttl`](vocabs/actions.ttl).

## Configuration

### GTK application settings

The GTK UI stores per-user settings in `~/.config/transmission/config.ttl`
(created automatically on first save). Currently it holds the VST3 plugin
search paths — see [Plugin catalogue](#plugin-catalogue) below.

The file format is a simple tab-delimited record per setting:

```
PLUGIN_PATH	~/
PLUGIN_PATH	/usr/lib/vst3
```

### Live server settings

Copy `config.defaults.ttl` to `config.ttl` and edit as needed:

```turtle
@prefix trn: <http://purl.org/stuff/transmissions/> .

[] a trn:ServerConfig ;
    trn:port 7878 ;
    trn:bindAddress "127.0.0.1" ;
    trn:allowedRoots ( "." ) ;
    trn:defaultOutputConnections ( "UMC404HD 192k:playback_FL" "UMC404HD 192k:playback_FR" ) .
```

`defaultOutputConnections` sets the JACK ports new projects connect to. Use the
suffix-free form (e.g. `"UMC404HD 192k:playback_FL"` rather than
`"UMC404HD 192k-89:playback_FL"`) so that fuzzy matching resolves the correct
port regardless of the session-assigned number suffix. Run `jack_lsp | grep
playback` to find the right names for your hardware.

The live server URL can also be overridden per-session:

```sh
node scripts/transmission-live.js --port 8080
TRANSMISSION_LIVE_URL=http://localhost:8080 ./transmission
```

## Plugin catalogue

Transmission scans VST3 bundles recursively under the directories configured in
**Settings → Plugin Path…** and merges discovered metadata with curated
behavioural profiles. The default search path is `~/` (the whole home
directory). Change it via the menu item, which opens an editable list (one
directory per line); clicking **Rescan** shows the count of bundles found
without closing the dialog. The setting is saved to
`~/.config/transmission/config.ttl` and applied on every launch. Profiles declare musical roles,
signal types, dependencies, genre hints, and chain recommendations in
[`profiles/downspout.ttl`](profiles/downspout.ttl).

The vocabulary for profile terms is in [`vocabs/profile.ttl`](vocabs/profile.ttl).

Example MCP queries:

> Search the Transmission catalogue for installed MIDI drum generators.
> Compare their behaviour, choose a compatible instrument, and validate the
> proposed chain.

> Create a slowly evolving generative patch, connect it to System Output, and
> save it as RDF Turtle.

## Documentation

- [Installation, building, MCP setup, and troubleshooting](docs/installation.md)
- [Architecture](docs/architecture.md)
- [RDF plugin profiles](docs/plugin-profiles.md)
- [Open work](TODO.md)

Transmission is under active development. Project files are intended to remain
portable and inspectable, but interfaces may still evolve.
