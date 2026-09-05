# TODO

A systematic problem is that some patches require more resources than available in real time with the current buffering setup. Explore how this aspect could be improved. Note that streaming patches to wav works nicely.

## Feature : scopes

Add built-in modules Oscilloscope & Spectrum analyzer, loaded like the Output built-ins as required. They should display while running in the main window, like the level meters in the output built-in.

## Parameter control

Right now we can load the generative plugins from Downspout into transmission but they all carry the default parameters. In the agent-as-DJ scenario, the agent should be able to modify the parameters over MCP. It would be inconveient to add a MCP server to every plugin, but maybe a common midi interface that is loaded as a plugin or built-in might allow this kind of control?

## Bugs

- `projectDefinitionToTurtle` in `src/http/TransmissionHttpClient.js` only serializes node IDs into the `:pipe` list — it drops node types, settings, ports, and connections. The server's `parseNewProject` then fails with "Graph node X type is required". Fix: replace the minimal hand-rolled Turtle with the existing `TransmissionRdf.js` serializer (the function is async, so the caller can await it).

- JUCE assertion failure in `juce_Messaging_linux.cpp:87` observed when hosting Valis inside Transmission. Likely triggered by a JUCE message thread operation happening off the expected thread. Needs a repro and investigation.

## Live generative DJ via MCP

Claude acts as a DJ via MCP, loading and playing generative patches from
`projects/patches/` and effects from Valis. The set list vocabulary and a
runner are now in place; next steps are live parameter control and Valis
integration.

Done:
- `vocabs/djset.ttl` — vocabulary for DJ set lists (DJSet, Cue, Transition, ParameterChange)
- `projects/setlists/rise-to-techno.ttl` — sample set list, 54→160 BPM arc
- `scripts/dj-runner.js` — autonomous set list runner over the live HTTP API
- MCP live session confirmed working: dub-reggae-birdsong loaded and playing

Remaining:
- Live parameter control: describe plugin parameters, apply changes during playback
- Valis effects integration: load a Valis patch as an effect insert in a DJ chain
- Crossfade transition implementation in dj-runner.js (requires mixer gain params)
- xoxolo pattern programming for hardcore-techno-160

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
