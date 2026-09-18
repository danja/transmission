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

- No state serialisation: `jig:Abi1` and `jig:Abi2` carry none, so a project restores a
  JigDAW plugin's parameters but not anything it keeps beyond them. Needs a profile
  statement or a processor message upstream in jigdaw before a host can do anything.
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

## Recurring — check periodically

- Remove completed tasks from this file.
- Check INBOX.md for new tasks and place them here on in a plan as appropriate
- Check MISTAKES.md for systematic problems; promote recurring issues to CLAUDE.md.
- If an issue in MISTAKES.md is fully resolved, remove it.
- For new material, check test coverage.
- Ensure README.md and docs are up-to-date.
