# TODO

## MCP parity with the GTK UI — audited, no open gaps

First-pass audit (2026-09-24) of `TransmissionMcpServer.js` against the GTK
UI's feature set. Node/connection creation is generic enough
(`type`/`settings` free-form) to cover Gain, AudioClip, MidiClip, and
JigdawPlugin kinds. Both concrete gaps found are now closed:
`midi_mapping_add`/`midi_mapping_remove` (2026-09-24) and audio render
`arrangement_render_audio` (2026-09-24, needs the native addon).

Remaining audit list, all verdict: correctly out of MCP scope, no work:
- Settings (JACK startup command/autostart, plugin search paths, JigDAW
  collection URLs): editor-owned, in the GTK app's own
  `~/.config/transmission/config.ttl` (ad-hoc tab format — a different file
  from the server's Turtle `./config.ttl`; see `docs/mcp-live.md`). Server
  operators use CLI flags + `trn:ServerConfig` instead.
- System input/output JACK port strings: already covered end to end
  (`graph.metadata.system{In,Out}putConnections` ↔ RDF ↔ `project_get` /
  `setProjectMetadata`); free-form by design, fuzzy-matched to JACK ports.
- Node port-label metadata: GTK-only display names with no vocabulary term
  and no `Graph` field; per repo convention editor metadata stays out of the
  execution model. Port counts (the execution-relevant part) are covered.

## Instance data lives in the vocabulary namespace

Not urgent, and worth knowing. Saved projects bind the default `:` prefix to
`http://purl.org/stuff/transmissions/`, so every patch node is minted in the vocabulary
namespace: 160 such IRIs across the four repositories, from `trn:pulse` to
`trn:plugins/downspout/ambo`. They are data and the vocabulary document does not define them.

`deploy/nginx/vocab.conf` 303s them to the namespace rather than 404ing, which is the ordinary
behaviour of a slash namespace for an IRI it does not define. Separating them properly means a
namespace of their own and rewriting every committed project file, which changes what every
saved project says. See `docs/namespace.md`.


## Feature : scopes

Add built-in modules Oscilloscope & Spectrum analyzer, loaded like the Output built-ins as required. They should display while running in the main window, like the level meters in the output built-in.

## Bugs

- Plugin-scan duplicate entries reported ("every time the VSTs are scanned on disk a
  duplicate entry for each is added"), on a different machine than the one most of this
  session's other fixes were tested on — not reproduced here. Audited every write path
  into `view.plugins` (`scanPlugins`, `scanJigdawCollections`, `startupScanCompleteIdle`,
  `pluginScanCompleteIdle`, the "Add Plugin…" dialog's own scan, the console `scan`
  command): all sort-and-dedupe by exact bundle `path` via `sortAndDedupePlugins` except
  `consoleScanCompleteIdle`, which rebuilt `view.plugins` and re-sorted by
  category/name without deduping — safe today only because its inputs were already
  deduped upstream. Hardened it to call `sortAndDedupePlugins` like every other
  completion path (2026-09-24), but this wasn't confirmed to be the actual cause of
  what was reported, since a plain path-string comparison should already have caught a
  same-string duplicate in every path I traced. Needs a repro on the machine that saw
  it: does `view.pluginSearchPath` (Settings > Plugins) contain two directory lines
  that are textually different but resolve to the same files (symlink, `~` vs. `$HOME`,
  a mounted duplicate), which `sortAndDedupePlugins`'s exact-string dedupe wouldn't
  catch either?

- Live JACK path produced no audio while the offline probe of the same patch
  was healthy (carried over from the console-banner investigation, 2026-09-24,
  banner fix itself done): a BassGen → Basilico → System Output patch measured
  RMS 0.0 on both `transmission:out_1`/`out_2` via `jack_capture` for 3 s while
  `status` reported playing, but `scripts/probe-project.js` showed BassGen
  emitting MIDI (144 events/30 windows) and Basilico producing audio
  (RMS ~0.10–0.22) — so the graph and plugins are fine and the fault is
  live-JACK-specific.
  Repro attempted headless 2026-09-24 (JACK dummy + NAPI engine, no display
  needed): engine `running`, graph loaded, but `processedBlocks` 0 and peaks
  0 — then traced to the environment, not the engine. The only JACK server
  available is PipeWire's, whose graph never drives any client here (stock
  `jack_simple_client` also idles; a standalone `jackd` cannot start —
  `jackdbus` owns JACK). No verdict possible without a rolling server.
  Two things did clear: (a) auto-connect wired both `out_1`→FL and `out_2`→FR
  correctly here, so the half-wired report was likely stale state on that
  machine; (b) found and fixed alongside: NAPI `loadProject` threw a bare
  `TypeError` on graphs without top-level `metadata` (V8 throw on an
  undefined receiver leaking past status checks — see MISTAKES.md), fixed
  with a typeof guard in `napi_bridge.cpp`, verified by bisection.
  Still needs a session on real hardware: if blocks stay 0 there, suspect the
  process callback never firing (check `pw-top`/xruns) rather than routing.

- `temp.ttl` interchange failure never reproduced (carried over from the
  `UiProjectCodec` error-message fix, 2026-09-24, fix itself done):
  `node scripts/native-ui-project.js load projects/temp.ttl` parses cleanly
  (`ok=1`). Suspects: stale/already-running GTK process, `node` resolution
  difference in the app's subprocess `PATH` vs. the shell's, or a transient
  race with the file being written. Needs a repro against the freshly rebuilt
  binary — the new error message should name the exact field if it recurs.

- JUCE assertion failure in `juce_Messaging_linux.cpp:87` observed when hosting Valis inside Transmission. Likely triggered by a JUCE message thread operation happening off the expected thread. Needs a repro and investigation.

## Live generative DJ via MCP

Claude acts as a DJ via MCP, loading and playing generative patches from
`projects/patches/` and effects from Valis.

Remaining:
- Valis effects integration: load a Valis patch as an effect insert in a DJ chain
- Crossfade transition implementation in dj-runner.js (requires mixer gain params)

### Live MCP setup prerequisites

For `transport_play` and audio control to work via MCP from a Claude session:

1. Start JACK (`jackd -d alsa -r 48000 -p 1024 &`) or enable **Settings > JACK Startup** in the GUI so it starts automatically.
2. Start Transmission: `build-ui-jack-vst3/transmission_graph_ui`
3. Enable the live server: **Settings > MCP Server** in the GUI. This launches `transmission-live.js` with `--native-addon build-napi-jack-vst3/transmission_native.node --jack --auto-connect`.
4. The Claude session must use the transmission MCP server with `--live` (connects to `http://localhost:7878`). This is configured in `~/.claude.json` for the valis project — restart the Claude session after enabling the live server.

## hardcore-techno-160 patch — remaining gaps

- Gremlin DSP is expensive (~12 ms/block probe average). Enable render-ahead
  before using this patch live; all other nodes remain at previous cost.

## JigDAW hosting — remaining gaps

`:JigdawPlugin` nodes load, run and route (see `docs/jigdaw.md`). Still open:

- `ensureThreadRegistered` one-time-per-thread allocation (WAMR threads the
  audio callback thread didn't create): a JACK thread-init callback
  (`jack_set_thread_init_callback`) would close it properly; not taken on since
  it would need threading into every real-time-ish call site (JACK, offline
  render, NAPI), not just one. Detail was in the removed WAMR write-up.

## Engine features

- Plugin delay compensation: no framework exists (VST3
  `getLatencySamples` unread, JigDAW `latencyFrames` surfaced but
  uncompensated). Needs an engine-wide design, not a per-plugin fix.
- ~~Freeze generator output back into arrangement clips~~ **Done, 2026-09-25.**
  `clip_freeze` composes the existing pieces: `engine.captureMidi` →
  note on/off pairing (`freezeClipFromEvents`: velocity-zero offs, stray-off
  and other-node filtering, overlap keeps first, open notes extend to the
  clip end, past-end durations clamped) → `addArrangementClip` validation.
  Source must name a graph node, target is validated by the arrangement
  model, duplicates rejected. Covered without native code (stubbed capture)
  plus a live-HTTP round trip. Determinism caveat stands: frozen output is
  only as deterministic as the generator.
- Bypass and send automation: no bypass or send concept exists anywhere
  (verified 2026-09-24) — needs model + engine design, not just plumbing.

## MCP Live — Phase 3

- GTK full two-way sync: send every in-editor change (add node, connect, drag) to live server
  as `trn:ChangeSet` POST so MCP always sees the latest graph without waiting for a save.
- SSE push (`GET /events`): server endpoint implemented — emits `{revision, generation, filePath}`
  on every state change. GTK still polls; connecting GTK to SSE requires adding a streaming
  CURL handler in `native_graph_ui_main.cpp` to replace the 500 ms `/status` poll.

## Plugin menu unification and startup scan — verification remaining

Done: background startup scan, "_Plugins…" settings with JigDAW collection
URLs in `config.ttl`, merged "Add Plugin…" dialog (all in
`native/src/native_graph_ui_main.cpp`; build-verified, launched once on host
X11). Details were here; removed 2026-09-24 on cleanup.

Not yet verified — needs a manual pass:
- Settings > Plugins dialog with a real collection URL entered, confirming the
  fetched entries appear in the unified Add Plugin list and round-trip through
  `config.ttl`.
- The merged "Add Plugin…" context-menu dialog end to end (select a VST3 row,
  select a JigDAW row, use "Add by IRI…"), interactively in the GTK UI — the
  X11 screenshot workflow in this environment could not reliably capture GTK
  menu popups (they render in separate override-redirect windows that
  `import -window <id>` does not capture), so this needs a human or a
  different capture approach.

## Cross-repo dependencies

- Reduce cross-repo dependencies where it can be done without breakage (from
  INBOX.md, 2026-09-24). Known instances: `JIGDAW_ROOT` / `WAMR_ROOT`
  local-checkout convention instead of `FetchContent` (already the pattern
  for both); the transmission-side JigDAW surface that could move into the
  adapter (profile parsing is already jigdaw-owned). Not started — needs an
  audit of what transmission includes from `~/github/jigdaw` vs. what could
  be adapter API.

## Recurring — check periodically

- Remove completed tasks from this file.
- Check INBOX.md for new tasks and place them here on in a plan as appropriate
- Check MISTAKES.md for systematic problems; promote recurring issues to CLAUDE.md.
- If an issue in MISTAKES.md is fully resolved, remove it.
- For new material, check test coverage.
- Ensure README.md and docs are up-to-date.
