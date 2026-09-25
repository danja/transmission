# Mistakes

## napi_get_named_property on a possibly-undefined receiver leaks a V8 TypeError

**What happened:** `loadProject` on a compiled graph without top-level
`metadata` threw `TypeError: Cannot convert undefined or null to object`
with no message, instead of loading cleanly or failing with a named error.
Bisected to `readStringArray`'s `napi_get_named_property(env, argv[0],
"metadata", &meta)` followed by a read on `meta`: when the property is
absent V8 throws on the undefined receiver, and that pending exception slips
past every `!= napi_ok` status check to surface at the call boundary.

**Fix:** typeof-guard the receiver first (`napi_typeof` + object check, the
same shape as the file's own `getObject()`), in `native/src/napi_bridge.cpp`.

**Prevention:** Any NAPI read chain that touches a value which can be
undefined/null (optional object, array element, `argv[i]` past `argc`) must
typeof-check before the next property access. Status checks alone do not
catch a V8-thrown TypeError — it is already pending by the time the status
is read.

## Interchange version bump — rebuilt the tests but not every consumer binary

**What happened:** After bumping the interchange writer to v9 (with all
readers in source accepting 1–9), `scripts/probe-project.js` failed with
"invalid native UI project interchange at line 1" — the prebuilt
`transmission_vst3_project_probe` binary still embedded the v8-only codec.

**Root cause:** The existing version-bump entry covers reader+writer in
source, but native helper binaries (`*_probe`, `*_inspect`, the UI itself)
bake the codec in at build time. A format change is not done when the
source is consistent; it is done when every shipped binary is rebuilt.
Rebuilding the probe fixed it immediately.

**Prevention:** A version bump ends with rebuilding all native targets that
link the codec and re-running one end-to-end probe, not just the unit
harness. `grep -rl UiProjectCodec native/src/*_main.cpp` lists the
consumers to rebuild.

## edit tool — newString dropping the trailing newline glues two lines

**What happened:** Four times across sessions, an `edit` whose `newString`
dropped the source's trailing newline fused two lines into one (e.g.
`})` + `function connectionMatches...` on one line, `it(...)` + `const
control...` on one line). Twice the fused line was still valid syntax, so
only a re-read of the edited region caught it; once the compiler caught it.

**Root cause:** Issuing a second "paired" edit with no real purpose (a no-op
whitespace touch-up, or an `oldString` ending at a line boundary while the
`newString` does not reproduce it). The tool does exactly what it is told:
byte replacement, no line-structure awareness.

**Prevention:** Never issue an edit without a functional purpose. When
`oldString` ends at a line boundary, `newString` must end at one too —
check the last character before calling. Always re-read the edited region
afterwards (this caught every occurrence so far).

## napi_bridge.cpp — wrong settings key format for pluginPath

**What happened:** VST3 plugins loaded via the MCP/Node control path produced silence. BassGen and Basilico were silently replaced by PassThroughProcessors, so no MIDI or audio was generated.

**Root cause:** `napi_bridge.cpp` read `settings.pluginPath` using the short property name `"pluginPath"` as a string. But `TransmissionRdf.js`'s `settingsObject()` stores settings keyed by full URI (e.g. `"http://purl.org/stuff/transmissions/pluginPath"`) with array values. The key lookup always missed, leaving `pluginPath` empty, so every node fell through to `PassThroughProcessor`.

**Fix:** Added `readPluginPath()` helper in `napi_bridge.cpp` that tries the short key first, then falls back to the full URI key and extracts element 0 from the array value. Applied to both `loadProject` and `captureMidi`.

**Prevention:** When a new RDF property key format is introduced in the JS layer, immediately check all C++ N-API consumers that read the same field. Add a native integration test that loads a minimal project with a VST3 node and asserts the plugin path is non-empty after `loadProject`.

## UiProjectCodec.cpp — version bump applied to the writer but not the reader

**What happened:** The native UI interchange version was raised from 7 to 8 to carry the new
`JigdawPlugin` node kind. The writer was changed, the reader was not, and every project the
Node helper produced was then rejected by every native binary with "invalid native UI project
interchange at line 1" — a message about line 1 for a change made everywhere else.

**Root cause:** The edit was made with a scripted string replacement whose pattern did not
match the file's actual indentation, and the script did not check that it had replaced
anything. Two of the three edits in the same run did apply, so the file looked changed.

**Fix:** Widened the reader's accepted version list. The accepted versions and the written
version are three lines apart in the same file and should be read together whenever either
moves.

**Prevention:** A scripted edit asserts that its pattern matched before writing the file. A
format version is a reader and a writer, and changing one without the other is not a partial
change, it is a break: bump both in the same edit and run a round trip afterwards. The round
trip here is `npx vitest run tests/rdf/NativeUiProject.test.js`, which asserts the version
string in both directions and would have caught it.

## native_graph_ui_main.cpp — a new node kind reused pluginPath and inherited its readers

**What happened:** Double-clicking a `:JigdawPlugin` node opened the VST3 editor, which
reported "https://strandz.it/jigdaw/plugins/pulse/ is not a module directory".

**Root cause:** A JigDAW node stores its plugin IRI in `Node::pluginPath`, the same slot a
VST3 node stores its bundle path in, because the UI interchange record carries one resource
per node. The double-click handler dispatched on `!node->pluginPath.empty()` rather than on
the node's kind, so the new kind silently inherited a branch written for a different one.

**Fix:** Dispatch on `kind == NodeKind::Plugin`, and give JigDAW nodes their own branch —
a parameter panel generated from the profile's `lv2:port` declarations, since a JigDAW
plugin has no editor a native host can open.

**Prevention:** This is the same shape as the `pluginPath` entry above: a field whose
meaning depends on the node kind, read by code that does not check the kind. When a field is
reused for a new kind, grep every reader of that field and make each one state which kinds it
is for. `grep -n "pluginPath" native/src/*.cpp` was the whole audit and it was not done.
