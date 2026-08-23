---
layout: default
title: Plugin System
nav_order: 6
---

# RDF plugin catalogue

Transmission keeps two complementary kinds of plugin knowledge:

- discovered VST3 evidence: class identity, bundle path, vendor/category, audio
  and MIDI topology, and parameter metadata;
- curated profiles: musical roles, meaningful signal types, requirements,
  pairings, genres, descriptions, and cautions.

Both are available as RDF Turtle. Discovery remains authoritative for technical
facts. Curated profiles remain authoritative for behavioural signal semantics.
This distinction matters for plugins such as DrumGen, whose two audio output
channels are intentionally silent compatibility outputs while its meaningful
output is MIDI.

The built-in Downspout profile pack is
[`profiles/downspout.ttl`](../profiles/downspout.ttl). Additional profile files
can be supplied by repeating `--plugin-profile FILE`.

## Profile shape

Profiles use the existing `http://purl.org/stuff/transmissions/` vocabulary:

```turtle
@prefix trn: <http://purl.org/stuff/transmissions/> .
@prefix dsp: <http://purl.org/stuff/transmissions/plugins/downspout/> .
@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .

dsp:drumgen a trn:PluginProfile ;
    rdfs:label "DrumGen" ;
    rdfs:comment "Transport- and meter-aware drum generator." ;
    trn:bundleName "drumgen.vst3" ;
    trn:vendor "danja" ;
    trn:role trn:MidiGenerator ;
    trn:produces trn:Midi, trn:DrumMidi ;
    trn:requires trn:HostTransport ;
    trn:recommendedBefore dsp:drumkit ;
    trn:caution "Its audio output is intentionally silent." .
```

Every profile needs an `rdfs:label` plus either `trn:bundleName` or
`trn:vstClassId`. Class ID is the strongest identity. Bundle name permits
source-controlled profiles to match builds before their platform-specific
class ID has been scanned.

Supported curated properties are:

| Property | Meaning |
| --- | --- |
| `trn:role` | Musical or processing role such as `trn:Instrument` |
| `trn:produces` | Meaningful output such as `trn:Audio` or `trn:DrumMidi` |
| `trn:accepts` | Meaningful input accepted by the plugin |
| `trn:requires` | Host or hardware requirement |
| `trn:recommendedBefore` | Recommended downstream plugin profile |
| `trn:recommendedAfter` | Recommended upstream plugin profile |
| `trn:companion` | Related plugin without imposing ordering |
| `trn:genre` | Supported or characteristic musical vocabulary |
| `trn:caution` | Operational or musical warning |
| `trn:homepage` | Human-facing documentation |

Semantic role and signal objects may use project-specific IRIs. Transmission
compacts objects in its own namespace to readable values in MCP responses.

## Discovery

The MCP server searches `~/.vst3` by default. Each bundle is inspected in a
separate bounded subprocess so a failed plugin does not take down the MCP
server. Worker count follows the machine's available parallelism and is capped
at eight.

The inspector records:

- processor class ID, name, vendor, category, bundle, and module path;
- audio channel and MIDI/event bus counts;
- inferred technical roles and signal types;
- parameter ID, title, short title, units, normalized default, step count, and
  VST3 parameter flags.

Use `--plugin-root DIR` to replace the default search root,
`--vst3-inspector FILE` to select an inspector build, or `--no-plugin-scan` to
start from curated profiles only.

## MCP access

Resources:

- `transmission://plugins` — merged JSON catalogue;
- `transmission://plugins/profiles` — curated RDF Turtle;
- `transmission://plugins/discovered` — discovered RDF Turtle evidence.

Tools:

- `plugins_list`
- `plugins_search`
- `plugin_describe`
- `plugin_validate_chain`
- `plugins_scan`

Search is deliberately demand-driven. The model can find candidates using
roles and signal semantics, then request a full description only for relevant
plugins. Chain validation checks adjacent meaningful signal types and reports
curated pairings and cautions.

