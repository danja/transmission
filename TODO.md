# TODO

## Status indicator

On the right-hand side of the bar containing the Play button it would be useful to have status indicators/buttons for Jack and MCP server

## Live generative DJ via MCP

A near-term goal is to have Claude act as a DJ, using /home/danny/github/transmission as a DAW, loading and running generative plugins from /home/danny/github/downspout together with effects from /home/danny/github/valis Both transmission and valis will need to have suitable mcp tool cover.

In the process we can also work out a way of expressing the DJ set list in Turtle/RDF, so potentially a runner could fire the MCP commands over HTTP without the language model having to be present at runtime. The vocabulary should have quite general-purpose foundations so it could potentially be used by a real DJ spinning disks.
(This material should be saved in the Transmission repo).

### Live MCP setup prerequisites

For `transport_play` and audio control to work via MCP from a Claude session:

1. Start JACK (`jackd -d alsa -r 48000 -p 1024 &`) or enable **Settings > JACK Startup** in the GUI so it starts automatically.
2. Start Transmission: `build-ui-jack-vst3/transmission_graph_ui`
3. Enable the live server: **Settings > MCP Server** in the GUI. This launches `transmission-live.js` with `--native-addon build-napi-jack-vst3/transmission_native.node --jack --auto-connect`.
4. The Claude session must use the transmission MCP server with `--live` (connects to `http://localhost:7878`). This is configured in `~/.claude.json` for the valis project — restart the Claude session after enabling the live server.

## hardcore-techno-160 patch — remaining gaps

- `:xoxolo` pattern all-zero — intentional until programmed.
- Gremlin DSP is expensive (~12 ms/block probe average). Enable render-ahead
  before using this patch live; all other nodes remain at previous cost.

## Engine features

- Suspend schedule-only instrument processors outside their authored activity
  window while preserving a bounded post-note tail.
- Add persisted VST3 parameter, bypass, and send automation with bounded
  sample-offset delivery to the native engine.
- Add a deterministic capture/freeze path for MIDI generator output.

## MCP Live — Phase 3

- GTK full two-way sync: send every in-editor change (add node, connect, drag) to live server
  as `trn:ChangeSet` POST so MCP always sees the latest graph without waiting for a save.
- SSE push (`GET /events`): server endpoint implemented — emits `{revision, generation, filePath}`
  on every state change. GTK still polls; connecting GTK to SSE requires adding a streaming
  CURL handler in `native_graph_ui_main.cpp` to replace the 500 ms `/status` poll.

## Recurring — check periodically

- Remove completed tasks from this file.
- Check MISTAKES.md for systematic problems; promote recurring issues to CLAUDE.md.
- If an issue in MISTAKES.md is fully resolved, remove it.
- For new material, check test coverage.
- Ensure README.md and docs are up-to-date.
