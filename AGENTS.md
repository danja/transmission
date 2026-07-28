# Agent Guidance

## Mission

Build Transmission as a maintainable Linux VST3 host with a Node.js control plane and a C++ real-time engine. Keep project persistence, graph editing, plugin control, and audio processing separate.

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

Before editing:

1. Inspect the relevant model, bridge, compiler, and test contracts.
2. Identify thread-affinity and ownership constraints.
3. Keep the change within the smallest affected subsystem.

When implementing:

1. Add or update the public interface first.
2. Add focused unit tests for valid, invalid, and failure cases.
3. Keep native and Node responsibilities explicit.
4. Document breaking changes and migration implications.

Before handoff, run the narrowest relevant tests, a Node type check or build, and a native configure/build check when native code changed. Report any environment-dependent tests separately.

## Native UI screenshot workflow

When visually checking the GTK UI on the host desktop, use the active X11 session rather than the sandbox display:

```sh
DISPLAY=:0 XAUTHORITY=/run/user/1000/gdm/Xauthority native/build-ui/transmission_graph_ui
wmctrl -lG
DISPLAY=:0 XAUTHORITY=/run/user/1000/gdm/Xauthority import -window <window-id> /tmp/transmission-ui.png
```

Keep the UI running in a terminal session while iterating. Inspect the captured PNG with the image viewer tool. `gnome-screenshot` can capture the whole desktop, but ImageMagick `import -window` is more reliable for the specific GTK window.

## VST3 and licensing

Use `/chalet/VST_SDK/vst3sdk` as the local SDK reference. Check the SDK licensing and redistribution requirements before packaging or distributing binaries.
