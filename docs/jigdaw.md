# JigDAW plugins

Transmission hosts [JigDAW](https://github.com/danja/jigdaw) plugins alongside VST3 ones. A
JigDAW plugin is a WebAssembly module named by a dereferenceable IRI: fetching the IRI returns
the plugin's profile in Turtle, the profile names the module and its integrity digest, and
there is no registry and no install step distinct from having fetched it.

```turtle
:pulse a :JigdawPlugin ;
    :settings [ :pluginIri "https://strandz.it/jigdaw/plugins/pulse/" ] .
```

That is the whole of adding one to a project.

## What is implemented

Both published module ABIs, through jigdaw's own `jigdaw_core` and its wasm3 runtime:

- **`jig:Abi1`** — audio in and out, parameters by `jig:paramIndex`, note on and note off.
- **`jig:Abi2`** — frame-stamped MIDI in and out, the 64-byte host transport block, and
  plugins with no audio at all.

A plugin whose module declares no `jig:abi` is refused with a message saying it is private to
its JavaScript processor. That is the specification's position, not a shortcoming of this
host: without a declared ABI there is nothing for a native host to call.

## Building

JigDAW hosting is off by default and needs the jigdaw checkout plus cpp-httplib:

```sh
cmake -S native -B native/build \
  -DTRANSMISSION_WITH_JIGDAW=ON \
  -DJIGDAW_ROOT="$HOME/github/jigdaw" \
  -DJIGDAW_HTTPLIB_DIR="$HOME/github/downspout/third_party/cpp-httplib"
cmake --build native/build
```

`./build.sh` turns it on automatically when both are present and says which it did. wasm3 is
fetched by jigdaw's own CMake the first time.

## Inspecting a plugin

`transmission_jigdaw_inspect` is the JigDAW counterpart of `transmission_vst3_inspect`, and
prints the same key names so the same greps work:

```sh
native/build/transmission_jigdaw_inspect https://strandz.it/jigdaw/plugins/pulse/ \
  | grep -E 'audioInputs|audioOutputs|midiInputs|midiOutputs'
```

`transmission_jigdaw_probe` renders one plugin offline, with no device and no display:

```sh
native/build/transmission_jigdaw_probe https://strandz.it/jigdaw/plugins/pulse/ --note 60
native/build/transmission_jigdaw_probe https://strandz.it/jigdaw/plugins/bassgen/ --bpm 128
```

Over MCP, `jigdaw_describe` returns the profile and the graph node to add for it, including
the real port counts. Call it before wiring anything: the host reads the same profile and
overwrites whatever a project declared, so a project that guessed fails validation.

## The parameter panel

A JigDAW plugin has no editor a native host can open: `jig:ui` is a web page and there is no
JavaScript engine here. Double-clicking a JigDAW node instead opens a panel generated from
the profile's own `lv2:port` declarations, which is the same panel the browser host and
jigdaw's adapter draw, from the same statements. The widget follows the shape of the
declaration and is never named by the author:

| Declaration | Control |
|---|---|
| `lv2:portProperty lv2:toggled` | switch |
| an enumeration with scale points | selector, every option named |
| anything else | slider over the declared range |

Each control is labelled with its `jig:paramIndex`, because that is what everything else
addresses the parameter by: the project file, MCP, and `jig_set_param` itself. Applying
writes normalised values into the project and, when audio is running, straight into the
plugin.

## Local plugins during development

A `file://` IRI is dereferenced like any other, and one naming a directory reads `profile.ttl`
inside it — the shape an http plugin IRI has, where content negotiation supplies the profile:

```
file:///home/danny/github/jigdaw/plugins/pulse/
```

`projects/patches/jigdaw-pulse.ttl` is a worked patch using two of these: BassGen generating
MIDI from the transport into Pulse. Swapping the two IRIs for their `https://strandz.it/…`
forms changes nothing else in the file.

## How a profile maps onto a transmission graph

| Profile | Graph |
|---|---|
| `jig:audioInputs > 0` | `audioInputs` = `jig:inputChannels` |
| `jig:audioOutputs > 0` | `audioOutputs` = `jig:outputChannels` |
| `trn:accepts trn:Midi` | one MIDI input |
| `trn:produces trn:Midi` | one MIDI output |
| `lv2:port` with `jig:paramIndex` | parameter `id`, addressed normalised 0..1 |

`jig:audioOutputs` counts Web Audio ports and `jig:outputChannels` counts the channels in
them. A transmission port is a channel, so the channel count is the one that matters.

A parameter's `id` is its declared `jig:paramIndex`, never its position in the document. A
normalised value is mapped onto the port's declared range, and a port that is `lv2:toggled`
or an enumeration is rounded to the nearest named value rather than left between two.

## The transport

A plugin that declares `trn:requires trn:HostTransport` gets the 64-byte block filled in
before every sub-block, with `bpm`, `beat`, `barStartBeat`, bar/beat/tick and the meter
marked valid. Transmission counts a bar as four beats everywhere and its timeline starts at
bar 1 beat 1, so that counting is the host's own rather than something derived behind the
module's back.

`seconds` is left invalid: this engine has a tempo map, so wall-clock time is not the current
tempo times the beat, and a host that does not have a field says so rather than guessing.

## Block sizes

A JigDAW module reports a maximum block through `jig_max_frames()`, and for the worked
plugins that is 128 — smaller than any device block size transmission runs at. Each block is
therefore processed in sub-blocks of at most that many frames, with incoming MIDI offsets
rebased into each sub-block, the transport advanced across them, and outgoing MIDI rebased
back onto the whole block.

## A note on locales

jigdaw's profile parser used `std::stof`, which reads the decimal separator from the global
C locale. GTK calls `setlocale(LC_ALL, "")`, so under a comma-decimal locale every
fractional `lv2:default`, `lv2:minimum` and `lv2:maximum` in every profile parsed as zero —
Pulse came up with its gain at 0. Fixed upstream in `jigdaw/native/jigdaw-adapter/src/Profile.cpp`;
a JigDAW checkout older than that will misread profiles inside the GTK editor while the
command-line tools, which never set a locale, look correct.

## Real-time behaviour

Everything that fetches, verifies, allocates or compiles happens in `initialize()` on the
control thread, including compiling every function in the module ahead of time. wasm3
otherwise compiles a function the first time it is called, which would put a compilation and
its allocations on the audio thread during the first block.

Parameter changes cross to the audio thread through a fixed-capacity lock-free slot array and
are written to the module only when the value has actually changed, because some plugins
retune on a parameter write.

## Limitations

- No state serialisation. `jig:Abi1` and `jig:Abi2` have none, so a project restores a
  JigDAW plugin's parameters but not any state it keeps beyond them.
- No plugin-supplied UI. `jig:ui` is a web page and this host has no JavaScript engine, so
  the panel is generated from the profile rather than fetched.
- No latency compensation, and no system exclusive. The module ABI carries no
  latency reporting; the profile's `jig:latencyFrames` is surfaced by
  inspection (`latencyFrames` in the topology and in `jigdaw_describe`) but
  nothing compensates it — the engine has no delay-compensation framework at
  all, so per-plugin compensation would be architecture, not a fix.
- Integrity is verified against the digest in the profile and there is no way to skip it, so
  a plugin whose module has been rebuilt without its profile being regenerated is refused.
