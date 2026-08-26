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

## Console: `parse` command — implemented (2026-08-18)

- `parse [path.ttl]` in GTK console checks the given TTL (or the loaded project) for Turtle syntax errors
- Delegates to `native-ui-project.js check` via `runProjectHelper`; reports node/connection counts on success or the parse error message on failure
- Updated help text in console

## Built-in node: Audio Clip — implemented (2026-08-18)

- `NodeKind::AudioClip` (kind 7) added to all enum layers (UI, codec, runtime)
- `AudioClipProcessor` loads WAV (PCM 16/24/32-bit and 32-bit float, mono or stereo) via `WavReader.h`; loops stereo output when transport plays; resets on stop
- File picker opens on "Add Audio Clip…" context-menu item or "Edit" on an existing node; filename stem used as block label
- Codec bumped to v7; `pluginPath` field carries the WAV file path; `audioOutputs = 2`
- JS interchange updated: kind 7 → `AudioClipNode` type URI

## Built-in node: MIDI Clip — implemented (2026-08-18)

- `NodeKind::MidiClip` (kind 8) added to all enum layers
- `MidiClipProcessor` parses SMF (format 0/1, any ticks-per-beat, tempo events) via `SmfReader.h`; loops events beat-accurate from transport position; `midiOutputs` = SMF track count
- File picker opens on "Add MIDI Clip…" or "Edit"; filename stem used as block label
- MIDI events from all tracks emitted on single output port (original channel bytes preserved); loop wrap-around handled per block
- Known limitation: per-track output port routing requires extending RoutedAudioGraph MIDI port API (future work)


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

- UI live-reload missed when `project_new` resets revision to 0 and edits land it back at the
  same number as the previously cached revision: UI sees no change and skips reload. Fix: C++
  should treat `project_new` (filePath change to null) as a forced reload trigger, or the server
  should use a monotonically-increasing generation counter that never resets.
