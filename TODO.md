# TODO

## The trn: namespace is deployed

**Done, 2026-09-18.** `http://purl.org/stuff/transmissions/` resolves. It had always returned
404: the PURL side was correct all along and nothing was served at the far end, so every
`trn:` IRI published by this project, by plugin-universe, by downspout and by JigDAW pointed
at nothing.

Measured against the live server after deployment: the PURL chain ends at 200, `text/turtle`
when asked for and `text/html` for a browser, 42048 bytes matching the committed build byte
for byte, parsing to 628 triples and 209 subjects. Every term 303s, including the hyphenated
and slashed IRIs that saved projects mint. CORS on every response. `jig:` still resolves, so
the second `include` in that server block broke nothing.

To check it again:

```sh
curl -sS -H "Accept: text/turtle" https://hyperdata.it/xmlns/transmissions/ | grep -c '^trn:'
curl -sS -o /dev/null -w '%{http_code}\n' https://hyperdata.it/xmlns/transmissions/PluginProfile
```

Expect 208 and 303. Pipe the document into `head` and curl exits 23: that is `head` closing
the pipe on a 42 kB body, not a failure. `grep -c` and `sed -n '1,20p'` read to the end and
exit 0.

Adding a term is `npm run build:vocab`, commit, `git pull` on the server. No nginx reload:
files are read from disk per request, and `tests/vocab/site.test.js` fails if the committed
copy is stale. See `docs/namespace.md`.

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

## JigDAW hosting — remaining gaps

`:JigdawPlugin` nodes load, run and route (see `docs/jigdaw.md`). Still open:

- ~~No state serialisation~~ **Fixed upstream, 2026-09-24.** `jigdaw::Profile` now parses
  `jig:asset` (key, resource, `jig:userReplaceable`), `jigdaw::Chain::add` fetches, verifies
  and loads each one before the slot is used (same as the module itself), and
  `jigdaw::Module::loadAsset` re-resolves every cached buffer pointer afterwards since a
  loader may grow the module's linear memory (`native/jigdaw-adapter/{include,src}/jigdaw/
  {Profile,Module,Chain}.{hpp,cpp}` in the jigdaw repo). Verified: profile parsing against
  Ferrite's real `#nam`/`#ir` assets, and a synthetic wasm module exercising fetch + integrity
  check + load + post-`memory.grow` pointer refresh end to end (including a deliberately
  tampered asset being refused). `jigdaw`'s own `ctest` suite still passes.

  `JigdawProcessor::initialize` (the real playback path — `Chain::add` is a separate call
  path used only by `jigdaw`'s own tests) had the same gap independently, since it drives
  `jigdaw::Module` directly rather than through `Chain`; fixed there too, verified with the
  same synthetic module through `JigdawProcessor` itself (topology reports the asset,
  `initialize` loads the shipped default, and again with an override path).

  UI, also done, 2026-09-24: `JigdawPluginTopology::assets` (key, `userReplaceable`),
  `JigdawProcessor::initialize`'s new `assetOverridePaths` parameter (a local file read
  instead of the fetched default, same `loadAsset` either way), `RuntimeGraphNode::
  jigdawAssetOverridePaths` and `Node::jigdawAssetOverrides` threading it from the graph to
  `uiProcessorFactory()`, and a `GtkFileChooserButton` row per `userReplaceable` asset in the
  generated JigDAW panel (`native_graph_ui_main.cpp`, mirroring `jigdaw/src/ui/Panel.js`'s
  file `<input>`). Picking a file logs to the console and marks the graph changed; it takes
  effect on the next compile (Play), the same "control thread, before the module runs" rule
  `Module::loadAsset` already requires — there is no live hot-swap into an already-running
  node.

  **wasm3 replaced with WAMR, 2026-09-24 — Ferrite now loads and runs.** wasm3's interpreter
  had no WebAssembly SIMD support and Ferrite (`jig:wasmFeature jig:Simd128`) needs it; both
  `Chain::add` and `JigdawProcessor::initialize` failed at the `jig_init` lookup before assets
  were even reached. Swapped in the jigdaw repo only — `Module.hpp`'s public interface is
  unchanged, so nothing outside `native/jigdaw-adapter/src/Module.cpp` and the two build
  scripts (`native/cmake/FindOrFetchWamr.cmake`, replacing `FindOrFetchWasm3.cmake`;
  `jigdaw-adapter/CMakeLists.txt`) needed to change:
  - Built as WAMR's fast interpreter with SIMD128 and reference types on, AOT/JIT off — pure
    bytecode interpretation, the same "no executable pages, nothing platform-specific to
    debug" property wasm3 was originally chosen for. Reference types (`WAMR_BUILD_REF_TYPES`)
    turned out to be needed too: Rust's `wasm32-unknown-unknown` target emits that section by
    default even when a module never uses one, and Ferrite's build does.
  - A local checkout (`WAMR_ROOT`, default `~/github/wasm-micro-runtime`) rather than
    `FetchContent`, the way `JIGDAW_ROOT` already works here — WAMR has no shallow-clone-sized
    release the way wasm3's tag did.
  - `wasm_runtime_call_wasm_a`'s own source was read to confirm it stays on a fixed 16-cell
    stack buffer (`argv_buf[16]`) and only allocates above that; every call this ABI makes has
    at most 2 argument cells, so nothing in the hot path allocates. One real caveat: a thread
    WAMR did not create (the audio callback thread) must call `wasm_runtime_init_thread_env()`
    once before its first call in — done lazily, thread_local-guarded, in every `Module`
    method that calls into wasm, so it is correct regardless of which thread ends up calling
    first, at the cost of that first call being allowed to allocate (a documented,
    accepted-in-code compromise; a JACK thread-init callback would remove even that but was
    not taken on here — see the comment on `ensureThreadRegistered` in `Module.cpp`).
  - Verified: `jigdaw`'s own `ctest` suite passes (`chain_file` covers real non-SIMD plugins
    end to end); a standalone `jigdaw::Chain` test against Ferrite's real profile now loads it
    and, fed an impulse, produces output smeared across every following sample (the amp model
    and convolution actually engaging, not a dry passthrough); the same confirmed again through
    `transmission::JigdawProcessor`/`JigdawInspector` directly (topology reports both assets,
    `initialize` loads the SIMD module and the shipped nam/ir defaults, `process` produces the
    same non-dry output) — the whole path the original bug report was about, now working.
    `transmission_jigdaw_processor_test` and the rest of `native/build-jigdaw`'s suite pass;
    `transmission_graph_ui` builds clean in both JigDAW-enabled and JigDAW-disabled configs.

  Still open:
  - No persistence: an override path lives only in the running UI's `Node`, not in the saved
    project (`UiProjectNode`/`UiProjectCodec` TTL, or the MCP `graph_apply_changes` schema),
    so it is lost on reload. Needs a project-format decision, not just more code.
  - Not exercised interactively in the GTK app itself in this session (screenshot tooling here
    cannot reliably capture GTK popups/dialogs); confirmed instead by driving
    `JigdawProcessor`/`JigdawInspector` directly, against both the synthetic test module and
    Ferrite's real profile, and by clean builds across every JigDAW-enabled `native/build*`
    directory.
  - The `ensureThreadRegistered` one-time-per-thread allocation noted above — a JACK
    thread-init callback (`jack_set_thread_init_callback`) would close it properly; not taken
    on since it would need to be threaded into every real-time-ish call site (JACK, offline
    render, NAPI), not just one.
