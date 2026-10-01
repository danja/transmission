# TODO

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

- ~~JUCE assertion failure in `juce_Messaging_linux.cpp:87` when hosting Valis~~ —
  **reproduced and diagnosed 2026-09-30, fix is Valis-side.** The assertion is
  JUCE's "message thread overloaded by tasks taking too long" in
  `InternalMessageQueue::postMessage`: it fires when 128 posted messages are
  still unprocessed. Valis is the source because **its in-process MCP server
  has no subscriptions/pubsub and `juce::MessageManager` is created without an
  event loop** (JUCE defaults `JUCE_DISPATCH_POSTMESSAGE_FIFO` on and
  `addEventToPost` on Linux to X11 `DispatchEvent`), and Transmission is a GTK
  host that pumps no JUCE events. Every MCP `tools/call` and resource read goes
  through `McpServer.cpp`'s `onMessageThread`, which `MessageManager::callAsync`
  + `WaitableEvent::wait(5000)` — every concurrent caller queues one message.
  Two plugin instances are involved: the audio instance runs the MCP server, and
  a second instance (the live UI opens the editor on a separate provider) owns
  the JUCE message thread that services them.
  Reproduced headless with a harness calling `Vst3EditorHost::open` (exactly what
  the UI's double-click does) while `Vst3Processor` processes audio, with Valis'
  MCP on and 256 concurrent `/mcp` calls: **671 assertions in 20 s**. At 8
  concurrent callers: 0 — which is why this looked intermittent.
  Fix belongs upstream: the MCP server should not block a request thread per
  call on the message thread (reply via a callback/pollable), or Valis should
  supply its own JUCE event loop. Not a Transmission bug and nothing to fix
  here; transmission merely hosts the plugin.

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

`:JigdawPlugin` nodes load, run and route (see `docs/jigdaw.md`; re-verified
2026-09-30: `transmission_jigdaw_inspect` + `transmission_jigdaw_probe` on
`file:///home/danny/github/jigdaw/plugins/pulse/` healthy, and
`scripts/probe-project.js projects/patches/jigdaw-pulse.ttl` renders 30 s with
BassGen → Pulse audio). Discovery is documented in `docs/jigdaw.md` ("Finding
plugins"): the canonical collection `https://strandz.it/jigdaw/collections/jigdaw.ttl`
pasted into Settings > Plugins, the gallery, and `jigdaw_describe` for single IRIs.
No site changes were needed — profiles and the collection already serve Turtle with
CORS. Still open:

- MCP collection browsing: closed 2026-09-30 with the `jigdaw_collection` tool
  (`readJigdawCollection` → `listJigdawCollection` → MCP + `POST
  /plugins/jigdaw/collection`, verified live against the strandz collection: 23
  members, relative IRIs resolved, describe follows). A collection carries no
  profiles, so each member still goes through `jigdaw_describe` before wiring.
- plugin-universe.com is not currently a source of Jig plugins (native catalogue;
  its JigDAW submission flow is in progress upstream). Watch, don't build around.

- `ensureThreadRegistered` one-time-per-thread allocation (WAMR threads the
  audio callback thread didn't create): a JACK thread-init callback
  (`jack_set_thread_init_callback`) would close it properly; not taken on since
  it would need threading into every real-time-ish call site (JACK, offline
  render, NAPI), not just one. Detail was in the removed WAMR write-up.

## Engine features

- Plugin delay compensation: no framework exists (VST3
  `getLatencySamples` unread, JigDAW `latencyFrames` surfaced but
  uncompensated). Needs an engine-wide design, not a per-plugin fix.
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

## VST3 plugins on Linux need a run loop from the host

Fixed 2026-10-01. The VST3 specification states it outright
(`pluginterfaces/gui/iplugview.h`): *"On Linux the host has to provide this
interface to the plug-in as there's no global event run loop defined as on
other platforms."* Transmission wasn't, so a JUCE plug-in had nowhere to run
its message queue — posts accumulated to the framework's limit (assert at 128
in `juce_Messaging_linux.cpp`) and its timers never fired. Reproduced 671
assertions in 20 s with Valis under load.

Two defects, both now fixed:
- `Vst3Processor`/`Vst3EditorHost` set only `PluginContextFactory::setPluginContext`
  and never called `IPluginFactory3::setHostContext`, which is where JUCE loads
  its run loop. Nothing was reaching the plug-in at all.
- `native/src/Vst3HostContext.h` (new) is the host context, and supplies a
  `Steinberg::Linux::IRunLoop` on its own thread — `poll()` for descriptors, a
  deadline for timers. A thread rather than a GLib main loop because the engine
  loads plug-ins with no main loop at all. Nothing starts until a plug-in
  registers, so a project with no such plug-in pays nothing.

Thread discipline: callbacks are made with no state lock held (a plug-in may
register from inside one) and under a shared lock that `unregister*` takes
exclusively, so a handler cannot be destroyed under a call in flight. The loop
is shut down explicitly before the provider is released, since the interfaces
it calls belong to the plug-in.

Result: 256 concurrent callers, 5822 requests, all answered, zero assertions
during the run. Residual asserts at teardown are JUCE's own leaked-object and
singleton reports from unloading a JUCE module in a non-JUCE host — unrelated.

Upstream is worth knowing: Valis also bounds its own in-flight message-thread
work (`src/mcp/McpServer.cpp`), which fixed a use-after-free on its request
timeout and a silent wrong-answer path there. That gate is defence in depth
now, not the fix.

## Cross-repo dependencies

Audited 2026-09-30 (from INBOX.md). The jigdaw surface transmission uses is already
the adapter's public API and nothing else: `jigdaw::Chain::fetchProfile`, `fetchUrl`,
`verifyIntegrity`, `Profile`/`Module`/`Port`, `Midi`, transport constants
(`native/src/JigdawProcessor.cpp`, plus `fetchUrl` for collection documents in
`native/src/native_graph_ui_main.cpp`). Profile parsing is jigdaw-owned; nothing
transmission-side duplicates it except `parsePluginCollection`, an ad-hoc string
scan over the tiny `jig:PluginCollection` shape (labels + `dcterms:hasPart`) rather
than a second Turtle parser — moving it into the adapter is possible but would drag
a GTK `PluginCacheEntry` type across the boundary, so it stays.

- `JIGDAW_ROOT` local-checkout convention instead of `FetchContent`: keep. Both repos
  pin the same way and the adapter has no stable install target yet; fetching a
  moving main would trade a path for version drift.
- `JIGDAW_HTTPLIB_DIR` defaulting into `~/github/downspout/third_party/cpp-httplib`:
  keep. jigdaw's own CMake expects the same path, so this is one shared checkout, not
  two dependencies — and the fix belongs upstream (jigdaw vendoring httplib or taking
  it via its package manager), not in transmission's flags.
- `trn:WAM` vocab drift (plugin-universe proposed it upstream; `npm test` failed until
  adopted): closed 2026-09-30 by adding it to `vocabs/formats.ttl` + rebuilt
  `deploy/vocab/transmissions.ttl`. The `site.test.js` pair-watch covers future drift
  in both directions.

## Recurring — check periodically

- Remove completed tasks from this file.
- Check INBOX.md for new tasks and place them here on in a plan as appropriate
- Check MISTAKES.md for systematic problems; promote recurring issues to CLAUDE.md.
- If an issue in MISTAKES.md is fully resolved, remove it.
- For new material, check test coverage.
- Ensure README.md and docs are up-to-date.
