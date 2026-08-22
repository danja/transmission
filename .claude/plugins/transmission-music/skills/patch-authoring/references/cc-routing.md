# CC Routing Reference

## Producer Control Bus (PCB)

Source: `/home/danny/github/downspout/docs/producer-control-bus.md`
Producer plugin: Mixgen (`mixgen.vst3`)

### CC address table

| CC | Role | Receiver | Notes |
|---:|---|---|---|
| 19 | Gate | All bus receivers | 127 = acquire ownership; 0 = release. Receivers default to not requiring this. |
| 20 | Gain strip 1 | T-Mix ch 1 | Normalized 0–127 overlay |
| 21 | Gain strip 2 | T-Mix ch 2 | |
| 22 | Gain strip 3 | T-Mix ch 3 | |
| 23 | Gain strip 4 | T-Mix ch 4 | |
| 24 | Gain strip 5 | T-Mix ch 5 | |
| 25 | Gain strip 6 | T-Mix ch 6 | |
| 26 | Gain strip 7 | T-Mix ch 7 | |
| 27 | Gain strip 8 | T-Mix ch 8 | |
| 30 | Delay time | Loopdelay | Or synchronized-length selection |
| 31 | Delay feedback | Loopdelay | |
| 32 | Reverb wet mix | Lightverb | |
| 33 | Reverb space | Lightverb | |

### Wiring rules

- Connect Mixgen MIDI out to T-Mix, Loopdelay, and Lightverb MIDI inputs
- Chain is valid: `Mixgen → T-Mix → Loopdelay → Lightverb` (all three forward MIDI unchanged)
- Receiver "Control channel" param: 0 = Omni (any channel), 1–16 = specific channel
- Mixgen sends gains on channels 1–8 (one per strip); Omni works for simple setups
- "Require CC 19 gate" defaults off — safe to omit gate wire unless intentional ownership enforcement is needed

### Mixgen profiles

| Profile | CCs emitted |
|---|---|
| T-Mix only (default) | CC 20–27 |
| FX-only | CC 30–33 |
| Full bus | CC 20–33 (FX defaults inverted: delay/reverb rise as source mix recedes) |

---

## Conductor CC Bus

Source: Conductor plugin profile at `/home/danny/github/downspout/plugins/conductor/profile.ttl`
Producer plugin: Conductor (`conductor.vst3`)

### CC address table

Conductor emits on MIDI channels 20–24 (one function per channel):

| Channel | CC | Function | Trigger |
|---:|---:|---|---|
| 20 | 20 | Scene | 0=Intro, 32=Develop, 64=Break, 96=Reprise, 127=Coda |
| 21 | 21 | Density | 0=sparse … 127=full |
| 22 | 22 | Energy | 0=low … 127=high |
| 23 | 23 | Mutation | 0=stable … 127=unstable |
| 24 | 24 | Reset | Fires at 127 only |

### Wiring rules

- Connect Conductor MIDI out → receiver MIDI in
- On the receiver, set `conductor_ch` param to match Conductor's output channel (0 = off)
- Compatible receivers: BassGen, Ground, DrumGen, Harmonic-Atlas
- Conductor is `recommendedBefore` Harmonic-Atlas in the signal chain

### Section sequence

Conductor advances through sections bar-by-bar according to its length settings:
`Intro → Develop → Break → Reprise → Coda → (loops back)`

---

## Other Control Sources

| Plugin | CCs emitted | Typical target |
|---|---|---|
| Drift | Configurable (4 lanes, user-assigned CC numbers) | Any CC-responsive plugin |
| Oracle | Configurable mapped CCs from audio/MIDI analysis | Any CC-responsive plugin |
| GremlinDriver | Gremlin-specific action/modulation CCs | Gremlin only |