- `jigdaw::Chain::process` (in the jigdaw repo, not used here) processes only
  `min(frames, jig_max_frames())` and leaves the rest of the block stale. Transmission
  drives `jigdaw::Module` directly and sub-blocks it instead, but the adapter that ships
  in jigdaw has the bug at any host buffer above 128 frames. Report upstream.
- `jig:latencyFrames` is read into the profile and then ignored; there is no latency
  compensation for a JigDAW node.
- The GTK "Add JigDAW Plugin…" dialog dereferences the IRI on the main thread, so a slow
  or unreachable https origin freezes the editor for up to the fetch timeout. This matches
  what the UI already does for VST3 inspection, but that reads a local file and this reads
  the network. jigdaw's own editor runs the same load on a worker for exactly this reason
  (`native/jigdaw-adapter/src/dpf/JigdawUI.cpp`); do the same here.

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

## Plugin menu unification and startup scan — verification remaining

Implemented in `native/src/native_graph_ui_main.cpp`:

- Startup no longer walks the VST3 search path (and, with JigDAW, fetches every
  configured collection) synchronously before the window appears. Both run on a
  background thread (`startupScanThreadFunc`) and merge into `view.plugins` via
  `g_idle_add` once the main loop is pumping. This was the slow-loading cause
  reported in INBOX.md.
- Settings menu item and dialog renamed "Plugin Path…" / "Plugin Paths" →
  "_Plugins…" / "Plugins", and the dialog gained a second text box for JigDAW
  plugin collection URLs (`docs/jigdaw.md`,
  `/home/danny/github/jigdaw/docs/plugin-collections.md`), persisted to
  `config.ttl` as `JIGDAW_COLLECTION` lines and merged into the same plugin
  list as scanned VST3 bundles (`scanJigdawCollections`,
  `collectJigdawCollectionEntries`, `parsePluginCollection`).
- The right-click "Add VST3 Plugin…" / "Add JigDAW Plugin…" context menu items
  are merged into one "Add Plugin…" entry. Its dialog lists VST3 and JigDAW
  entries together (JigDAW ones flagged in a hidden list-store column) and
  keeps an "Add by _IRI…" button for a one-off JigDAW plugin not in any
  collection (the former free-text dialog, unchanged).
- The collection parser is a targeted regex extraction of the normative
  `dcterms:hasPart` / `rdfs:label` shape (not a general RDF parser, consistent
  with this file's existing ad hoc Turtle handling), verified standalone
  against `examples/reference-collection.ttl` and `web/collections/jigdaw.ttl`
  in the jigdaw checkout.

Verified: `transmission_graph_ui` builds clean in both `build-ui-jack-vst3`
(JigDAW + JACK + VST3 on) and `build-ui` (all off) configs; launched on the
host X11 session and loaded an existing project instantly.

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

## Recurring — check periodically

- Remove completed tasks from this file.
- Check INBOX.md for new tasks and place them here on in a plan as appropriate
- Check MISTAKES.md for systematic problems; promote recurring issues to CLAUDE.md.
- If an issue in MISTAKES.md is fully resolved, remove it.
- For new material, check test coverage.
- Ensure README.md and docs are up-to-date.
