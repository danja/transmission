To find gaps in the toolset we have with Transmission and the Downspout plugins, I propose attempting the following piece of music using what's available and noting where we aren't well-equipped. We will then implement the missing pieces.

Current live-project status and next-session notes are in
[`docs/gen-omega-progress.md`](gen-omega-progress.md).

As necessary read material from :
/home/danny/github/transmission
/home/danny/github/downspout

The piece, called Gen Omega, will be 3 minutes long and begin with distant fife and drums, 140 bpm, like an 19th century army battalion coming over a hill. There will then be a classic techno build and drop, which will introduce a dubstep-style wub bass and looping dub style percussion. After a little time in half-tempo, a more driving 4x4 techno bass line will take over, with a hypnotic acid house-style repetitive melody. This will devolve into an extreme hardcore industrial beat, with a chordal base with hypnotic Krautrock influences before fading to oblivion.

## First arrangement pass

At 140 BPM, 105 bars of 4/4 last exactly 180 seconds. The first pass uses this
form:

| Bars | Section | Purpose |
| ---: | --- | --- |
| 1-16 | Distant Battalion | Fife and field drum approach from a heavily reverberant distance. |
| 17-32 | Techno Build | Four-on-the-floor kick and an increasingly dense snare build. |
| 33-48 | Wub Dub Half-Time | Half-time drop, Basilico wobble bass, and sparse dub percussion. |
| 49-72 | Driving Acid | Driving bass, full techno drums, and a repetitive Dorian acid line. |
| 73-96 | Industrial Krautrock | Hardcore drums and distorted pedal bass under motorik power chords. |
| 97-105 | Fade to Oblivion | Drum fragments, sub decay, and a long D-minor reverb tail. |

Run `npm run --silent gen:omega` to emit a deterministic JSON recipe. Its clips use the
`start_position`, `length`, and note fields accepted by REAPER MCP's
`create_midi_clip`, so they can be applied when the REAPER connector is healthy.
The recipe keeps fife, drums, bass, acid, and chords on separate editable tracks
and names the intended Downspout instruments and effects for each one.
Run `npm run gen:omega:project` to regenerate the native Transmission project at
`projects/gen-omega.ttl`.

## Downspout mapping

- DrumKit renders the authored field, techno, dub, and industrial drum MIDI.
  DrumGen or Xoxolo can later replace selected authored clips with captured
  variations.
- Canticle handles the fife and chordal parts. Ambo supplies the distant opening
  and terminal reverb tail; Orchid can enrich the Krautrock chords.
- Two Basilico instances keep the wub/driving bass and acid melody independently
  editable. E-Mix provides rhythmic gating, while PaunchLad supplies dub space.
- Rift is reserved for the build/drop and acid-to-industrial transitions.

## Gaps found and implemented

The available plugins cover the requested sound roles. The principal missing
pieces are in Transmission's composition control plane:

1. Transmission now persists beat-domain MIDI clips and compiles their note-on
   and note-off events into immutable native schedules. The callback injects
   bounded, sample-offset MIDI directly into each target graph node, and the same
   path is used for live and offline processing.
2. Built-in gain nodes now persist static gain and step/linear beat-domain
   envelopes. This covers role balance and the nine-bar master fade without
   requiring general VST3 automation.
3. Projects still cannot persist or schedule general VST3 parameter and bypass automation.
   The graph supports bounded immediate parameter changes, but not the filter,
   wobble-rate, distortion, and effect-send curves listed as
   `productionCues` in the recipe.
4. Generated MIDI needs a capture/freeze path if DrumGen or Xoxolo performances
   are to become deterministic, editable clips.

## Native Transmission result

`projects/gen-omega.ttl` is a self-contained 140 BPM performance definition with
420 beats, 17 clips, 2,059 notes, six instrument roles, per-role gain nodes, and
a master fade ending at -120 dB. GTK project interchange v3 preserves the
arrangement and defaults the render dialog to its full 105-bar length.

The VST3 project probe verified the installed Downspout bundles without an
external MIDI sequencer. At the start of the Techno Build, Main Drums Gain measured
0.0802 RMS and Master Gain measured 0.0568 RMS. A probe starting at the final
nine-bar section measured Master Gain falling from 0.0500 RMS in its first second
to 1.22e-8 RMS in its last second.

## Live REAPER result

The recipe was applied to a new REAPER project at
`projects/gen-omega.rpp`. It contains six instrument tracks, 17 MIDI clips,
2,059 notes, six named arrangement regions, and a nine-bar fade. Basilico was
verified in Dub mode for bass and Acid mode for the lead. The project uses
DrumKit, Canticle, Basilico, Ambo, and Orchid.

The first live mix exposed an effectively silent Techno Build: P-Mix was gating
the section's only sound source while an automatically initialized volume
envelope added another 9 dB of attenuation. P-Mix was removed from Main Drums,
the baseline was corrected to unity, and a -3 dB section envelope was confined
to bars 17-32. The pre-fix project remains at
`projects/gen-omega-before-techno-build-fix.rpp`.

The original `renders/gen-omega-first-pass-final.wav` is superseded because its
Techno Build is effectively inaudible. The corrected diagnostic render is
`renders/gen-omega-techno-build-fix-v5.wav`: 180 seconds, 48 kHz, stereo, and
24-bit PCM. Its build measures -19.2 dB average with an -8.9 dB peak, compared
with -64.1 dB average and -35.0 dB peak before the repair. The build itself is
unclipped; later sections still need a fresh mastering pass before v5 should be
treated as the final release render.

The attempt also found and fixed two REAPER MCP issues: recovery when the server
starts before REAPER, and incorrect render-bound constants. Master-volume writes
now convert dB to REAPER's native linear gain representation as well.
