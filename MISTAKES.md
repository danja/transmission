# Mistakes

## napi_bridge.cpp — wrong settings key format for pluginPath

**What happened:** VST3 plugins loaded via the MCP/Node control path produced silence. BassGen and Basilico were silently replaced by PassThroughProcessors, so no MIDI or audio was generated.

**Root cause:** `napi_bridge.cpp` read `settings.pluginPath` using the short property name `"pluginPath"` as a string. But `TransmissionRdf.js`'s `settingsObject()` stores settings keyed by full URI (e.g. `"http://purl.org/stuff/transmissions/pluginPath"`) with array values. The key lookup always missed, leaving `pluginPath` empty, so every node fell through to `PassThroughProcessor`.

**Fix:** Added `readPluginPath()` helper in `napi_bridge.cpp` that tries the short key first, then falls back to the full URI key and extracts element 0 from the array value. Applied to both `loadProject` and `captureMidi`.

**Prevention:** When a new RDF property key format is introduced in the JS layer, immediately check all C++ N-API consumers that read the same field. Add a native integration test that loads a minimal project with a VST3 node and asserts the plugin path is non-empty after `loadProject`.
