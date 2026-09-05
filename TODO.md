# TODO

## Live generative DJ via MCP

A near-term goal is to have Claude act as a DJ, using /home/danny/github/transmission as a DAW, loading and running generative plugins from /home/danny/github/downspout together with effects from /home/danny/github/valis Both transmission and valis will need to have suitable mcp tool cover.

In the process we can also work out a way of expressing the DJ set list in Turtle/RDF, so potentially a runner could fire the MCP commands over HTTP without the language model having to be present at runtime. The vocabulary should have quite general-purpose foundations so it could potentially be used by a real DJ spinning disks.
(This material should be saved in the Transmission repo).

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
