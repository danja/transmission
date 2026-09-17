# Mistakes

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
