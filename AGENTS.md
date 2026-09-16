# Agent Guidance

## Mission

Build Transmission as a maintainable Linux VST3 host with a Node.js control plane and a C++ real-time engine. Keep project persistence, graph editing, plugin control, and audio processing separate.

## Song Generation

When asked to generate music and patches read /home/danny/github/downspout/README.agents.md if available. You should use MCP to communicate with a live Transmission instance.

## Non-negotiable real-time rules

- Never allocate, access the filesystem or network, log through an unbounded sink, call JavaScript, or take an unpredictable lock on the audio callback thread.
- Use preallocated buffers and bounded lock-free queues for callback communication.
- Transport advancement on the callback must use fixed-capacity storage; tempo-map edits belong on the control thread.
- Keep RDF parsing, graph compilation, plugin discovery, and project I/O outside the real-time path.
- Treat plugin code as untrusted: surface errors, isolate failures where practical, and make shutdown recoverable.

## Repository conventions

- Use ES modules in Node.js.
- Maintain TypeScript declaration files for public JavaScript interfaces.
- Use small modules with explicit dependencies and dependency injection.
- Use Vitest for Node tests, CTest for C++ tests, and CMake for native builds.
- Prefer deterministic offline audio tests before device-based tests.
- Add comments only where intent or an unusual library/API is not obvious.
- Keep editor metadata independent from execution metadata.
- Do not let the UI mutate the native runtime graph directly; changes must pass through the model and compiler.

## Change workflow

Add newly identified work items to `TODO.md`. Remove each item when its
implementation and verification are complete.

Before editing:

1. Inspect the relevant model, bridge, compiler, and test contracts.
2. Identify thread-affinity and ownership constraints.
3. Keep the change within the smallest affected subsystem.

When implementing:

1. Add or update the public interface first.
2. Add focused unit tests for valid, invalid, and failure cases.
3. Keep native and Node responsibilities explicit.
4. Document breaking changes and migration implications.
5. Log mistakes in MISTAKES.md (what happened, root cause, prevention).

Before handoff, run the narrowest relevant tests, a Node type check or build, and a native configure/build check when native code changed. Report any environment-dependent tests separately.

## Native UI screenshot workflow

When visually checking the GTK UI on the host desktop, use the active X11 session rather than the sandbox display:

```sh
DISPLAY=:0 XAUTHORITY=/run/user/1000/gdm/Xauthority native/build-ui/transmission_graph_ui
wmctrl -lG
DISPLAY=:0 XAUTHORITY=/run/user/1000/gdm/Xauthority import -window <window-id> /tmp/transmission-ui.png
```

Keep the UI running in a terminal session while iterating. Inspect the captured PNG with the image viewer tool. `gnome-screenshot` can capture the whole desktop, but ImageMagick `import -window` is more reliable for the specific GTK window.

## Patch authoring via MCP

When building a graph with `graph_apply_changes`, the port counts you declare on each node are
overwritten by the GTK UI's `applyProject`, which calls `Vst3Inspector::inspectTopology` to read
the real port counts from each plugin binary. `validateProject` then checks every connection
against those real counts. A mismatch causes a silent "apply failed" and the UI keeps its previous
graph.

Before wiring connections, verify real port counts with:

```sh
native/build-ui-jack-vst3/transmission_vst3_inspect /home/danny/.vst3/<name>.vst3 \
  | grep -E 'audioInputs|audioOutputs|midiInputs|midiOutputs'
```

Use only port indices that fall within those counts (0-based). Declaring a higher count on the
node has no effect — the inspector result wins.

The live-reload trigger relies on the server revision changing. After `project_new` (which resets
revision to 0), any sequence of edits that lands back on the same revision number the UI last saw
will not trigger a reload. Apply a no-op `setProjectMetadata` change to force a unique revision
if needed.

## Diagnosing audio problems

### No audio from a project

1. **Run the headless probe first** — confirms the graph initialises and plugins produce signal without needing JACK or a display:
   ```sh
   node scripts/probe-project.js projects/<name>.ttl
   ```
   Look for `AUDIO node=guardian totalRms=...` near the bottom. If the probe fails with "module path, output channels, frames, and sample rate must be valid", a plugin has `audioOutputs 0` in the TTL — add the correct `:audioOutputs N` declaration (check with `transmission_vst3_inspect`).

2. **Verify real port counts** before wiring connections:
   ```sh
   native/build-ui-jack-vst3/transmission_vst3_inspect /home/danny/.vst3/<name>.vst3 \
     | grep -E 'audioInputs|audioOutputs|midiInputs|midiOutputs'
   ```
   The GTK UI overwrites declared counts with inspector results. A connection to a port index outside the real count causes a silent "apply failed".

3. **Check JACK output port names** — port names with a numeric suffix (`UMC404HD 192k-89:...`) change every JACK session. Use the suffix-free stable form so fuzzy matching resolves it automatically:
   - Good: `"UMC404HD 192k:playback_FL"` — matches any `UMC404HD 192k-NN:playback_FL`
   - Bad: `"system:playback_1"` — does not exist in this JACK graph; fuzzy matching cannot help
   - Check available ports: `jack_lsp | grep playback`
   - Or via live server: `curl http://127.0.0.1:7878/jack-ports`

4. **GTK UI console commands** (type into the console text entry):
   - `lsp` — list all available JACK input/output ports
   - `connections` — show what `transmission:out_1/2` are currently wired to
   - `peaks` — show current output peak levels; `(silence — check connections)` means no audio is reaching the output
   - `status` — show runtime state plus last connection error if connections failed
   - `diag` — full engine diagnostics including timing, block counts, and last connection error
   - `reconnect` — retry JACK connections without restarting audio

5. **Live server diagnostics endpoints**:
   - `GET /diagnostics` — engine state, transport, peaks
   - `GET /peaks` — `{ peakL, peakR }` (both near zero → silence at output)
   - `GET /jack-ports` — available JACK playback/capture ports via `jack_lsp`

### Common TTL authoring mistakes

- Missing `:audioOutputs N` on a MIDI-only generator causes the probe and live engine to reject it.
- `canticle.vst3` has only 1 real MIDI input despite appearing to accept 2; connecting two sources to port 0 merges them.
- MIDI port indices are 0-based; two sources wired to the same `:toPort` silently clobber each other — use separate instruments or a dedicated MIDI router.

## VST3 and licensing

Use `/chalet/VST_SDK/vst3sdk` as the local SDK reference. Check the SDK licensing and redistribution requirements before packaging or distributing binaries.
