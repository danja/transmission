# TODO

## URGENT: External interface / audio output broken

- No audio output from Transmission UI.
- Launching the GTK UI via Claude crashes with "Illegal Operation", killing the Claude process.
- Investigate external interface settings (JACK port connections, device selection) as likely root cause.
- Must reproduce and diagnose outside Claude session (run UI in a separate terminal).

## hardcore-techno-160 patch — remaining gaps

Routing fixes applied (2026-08-04):
- bassgen → arpgen MIDI added; arpgen now active (107 events/5 s)
- ambo → oracle audio added (replacing duplicate ambo→rift); oracle and rift now active
- canticle stays active for the full run (arpgen feeds it throughout)

Still open:
- `:gremlin` produces 0 audio despite gremlin-driver MIDI. The TTL declares no
  audioInputs, consistent with it being an instrument, but the plugin may need
  specific trigger messages or a warm-up period longer than 5 s. Investigate
  with a longer probe run or by inspecting the gremlin.vst3 topology directly.
- `:xoxolo` pattern all-zero — intentional until programmed.

Performance with oracle now active: 60% late blocks in the headless probe (was 35%)
because oracle FFT analysis adds ~75 µs average per block. This is offline-probe
cost only. Use render-ahead when running live; the persistent setting will now be
saved per-patch.

## Settings persistence — implemented (2026-08-04)

Render-ahead, buffer size, and processing-thread count now persist per project:

- JS: `trn:audioSettings` vocabulary terms and TTL read/write in
  `TransmissionRdf.js` / `Vocabulary.js`
- Interchange: `SETTINGS` line in `native-ui-project.js` and
  `UiProjectCodec.cpp` (codec bumped to version 6; versions 1–5 still load)
- C++: `UiProject` struct carries the three fields; `captureProject` /
  `applyProject` round-trip them through the codec and `GraphView`

UI additions:
- Settings → Reconnect JACK Ports: calls `applyExternalConnections()` live
  without stopping audio — useful when PipeWire reassigns device numbers
- System Input/Output dialogs now show resolved JACK port names (after suffix
  substitution) below the selectors when JACK is available

## Engine features

- Suspend schedule-only instrument processors outside their authored activity
  window while preserving a bounded post-note tail.
- Add persisted VST3 parameter, bypass, and send automation with bounded
  sample-offset delivery to the native engine.
- Add a deterministic capture/freeze path for MIDI generator output.

## MCP Live — remaining work

Phase 1 (foundation) is complete: HTTP server, HTTP client, live entry point, GTK poll+sync.

Phase 2 — implemented (2026-08-10):
- Auto-reload GTK view when external edit detected: `liveServerPollTick` fetches `/graph`,
  writes to a temp `.ttl`, runs the project helper, calls `applyProject`, redraws.
- `transmission-live.js` subprocess launch from GTK on startup: if the initial ping fails,
  `launchLiveServer` starts the server via `g_subprocess_new`; poll timer always runs.
  On window destroy, the subprocess receives SIGTERM.

Still open (Phase 3):
- GTK full two-way sync: send every in-editor change (add node, connect, drag) to live server
  as `trn:ChangeSet` POST so MCP always sees the latest graph without waiting for a save.
- SSE (`GET /events`) for push notifications to GTK and MCP instead of 500 ms polling.
