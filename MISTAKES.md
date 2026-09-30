# Mistakes

Kept short: recurring patterns only. One-offs and fixed-and-forgotten entries removed 2026-09-30.

## NAPI: typeof-check any receiver that can be undefined/null

`loadProject` on a graph without top-level `metadata` threw a bare V8 `TypeError`
instead of loading cleanly: `napi_get_named_property` on an absent property leaves a
pending exception that slips past every `!= napi_ok` status check to the call boundary.
Fixed with a `napi_typeof` + object guard in `native/src/napi_bridge.cpp`.

**Rule:** any NAPI read chain touching a possibly-undefined value (`argv[i]` past
`argc`, optional objects, array elements) typeof-checks before the next property
access. Status checks alone do not catch it.

## Format version bumps are reader + writer + every baked-in binary

Two instances of the same break: the v7→v8 bump changed the interchange writer but not
the reader (a scripted replacement whose pattern never matched — unchecked), and the
v8→v9 bump rebuilt the tests but not the prebuilt `*_probe`/`*_inspect`/UI binaries,
which bake the codec in at build time. Both surfaced as "invalid native UI project
interchange at line 1".

**Rule:** a version bump edits reader and writer in the same change, asserts scripted
patterns matched, rebuilds all native targets linking the codec
(`grep -rl UiProjectCodec native/src/*_main.cpp`), and ends with one end-to-end probe
plus `npx vitest run tests/rdf/NativeUiProject.test.js`.

## A field whose meaning depends on kind/encoding: grep every reader

Two instances: `napi_bridge.cpp` read `settings.pluginPath` by short key while
`TransmissionRdf.js` stores full-URI keys with array values (every VST3 node silently
became a PassThrough), and the double-click handler dispatched a `:JigdawPlugin` node
on `!pluginPath.empty()` and opened the VST3 editor on an IRI.

**Rule:** when a field is reused for a new kind or a new key format lands in the JS
layer, grep every reader (`grep -n "pluginPath" native/src/*.cpp`) and make each one
state which kinds/keys it is for. Check the C++ N-API consumers with every RDF
property change.

## edit tool: keep line boundaries intact

Repeated fused-line edits from `newString` dropping the source's trailing newline when
`oldString` ended at a line boundary. Harmless when the compiler catches it; twice it
didn't.

**Rule:** no functional purpose, no edit. When `oldString` ends at a line boundary,
`newString` must too — check the last character before calling, re-read the region
after.
