# Gen Omega handoff

Last updated: 2026-07-31

## Accepted state

The user accepted the current version after the Techno Build repair.

- Live project: `projects/gen-omega.rpp`
- Accepted render: `renders/gen-omega-techno-build-fix-v5.wav`
- Safety copy from before the repair:
  `projects/gen-omega-before-techno-build-fix.rpp`
- Form: 140 BPM, 4/4, 105 bars, exactly 180 seconds
- Material: six tracks, 17 MIDI clips, and 2,059 authored notes

The v5 render is stereo 48 kHz/24-bit PCM and exactly 180 seconds. Treat the
user's acceptance as authoritative. It is not necessary to remix or master it
unless the user asks for another pass.

## Techno Build repair

Bars 17-32 were effectively silent in the earlier render: -64.1 dB average and
-35.0 dB peak. The MIDI item was present and contained 208 notes. The problem
was downstream gain and gating:

- P-Mix was the only effect after Main Drums' DrumKit and heavily suppressed the
  section's only active sound source.
- Creating the volume envelope had initialized its first point from the track's
  old -9 dB fader value, duplicating attenuation.

The accepted fix is:

- Main Drums FX chain: DrumKit only. Do not re-add P-Mix without an explicit
  bypass/automation plan.
- Main Drums static fader: -13 dB.
- Main Drums volume envelope: 0 dB at 0 seconds, -3 dB at both
  27.428571429 and 54.857142857 seconds, back to 0 dB at 54.858142857,
  then the existing fade from 164.571428571 to 180 seconds.
- Corrected build measurement: -19.2 dB average and -8.9 dB peak.

An attempted +9 dB build envelope was too loud and clipped; it exists only in
the diagnostic `renders/gen-omega-techno-build-fix-v4.wav` and must not be
restored.

## Mix and plugin state

- Field Drums: DrumKit -> Ambo, -18 dB, pan -0.12.
- Fife: Canticle -> Ambo, -20 dB, pan +0.12.
- Main Drums: DrumKit, -13 dB, centered.
- Bass: Basilico verified as Dub, -15 dB, centered.
- Acid Lead: Basilico verified as Acid, -19 dB, pan +0.08.
- Kraut Chords: Canticle -> Orchid -> Ambo, -21 dB, pan -0.08.

The first-pass recipe is `scripts/gen-omega-arrangement.js`. It emits the exact
REAPER `create_midi_clip` payloads and no longer recommends P-Mix on Main Drums.

## Render history

- `gen-omega-first-pass-final.wav` is superseded because its Techno Build is
  effectively inaudible.
- `gen-omega-techno-build-fix-v4.wav` proves the diagnosis but clips because the
  +9 dB compensation was excessive.
- `gen-omega-techno-build-fix-v5.wav` contains the accepted -3 dB build balance.
  Its later sections can reach 0 dBFS. Do not silently replace the accepted file;
  if the user requests mastering, make a new version and preserve v5.

## REAPER MCP findings

Changes were made in `/home/danny/github/reaper-mcp` with nine passing tests:

1. `connection.ensure_connected()` now calls `reapy.reconnect()` when the server
   was imported before REAPER exposed its generated API.
2. Render bounds now use REAPER's actual constants: 1 for the entire project and
   2 for a time selection.
3. Master-volume writes convert dB to REAPER's native linear gain.

Restart the Codex/MCP session after those source changes before relying on
`render_project` or `set_master_volume`. The running server may still contain an
older imported module.

`get_project_info` still reports misleading metadata in this environment: the
path can appear as `/home/danny/Documents/REAPER Media`, and the time signature
as `140/4`. The saved RPP is the authoritative source and contains `TEMPO 140 4
4`.

## Remaining Transmission gaps

Transmission now has a persisted beat-domain MIDI arrangement, native scheduled
playback, built-in gain envelopes, GTK project interchange, and the canonical
`projects/gen-omega.ttl` project. The installed Downspout plugins were smoke-tested
at the opening, Techno Build, and final fade without an external sequencer.

General scheduled VST3 automation and deterministic MIDI generator capture/freeze
remain listed in `docs/todo.md`; neither is required for the accepted authored-MIDI
Gen Omega arrangement.
