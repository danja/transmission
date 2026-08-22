---
name: patch-authoring
description: This skill should be used when the user asks to "build a patch", "create a project", "make a patch", "wire up plugins", "set up a graph", "generate music", "create a beat", "build an ambient patch", "choose plugins", "select plugins for", or discusses routing downspout plugins in Transmission. Also activates when the user mentions specific plugin names (Conductor, Mixgen, T-Mix, BassGen, DrumGen, MelGen, etc.) in the context of building or connecting things.
version: 1.0.0
---

# Patch Authoring — Transmission + Downspout

Downspout provides 40+ plugins across generators, processors, instruments, effects, and controllers. Maximise variety by matching plugins to musical intent using signal types and role taxonomy, not just familiar combinations.

## Step 1 — Identify musical intent

Determine the target character before selecting plugins:

- **Generative / evolving** → Conductor for long-form structure; Ground or BassGen + DrumGen + MelGen/Harmonic-Atlas
- **Beat-driven / rhythmic** → DrumGen or Polymeter or Xoxolo → DrumKit; add P-Mix or E-Mix for gate variation
- **Ambient / textural** → Syrinx or Canticle + Ambo, Orchid, Resonance-Garden (Guardian mandatory downstream)
- **Chaotic / glitch** → Gremlin + GremlinDriver; add Rift for buffer disruption
- **Performance / live** → Luma or PaunchLad (require Launchpad); or Drift for hands-on CC modulation
- **Harmonic / melodic** → Harmonic-Atlas → Canticle; ArpGen or Cadence as MIDI processors; Counterpointer for counter-melody

Combine archetypes freely. Load `references/plugin-inventory.md` for the full plugin table with roles and signal types.

## Step 2 — Select plugins using signal-type compatibility

Match `trn:produces` → `trn:accepts` between nodes. Key signal types:

| Type | Meaning |
|---|---|
| `trn:Audio` | Standard audio |
| `trn:BassMidi` | Bass register MIDI |
| `trn:DrumMidi` | Drum MIDI |
| `trn:MelodyMidi` | Melody MIDI |
| `trn:HarmonyMidi` | Chord/harmony MIDI |
| `trn:ControlMidi` | CC/scene control only — not musical notes |
| `trn:MultiPartMidi` | Multi-lane MIDI (Luma) |

Rules:
- Instruments accept typed MIDI and produce Audio; match types precisely (e.g. Basilico needs `BassMidi`, not generic `Midi`)
- Generic `Midi` is a fallback — prefer typed connections for generators that produce them
- `ControlMidi` flows from controllers (Conductor, Mixgen, Drift, Oracle) to receivers; never connect ControlMidi to an instrument's MIDI input
- Check `trn:requires` — some plugins need `HostTransport` (transport must be running) or `Launchpad`

Use `references/plugin-inventory.md` to look up any plugin's full I/O profile before wiring.

## Step 3 — Wire the Conductor CC bus (if using Conductor)

Conductor emits `ControlMidi` to reshape generators over time (scene, density, energy, mutation, reset). Load `references/cc-routing.md` for the full CC table.

To connect Conductor to a receiver:
1. Wire Conductor's MIDI out → receiver's MIDI in
2. Set the receiver's `conductor_ch` parameter to Conductor's output channel (0 = off, 1–24 = channel)
3. Compatible receivers: BassGen, Ground, DrumGen, Harmonic-Atlas

Conductor is recommended before Harmonic-Atlas in the signal chain.

## Step 4 — Wire the Producer Control Bus (PCB) (if using Mixgen)

Mixgen drives T-Mix gain lanes and FX parameters via CC. Load `references/cc-routing.md` for the full CC table (CC 19–33).

Standard wiring (all three receive Mixgen's ControlMidi):
```
Mixgen MIDI out → T-Mix MIDI in      (CC 20–27: gain strips 1–8)
Mixgen MIDI out → Loopdelay MIDI in  (CC 30–31: time/feedback)
Mixgen MIDI out → Lightverb MIDI in  (CC 32–33: wet/space)
```
Or chain serially — T-Mix, Loopdelay, and Lightverb all forward MIDI unchanged:
```
Mixgen → T-Mix → Loopdelay → Lightverb
```

Mixgen profiles: **T-Mix only** (default), **FX-only**, **Full bus** (CC 20–33 together).

## Step 5 — Order the audio chain

Follow these ordering rules to avoid problems:

1. **Generators / instruments** — produce audio first
2. **T-Mix** — collects up to 8 mono strips into stereo; place before effects
3. **Loopdelay** → **Lightverb** — recommended order; Loopdelay before Lightverb
4. **Rift / Orchid / P-Mix / E-Mix** — insert anywhere post-mix
5. **Orbit** — spatial motion; place before Guardian
6. **Resonance-Garden** — always place Guardian downstream (feedback accumulation risk)
7. **Guardian** — always last in the audio chain; output safety (DC removal, look-ahead limiter, true-peak)

Never omit Guardian on a live or rendered output chain.

## Step 6 — Build via MCP or TTL

**Via MCP** (`graph_apply_changes`):
- Declare nodes with correct `bundleName` values (e.g. `"conductor.vst3"`)
- Verify real port counts with `transmission_vst3_inspect` before wiring connections (port counts in declarations are overwritten by the inspector)
- Use `plugin_describe` to confirm parameter names before calling `parameter_set`
- After `project_new`, apply a `setProjectMetadata` no-op if the revision stays at 0 to force a UI reload

**Via TTL** (`.ttl` project files):
- Use `:audioOutputs N` on each node — missing this causes the probe to reject the plugin
- MIDI port indices are 0-based; two sources to the same `:toPort` silently clobber each other
- Run `node scripts/probe-project.js projects/<name>.ttl` to confirm signal reaches output before using JACK

Load `examples/` for working patch templates.

## Quick reference — unusual plugins worth including

| Plugin | Why use it |
|---|---|
| **Oracle** | Extracts audio/MIDI features and emits mapped CCs — bridges audio analysis into control |
| **Drift** | Four-lane transport-synced LFO/chaos/S&H modulator; audio transparent |
| **Lifeform** | Conway Game of Life → MIDI — non-repeating generative pattern |
| **TuneyVst** | Text-to-music with microtonal scales — unusual melodic material |
| **Campione** | RDF-patched zone sampler with MCP server on port 7220 |
| **Syrinx** | Avian vocal synth (10 bird models) — distinctive textural source |
| **Ambo** | Multi-module ambient processor (shimmer, spectral, tape, feedback) |
| **Resonance-Garden** | Turns any stereo input into pitched resonator output |
