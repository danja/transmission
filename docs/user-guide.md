---
layout: default
title: User Guide
nav_order: 3
---

# User Guide

## Starting Transmission

```sh
./transmission
```

This launches the GTK graph editor. The launcher sets `GDK_BACKEND=x11` and `TRANSMISSION_ROOT`, and suppresses the auto-spawned live server (run `transmission-live.js` separately if needed).

To start the live HTTP/MCP server alongside:

```sh
node scripts/transmission-live.js
```

---

## Graph editing

The graph editor shows nodes as boxes and connections as directed edges.

| Action | How |
|--------|-----|
| Add a node | Right-click on the canvas → select plugin |
| Move a node | Drag its title bar |
| Connect two nodes | Drag from an output port to an input port |
| Remove a connection | Right-click the connection line |
| Remove a node | Right-click the node → Remove |
| Open plugin editor | Double-click the node |

Port colours indicate signal type: **orange** = MIDI, **blue** = audio.

Connections are validated against real port counts read from each VST3 binary. A connection to an out-of-range port is silently rejected; check port counts with:

```sh
native/build-ui-jack-vst3/transmission_vst3_inspect ~/.vst3/<name>.vst3 \
  | grep -E 'audioInputs|audioOutputs|midiInputs|midiOutputs'
```

---

## Transport

The toolbar provides **Play**, **Stop**, and **BPM** controls. Changes to tempo or loop range require audio to be stopped first.

---

## Console commands

Type commands into the console text entry at the bottom of the window.

| Command | Output |
|---------|--------|
| `lsp` | All available JACK input/output ports |
| `connections` | Current JACK output connections |
| `peaks` | Current output peak levels |
| `status` | Runtime state and last connection error |
| `diag` | Full engine diagnostics: timing, block counts, underruns |
| `reconnect` | Retry JACK connections without restarting audio |

---

## Audio setup

Transmission routes its output to JACK. JACK port names that include a numeric suffix change each session; use the suffix-free form so fuzzy matching resolves them automatically:

```
Good: "UMC404HD 192k:playback_FL"
Bad:  "system:playback_1"
```

Configure output connections in `~/.config/transmission/config.ttl`:

```turtle
:config :defaultOutputConnections (
    "UMC404HD 192k:playback_FL"
    "UMC404HD 192k:playback_FR"
) .
```

---

## Saving and loading projects

Projects are Turtle files (`*.ttl`) in the `projects/` directory.

| Action | How |
|--------|-----|
| Save | File → Save (or MCP `project_save`) |
| Open | File → Open (or MCP `project_open`) |
| New | File → New (or MCP `project_new`) |

---

## Offline rendering

Export a project to WAV without a running JACK server:

```sh
node scripts/probe-project.js projects/<name>.ttl
```

For full offline export:

```sh
node scripts/offline-render.js projects/<name>.ttl output.wav
```

Convert to MP3 with ffmpeg:

```sh
ffmpeg -i output.wav -q:a 2 output.mp3
```

---

## Configuration

**GTK editor:** `~/.config/transmission/config.ttl`  
**Live server:** `config.ttl` in the project root (see `config.defaults.ttl` for all options)

Key live-server settings:

```turtle
:config :port 7878 ;
        :bindAddress "127.0.0.1" ;
        :renderAheadBlocks 24 ;
        :processingThreads 4 .
```

---

## Diagnosing audio problems

1. Run the headless probe first — confirms the graph initialises and plugins produce signal without JACK:

   ```sh
   node scripts/probe-project.js projects/<name>.ttl
   ```

   Look for `AUDIO node=guardian totalRms=...` at the bottom. If the probe fails, a plugin likely has `audioOutputs 0` in the TTL — add the correct `:audioOutputs N` declaration.

2. Check JACK output: `jack_lsp | grep playback`

3. Use the `peaks` console command — `(silence — check connections)` means no signal at the output.

4. Run `diag` for full engine timing and underrun counts.

See [Architecture → Diagnostics](architecture#diagnostics-and-failure-handling) for more detail.

---

## MCP usage

When `transmission-live.js` is running, any MCP client can connect and control the graph. See the [MCP Reference](mcp-reference) for the full tool and API listing.

Quick example — list installed plugins and add a node:

```sh
# Via HTTP
curl http://127.0.0.1:7878/plugins | jq '.[] | .name'

# Via MCP (Claude or other client)
# tools: node_add, connection_add, graph_apply_changes, transport_play
```
