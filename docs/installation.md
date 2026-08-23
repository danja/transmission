---
layout: default
title: Installation
nav_order: 2
---

# Installing and building Transmission

Transmission is currently distributed as source and targets Linux desktops
with VST3 and JACK-compatible audio. The normal build produces the GTK graph
editor, native VST3 hosting, JACK audio/MIDI support, offline rendering tools,
and the Node.js control plane.

Prebuilt Ubuntu x86-64 archives may also be attached to tagged
[GitHub Releases](https://github.com/danja/transmission/releases). Each archive
contains runtime instructions and a SHA-256 checksum.

## Requirements

- A C++20 compiler and standard build tools
- CMake 3.20 or newer
- Node.js 20 or newer with npm
- GTK 3 development files
- JACK development files; PipeWire's JACK compatibility layer is supported
- The Steinberg VST3 SDK
- `ffmpeg` for MP3 export (WAV export does not require it)

On Debian or Ubuntu, the usual build packages are:

```sh
sudo apt update
sudo apt install \
  git build-essential cmake pkg-config \
  libgtk-3-dev libjack-jackd2-dev \
  nodejs npm ffmpeg
```

Your distribution may provide JACK through JACK2 or PipeWire. Transmission
only requires that the JACK client API and a working JACK-compatible server are
available. If the distribution's Node.js is older than version 20, install a
current Node.js release before running `npm install`.

## Steinberg VST3 SDK

Download the SDK from the
[Steinberg VST3 developer page](https://www.steinberg.net/developers/vstsdk/)
and extract it so this file exists:

```text
~/VST_SDK/vst3sdk/CMakeLists.txt
```

The standard `build.sh` script uses that location. For a manual CMake build,
another SDK location can be supplied with:

```sh
-DVST3_SDK_ROOT=/path/to/vst3sdk
```

Review Steinberg's SDK licence and redistribution requirements before
packaging or distributing binaries.

## Install VST3 plugins

Transmission scans the standard per-user VST3 directory:

```text
~/.vst3
```

Each plugin should appear there as a `.vst3` bundle, for example:

```text
~/.vst3/drumgen.vst3
```

The [Downspout](https://danja.github.io/downspout/) collection provides
generators, instruments, and effects designed for generative systems and has
curated behavioural metadata in Transmission's plugin catalogue.

## Build and launch

Get the source, then build from the repository root:

```sh
git clone https://github.com/danja/transmission.git
cd transmission
npm install
./build.sh
./transmission
```

`build.sh` runs the JavaScript checks and tests, builds and tests the native
engine, builds JACK tools, and creates the GTK/JACK/VST3 application at:

```text
native/build-ui-jack-vst3/transmission_graph_ui
```

The `./transmission` launcher selects GTK's X11 backend. This is necessary
because native VST3 editor embedding currently uses X11, including when the
desktop session itself runs under Wayland.

## Audio configuration

Start a JACK server or PipeWire with JACK compatibility before playing a
project. In Transmission:

1. Double-click **System Output**.
2. Select the desired playback ports for the left and right channels.
3. Add and configure System Input or MIDI Input/Output nodes only when the
   patch needs external signals.
4. Press **Play**.

Requested port selections are saved in the Turtle project even if the external
device is temporarily unavailable.

## MIDI controllers

To control Gain/Pan or VST3 parameters from a device such as an Akai MIDImix:

1. Right-click the canvas, choose **Add MIDI Input…**, and select the
   controller's JACK MIDI source.
2. Draw a magenta MIDI cable from that input to each Gain/Pan or VST3 node you
   want to control.
3. Right-click a target node and choose **MIDI mappings…**.
4. Select an automatable parameter, MIDI channel (or any channel), and CC
   number, then choose **Add mapping** and **Apply**.
5. Press **Play** after changing the graph or mappings.

Mapped CC values use the full controller range for the target parameter. By
default the mapped event is consumed so the same movement does not also invoke
a plugin's own MIDI behavior. Clear **Consume mapped CC** when the plugin
should receive it too. Unmapped events always pass through, so T-Mix's native
CC assignments continue to work without host mappings. Controller learn and
live hardware-to-widget feedback are not currently implemented; enter the CC
number shown by the controller's configuration or a MIDI monitor.

Settings → Audio shows the active JACK/PipeWire period, render-ahead setting,
processing-thread selection, xruns, and graph timing. The default automatic
thread count adapts to the machine and graph.

## Rendering audio

Use **File → Render Audio** or `Ctrl+Shift+R`. Choose the number of bars and
sample rate, then save as:

- `.wav` for stereo 32-bit floating-point WAV;
- `.mp3` for MP3 encoded by `ffmpeg` and `libmp3lame`.

Rendering starts at beat zero, follows the current tempo and loop settings, and
runs without JACK. External audio and MIDI inputs are silent during an offline
render, while autonomous plugin generators run normally.

## MCP setup

Transmission has two MCP modes: **live** (recommended, shares state with the
running GTK window) and **project-only** (no audio device needed).

### Live mode

The live server is a persistent Node.js process that runs the control plane
and exposes an HTTP REST API at `localhost:7878`. Both the GTK application and
the MCP client connect to it, so all three share one authoritative project.

Start the live server (with JACK audio):

```sh
node scripts/transmission-live.js --jack --auto-connect
```

Or use the npm shortcut:

```sh
npm run live:start:jack
```

Start the GTK window pointing at it:

```sh
TRANSMISSION_LIVE_URL=http://localhost:7878 ./transmission
```

The title bar shows `[live]` when connected. Saving from the UI automatically
syncs the server. External edits from MCP are detected every 500 ms and logged
to the console window.

Start MCP in live mode (stdio transport, for Claude Code or Codex):

```sh
node scripts/transmission-mcp.js --live
```

Or with a non-default server URL:

```sh
node scripts/transmission-mcp.js --live http://localhost:8080
```

Register with Claude Code (`~/.claude/settings.json`):

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

Register with Codex:

```sh
codex mcp add transmission -- \
  node "$PWD/scripts/transmission-mcp.js" --live
```

### Project-only mode

Runs the control plane inline over stdio — no live server or audio device
needed. Suitable for offline project editing, plugin exploration, and CI.

```sh
node scripts/transmission-mcp.js --project-root "$PWD"
```

Or the npm shortcut:

```sh
npm run mcp:start -- --project-root "$PWD"
```

Run the protocol smoke test:

```sh
npm run mcp:smoke
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

Project access is restricted to configured roots; repeat `--project-root` to
allow additional directories. Graph mutations require the current revision;
complex edits should use `dryRun` validation first.

### MCP with a native audio engine (project-only)

To drive audio directly from MCP without the GTK window, build the N-API addon:

```sh
cmake -S native -B native/build-napi-jack-vst3 \
  -DTRANSMISSION_WITH_NAPI=ON \
  -DTRANSMISSION_WITH_JACK=ON \
  -DTRANSMISSION_WITH_VST3=ON \
  -DVST3_SDK_ROOT="$HOME/VST_SDK/vst3sdk"
cmake --build native/build-napi-jack-vst3 \
  --target transmission_native --parallel
```

Start MCP with that addon:

```sh
node scripts/transmission-mcp.js \
  --project-root "$PWD" \
  --native-addon native/build-napi-jack-vst3/transmission_native.node \
  --jack --auto-connect
```

### Live server configuration

Copy `config.defaults.ttl` to `config.ttl` and edit the port or allowed roots:

```turtle
@prefix trn: <http://purl.org/stuff/transmissions/> .

[] a trn:ServerConfig ;
    trn:port 7878 ;
    trn:bindAddress "127.0.0.1" ;
    trn:allowedRoots ( "." ) .
```

Command-line flags (`--port`, `--bind`, `--project-root`) override the file.

## Developer builds and tests

Run the JavaScript checks:

```sh
npm run check
npm test
```

Build and test the dependency-light native engine:

```sh
cmake -S native -B native/build \
  -DTRANSMISSION_BUILD_TESTS=ON
cmake --build native/build --parallel
ctest --test-dir native/build --output-on-failure
```

Build only the VST3 inspection and offline-processing tools:

```sh
cmake -S native -B native/build-vst3 \
  -DTRANSMISSION_WITH_VST3=ON \
  -DTRANSMISSION_BUILD_TESTS=OFF \
  -DVST3_SDK_ROOT="$HOME/VST_SDK/vst3sdk"
cmake --build native/build-vst3 --parallel
```

Useful commands from that build include:

```sh
native/build-vst3/transmission_vst3_inspect /path/to/Plugin.vst3
native/build-vst3/transmission_vst3_offline /path/to/Plugin.vst3
```

The project probe loads the same interchange model as the desktop UI and
diagnoses a patch without JACK or GTK:

```sh
cmake --build native/build-ui-jack-vst3 \
  --target transmission_vst3_project_probe
npm run probe:project -- \
  patches/crystal-healing-vibratones.ttl --seconds 30
```

It reports MIDI activity, per-node audio windows, block timing, and the point
at which a signal becomes silent. Add `--realtime` to exercise the
render-ahead worker path at wall-clock speed.

### JACK and VST3 smoke tests

Build the standalone JACK tools with:

```sh
cmake -S native -B native/build-jack \
  -DTRANSMISSION_WITH_JACK=ON
cmake --build native/build-jack --parallel
```

With a running JACK-compatible server, the following commands exercise the
device lifecycle and MIDI path:

```sh
native/build-jack/transmission_jack_engine_probe
scripts/test-jack-midi.sh
```

For a VST3-enabled N-API build, stopped-engine parameter routing can be checked
against a specific bundle:

```sh
scripts/test-napi-vst3-parameter.sh /path/to/Plugin.vst3
```

## Troubleshooting

### The build cannot find the VST3 SDK

Confirm that `~/VST_SDK/vst3sdk/CMakeLists.txt` exists. If the SDK is elsewhere,
run CMake manually and set `VST3_SDK_ROOT`.

### Plugins are missing from the browser or MCP catalogue

Confirm that each bundle is below `~/.vst3` and ends in `.vst3`. Rebuild the
VST3-enabled targets after changing the SDK or host build.

### The graph plays but produces no sound

Open System Output and apply valid JACK playback destinations. Confirm the
JACK/PipeWire server is running and inspect Settings → Audio for xruns or
render-queue failures.

### A native plugin editor does not open

Launch with `./transmission`, which selects the X11 GTK backend required by the
current editor host. A pure Wayland `GtkSocket` cannot embed the VST3 X11 view.

### MP3 export fails

Confirm that `ffmpeg` is on `PATH` and that its build includes the
`libmp3lame` encoder:

```sh
ffmpeg -encoders | grep libmp3lame
```

WAV rendering remains available without `ffmpeg`.

### MCP can edit projects but cannot play them

In project-only mode that is expected. Switch to live mode: start
`transmission-live.js --jack --auto-connect` and run MCP with `--live`.
Alternatively, build the N-API/JACK/VST3 addon and pass `--native-addon`.

### The GTK window shows `[offline]` instead of `[live]`

The window could not reach the live server at startup or the server stopped.
Check that `transmission-live.js` is running and that `TRANSMISSION_LIVE_URL`
matches its address. The window retries every 500 ms and updates to `[live]`
as soon as the server responds.

### The live server build does not include HTTP support

`build.sh` prints `Live server support: disabled` if libcurl was not found at
configure time. Install the development headers and rebuild:

```sh
sudo apt install libcurl4-openssl-dev
cmake -S native -B native/build-ui-jack-vst3 \
  -DTRANSMISSION_WITH_GTK_UI=ON \
  -DTRANSMISSION_WITH_JACK=ON \
  -DTRANSMISSION_WITH_VST3=ON \
  -DVST3_SDK_ROOT="$HOME/VST_SDK/vst3sdk" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build native/build-ui-jack-vst3 --target transmission_graph_ui --parallel
```
