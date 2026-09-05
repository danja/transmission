---
layout: default
title: DJ Set Lists
nav_order: 7
---

# DJ Set Lists

Transmission supports autonomous DJ performance via a Turtle set list format.
A set list describes an ordered sequence of patches to load and play, with
timing and transition metadata. A runner script executes the set list against
the live server without requiring an AI agent to be present at runtime.

## Vocabulary

Set list terms live in [`vocabs/djset.ttl`](../vocabs/djset.ttl) under the
`http://purl.org/stuff/transmissions/` namespace (prefix `trn:`). The vocabulary
reuses transport and action terms from [`vocabs/actions.ttl`](../vocabs/actions.ttl)
— no terms are duplicated.

### Classes

| Class | Description |
|---|---|
| `trn:DJSet` | A complete performance: metadata plus an ordered list of cues. |
| `trn:Cue` | One slot in the set: a patch file, a duration, and a transition. |
| `trn:Transition` | Abstract base for transition types. |
| `trn:HardCut` | Stop the current cue, then immediately load and start the next. |
| `trn:Crossfade` | Fade out the outgoing cue while the incoming one fades in. |
| `trn:ParameterChange` | A timed automation step applied at a beat offset within a cue. |

### DJSet properties

| Property | Range | Description |
|---|---|---|
| `trn:cues` | `rdf:List` of `trn:Cue` | Ordered cue sequence. |
| `trn:artist` | `xsd:string` | Performer name. |
| `trn:setDate` | `xsd:date` | Date of the performance. |
| `trn:venue` | `xsd:string` | Venue name. |
| `trn:genre` | `xsd:string` | Genre description (from `profile.ttl`). |

### Cue properties

| Property | Range | Description |
|---|---|---|
| `trn:filePath` | `xsd:string` | Path to the Turtle patch, relative to the projects root. |
| `trn:durationBeats` | `xsd:integer` | Beats to play before advancing. 0 means play indefinitely. |
| `trn:tempo` | `xsd:decimal` | BPM override; absent means use the patch default. |
| `trn:transition` | `trn:Transition` | How to move to the next cue. Defaults to `trn:HardCut`. |
| `trn:onLoad` | `rdf:List` of `trn:ParameterChange` | Parameter changes applied when the patch loads. |
| `trn:scheduled` | `rdf:List` of `trn:ParameterChange` | Timed parameter changes during the cue. |

### Transition properties

| Property | Domain | Description |
|---|---|---|
| `trn:fadeBeats` | `trn:Crossfade` | Duration of the crossfade in beats. |

### ParameterChange properties

Reuses terms from `actions.ttl`:

| Property | Description |
|---|---|
| `trn:atBeat` | Beat offset from cue start at which to apply the change. 0 = at load time. |
| `trn:targetNode` | Node identifier within the patch (local name from the project Turtle). |
| `trn:parameterId` | VST3 parameter index on the target node. |
| `trn:normalizedValue` | Target value in [0, 1]. |

## Composing a set list

Create a Turtle file in `projects/setlists/`. Declare a `trn:DJSet` with a
`trn:cues` list. Patch paths are resolved relative to the `projects/` directory.

```turtle
@prefix :    <http://purl.org/stuff/transmissions/setlists/my-set/> .
@prefix trn: <http://purl.org/stuff/transmissions/> .
@prefix rdf: <http://www.w3.org/1999/02/22-rdf-syntax-ns#> .
@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .
@prefix xsd:  <http://www.w3.org/2001/XMLSchema#> .

:set a trn:DJSet ;
    rdfs:label "My Set" ;
    trn:artist "Claude" ;
    trn:setDate "2026-09-05"^^xsd:date ;
    trn:cues ( :cue-a :cue-b ) .

:cue-a a trn:Cue ;
    rdfs:label "Opening" ;
    trn:filePath "patches/nocturnal-drift.ttl" ;
    trn:durationBeats 128 ;
    trn:transition [ a trn:HardCut ] .

:cue-b a trn:Cue ;
    rdfs:label "Peak" ;
    trn:filePath "patches/hardcore-techno-160.ttl" ;
    trn:durationBeats 0 .
```

`durationBeats 0` in the last cue means the runner plays it until interrupted.

### Adding parameter changes

Use `trn:onLoad` for settings to apply immediately when the patch loads, and
`trn:scheduled` for changes at specific beat positions:

```turtle
:cue-a a trn:Cue ;
    trn:filePath "patches/live-dj.ttl" ;
    trn:durationBeats 256 ;
    trn:onLoad (
        [ trn:targetNode "drumgen" ; trn:parameterId 2 ; trn:normalizedValue 0.8 ]
    ) ;
    trn:scheduled (
        [ trn:atBeat 128 ; trn:targetNode "drumgen" ; trn:parameterId 2 ; trn:normalizedValue 0.4 ]
    ) .
```

Parameter IDs are VST3 indices. Use the `mcp__transmission__plugin_describe` MCP
tool to look up parameter names and indices for a given plugin.

## Running a set list

`scripts/dj-runner.js` executes a set list against the live server:

```sh
node scripts/dj-runner.js projects/setlists/rise-to-techno.ttl
```

Options:

| Flag | Default | Description |
|---|---|---|
| `--base <dir>` | `projects/` | Root directory for resolving patch file paths. |
| `--server <url>` | `http://localhost:7878` | Live server URL. |

The runner requires the Transmission live server to be running. See the
[live server setup](mcp-live.md#live-mcp-setup-prerequisites) for how to start it.

For each cue the runner:

1. Stops the current patch (if playing).
2. Opens the patch file via `POST /projects/open`.
3. Reads the patch's own tempo from the server status response.
4. Applies any `trn:onLoad` parameter changes.
5. Starts playback via `POST /transport/play`.
6. Waits for `durationBeats / bpm * 60` seconds.
7. Applies any `trn:scheduled` parameter changes at their beat offsets.
8. Advances to the next cue.

A `durationBeats` of 0 blocks until the process is interrupted (Ctrl-C).

## Example set lists

[`projects/setlists/rise-to-techno.ttl`](../projects/setlists/rise-to-techno.ttl)
— A 10-cue arc from crystal ambient (54 BPM) through organic grooves to
hardcore techno (160 BPM).

## Output port names

All patches must use the stable JACK port names for the main stereo output:

```turtle
:main trn:systemOutputConnections ( "UMC404HD 192k:playback_FL" "UMC404HD 192k:playback_FR" ) .
```

Session-numbered variants (`UMC404HD 192k-NNN:`) and AUX ports are not stable
across JACK restarts.
