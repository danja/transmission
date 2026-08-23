---
layout: default
title: MCP Reference
nav_order: 4
---

# MCP Reference

Transmission exposes its control plane via the [Model Context Protocol](https://modelcontextprotocol.io) and a REST HTTP API. Both share the same underlying state.

**MCP server entry point:** `scripts/transmission-mcp.js`  
**Live server (MCP + HTTP):** `scripts/transmission-live.js`  
**Default HTTP port:** 7878  
**MCP config:** `.mcp.json`

Start the live server:

```sh
node scripts/transmission-live.js
# or
npm run live:start
```

---

## General notes

- Pass the current `revision` to every mutation tool. Get it from `transmission_status` or any project read.
- Use `dryRun: true` on `graph_apply_changes` before committing complex edits.
- Structural changes (adding/removing nodes and connections) require stopped audio.
- Parameter changes apply live when the engine is running.

---

## Resources

Read-only MCP resources accessible via `transmission://` URIs.

| URI | Format | Description |
|-----|--------|-------------|
| `transmission://project` | JSON | Current graph, revision, transport, and status |
| `transmission://project/turtle` | Turtle | Canonical Turtle representation of the project |
| `transmission://diagnostics` | JSON | Engine state, transport, processing metrics |
| `transmission://plugins` | JSON | Discovered VST3 facts merged with curated profiles |
| `transmission://plugins/profiles` | Turtle | Curated behavioural profiles (roles, routing hints) |
| `transmission://plugins/discovered` | Turtle | Raw VST3 discovery evidence |

---

## Tools

### Status and inspection

| Tool | Description |
|------|-------------|
| `transmission_status` | Project revision, dirty state, transport position, engine availability |
| `project_get` | Complete current graph and its revision |
| `transmission_diagnostics` | Engine diagnostics: timing, block counts, underruns, peak levels |
| `peaks_get` | Current left/right output peak levels |
| `node_list` | Compact node inventory: id, type, label, port counts |
| `arrangement_get` | Current arrangement: length, MIDI clips, gain automation lanes |

### Project management

| Tool | Description |
|------|-------------|
| `project_new` | Replace the current project with a validated graph |
| `project_open` | Load a Turtle or JSON project from disk |
| `project_save` | Atomically save the current project |
| `project_capture_midi` | Run the engine, capture MIDI output, write a multi-track SMF (requires native addon) |

### Graph operations

All graph mutations require stopped audio and the current `expectedRevision`.

| Tool | Description |
|------|-------------|
| `graph_apply_changes` | Validate and atomically apply a batch of node and connection operations. Supports `dryRun`. |
| `node_add` | Add a plugin node with optional initial connections |
| `node_remove` | Remove a node and all its connections |
| `connection_add` | Add a single audio or MIDI connection |
| `connection_remove` | Remove a connection |

### Transport

| Tool | Description |
|------|-------------|
| `transport_configure` | Set tempo, loop range, or position (requires stopped audio) |
| `transport_play` | Start audio processing |
| `transport_stop` | Stop audio processing |

### Parameters and arrangement

| Tool | Description |
|------|-------------|
| `parameter_set` | Set a normalised VST3 parameter value; applies live |
| `parameters_set_batch` | Set multiple parameters on one node atomically |
| `arrangement_update` | Replace arrangement length, MIDI clips, and gain lanes |
| `clip_add` | Add a MIDI clip with note events |
| `clip_remove` | Remove a MIDI clip by id |
| `arrangement_render_midi` | Write the current arrangement to a Standard MIDI File |

### Plugin discovery

| Tool | Description |
|------|-------------|
| `plugins_list` | List installed plugins with optional text filter |
| `plugins_search` | Search by name, role, signal types, or vendor |
| `plugin_describe` | Full merged description, CC mappings, recommendations, and cautions for one plugin |
| `plugin_validate_chain` | Check two adjacent plugins for compatible signal flow |
| `plugins_scan` | Rescan VST3 bundles under `~/.vst3` and refresh metadata |

---

## HTTP API

When `transmission-live.js` is running, the same operations are available over HTTP.

### GET endpoints

| Endpoint | Response |
|----------|----------|
| `GET /status` | Revision, dirty flag, transport, engine state |
| `GET /project` | Complete graph (JSON) |
| `GET /plugins` | All discovered plugins with merged profiles |
| `GET /peaks` | `{ peakL, peakR }` — use for silence detection |
| `GET /diagnostics` | Engine state, timing, underrun count |
| `GET /jack-ports` | Available JACK playback/capture ports via `jack_lsp` |

### POST endpoints

| Endpoint | Body | Description |
|----------|------|-------------|
| `POST /graph/apply` | `{ changes, expectedRevision, dryRun? }` | Atomic graph mutation |
| `POST /transport` | `{ action: "play"\|"stop"\|"configure", ... }` | Transport control |
| `POST /parameters` | `{ nodeId, parameters: [{id, normalizedValue}] }` | Set VST3 parameters |
| `POST /projects/open` | `{ path }` | Load a project file |
| `POST /projects/save` | `{ path? }` | Save the current project |
| `POST /projects/new` | `{ graph }` | Replace project with a new graph |
| `POST /plugins/scan` | — | Rescan `~/.vst3` |

### Example: check output levels

```sh
curl http://127.0.0.1:7878/peaks
# {"peakL": 0.42, "peakR": 0.41}
# Both near zero → silence at output; run "peaks" in the console to diagnose.
```

### Example: atomic graph change

```sh
curl -X POST http://127.0.0.1:7878/graph/apply \
  -H 'Content-Type: application/json' \
  -d '{
    "expectedRevision": 12,
    "changes": [
      { "op": "addNode", "id": "reverb", "pluginPath": "/home/danny/.vst3/lightverb.vst3" },
      { "op": "addConnection", "from": "synth", "to": "reverb", "kind": "audio", "fromPort": 0, "toPort": 0 }
    ]
  }'
```
