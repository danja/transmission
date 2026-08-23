# Crocotta

[Transmission test : Crocotta](https://youtube.com/shorts/4fgfn_XeoXo) on YouTube

## Initial Prompt

The task is to create a piece of generative music representing the crocotta, a mythical beast. This will be achieved by first reading about the characteristics of this creature, comparing them with all the plugins available in Downspout and selecting those which might be considered a potential match on a very abstract level. There will also need to be a Conductor plugin and as many of the midi generating plugins as necessary, together with instruments and effects. The piece should have a beginning, middle and end. Include in the plugins at least one instance of Campione. 
Once the topology of the system has been  determined, call MCP tools in the running instance of Transmission to build an implementation. When it comes to the Campione instance(s) use their MCP tools to load appropriate sounds from the ~/Music/samples dir.
If manual intervention is needed at any stage, let me know.

## Procedure

### 1. Research and plugin selection

The crocotta is described in classical sources as a hybrid predator capable of imitating the human voice to lure prey — part hyena, part lion, part human. Its defining traits are mimicry, hybridity, and an unsettling vocal quality. These were mapped abstractly to available Downspout plugins:

| Trait | Plugin(s) | Rationale |
|-------|-----------|-----------|
| Animal voice mimicry | Campione | Loaded with real animal samples (laughing, throat, woo, wolf, waver, screech) triggered as a sampler |
| Chaotic, unpredictable nature | Gremlin + GremlinDriver | Chaotic glitch instrument with scene/macro randomisation driven by its companion modulator |
| Avian/strange vocal texture | Syrinx | Polyphonic avian vocal synth using biomechanical ODE models |
| Hybrid, complex timbre | Floozy | Eight-voice hybrid physical/modulation synthesizer |
| Bass pulse / ground presence | Ground → Basilico | Long-form bass generator feeding a monophonic bass instrument |
| Rhythmic, cyclic hunting pattern | Polymeter + DrumGen | Euclidean rhythms (Polymeter driving Floozy) and drum patterns triggering Campione |
| Long-form arc (beginning/middle/end) | Conductor | Section generator emitting scene, density, energy, and mutation CCs |
| Melodic phrase memory and mutation | MelGen → Mnemosyne | Phrase generator feeding a capture-and-recombine memory processor |
| Disruption and stuttering | Rift | Buffer disruptor applied to Gremlin's output |
| Mix | T-Mix | Eight-input mixer combining all audio streams |
| Ambience | Lightverb | Minimal FDN reverb on the master bus |
| Output protection | Guardian | DC removal, limiting, and true-peak protection |

### 2. Graph construction

The graph was built using the Transmission MCP tools (`node_add`, `connection_add`, `graph_apply_changes`) against a running Transmission instance. Nodes were added in topological order, then connections wired.

**MIDI routing:**

```
Conductor ──► Ground ──► Basilico
          │         └──► MelGen ──► Mnemosyne ──► Syrinx
          └──► DrumGen ──► Campione
Polymeter ──► Floozy
GremlinDriver ──► Gremlin
```

**Audio routing:**

```
Campione ──► T-Mix (ch 0–1)
Syrinx   ──► T-Mix (ch 2–3)
Basilico ──► T-Mix (ch 4)
Floozy   ──► T-Mix (ch 5)
Gremlin  ──► Rift ──► T-Mix (ch 6–7)
T-Mix ──► Lightverb ──► Guardian ──► Audio Out
```

### 3. Loading animal sounds into Campione

With the graph loaded and Campione initialised, its MCP server (JSON-RPC 2.0, port 7220) was used to load animal samples from `~/Music/samples/animals/`:

- `laughing.wav` — MIDI range 0–47, root 36
- `throt.wav` — MIDI range 48–59, root 48
- `woo.wav` — MIDI range 60–71, root 60
- `wolf.wav` — MIDI range 72–83, root 72
- `waver.wav` — MIDI range 84–95, root 84
- `screech.wav` — MIDI range 96–127, root 96

The resulting zone layout was saved as `/home/danny/.vst3/campione-data/crocotta-animals.ttl`.

### 4. Configuring Ground and DrumGen

Both plugins were left at defaults during initial graph construction and subsequently configured to match the crocotta's character by editing the `componentState` blobs in `crocotta.ttl`.

**Ground** — bass generator feeding Basilico:

| Parameter | Value | Rationale |
|-----------|-------|-----------|
| scale | Phrygian (3) | Flattened 2nd gives an ominous, predatory quality |
| style | Dub (6) | Sparse, heavy, off-beat — the patience of a stalking hunter |
| form_shape | Dub16 (10) | 16-bar dub arc reinforcing the character |
| tension | 0.75 | High sustained menace (up from default 0.45) |
| motion | 0.65 | More movement, prowling quality (up from 0.55) |
| cadence | 0.25 | Unresolved, suspended; never settles (down from 0.50) |
| color | 0.60 | Richer harmonic inflection (up from 0.00) |
| vary | 35 | Shapeshifts over time, evoking the crocotta's mimicry (up from 0) |
| seed | 7 | Forces a fresh pattern generation with the new settings |

**DrumGen** — rhythm generator feeding Campione:

| Parameter | Value | Rationale |
|-----------|-------|-----------|
| genre | Breakbeat (8) | Syncopated, unpredictable; replaces straight Rock default |
| variation | 0.75 | High pattern variation, chaotic and hybrid (up from 0.35) |
| density | 0.65 | Denser hits (up from 0.58) |
| fill | 0.45 | More frequent fills (up from 0.30) |
| vary | 40 | Auto-mutation over time (up from 0) |
| kickAmt | 0.55 | Weakened conventional kick (down from 0.78) |
| backbeatAmt | 0.40 | Weakened conventional backbeat (down from 0.76) |
| auxAmt | 0.60 | More auxiliary percussion (up from 0.28) |
| tomAmt | 0.65 | More toms — animalistic (up from 0.30) |
| metalAmt | 0.55 | More metallic sounds (up from 0.26) |
| seed | 7 | Forces a fresh breakbeat pattern with the new settings |