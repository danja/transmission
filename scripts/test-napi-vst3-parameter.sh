#!/usr/bin/env bash
set -euo pipefail

plugin_path=${1:?usage: scripts/test-napi-vst3-parameter.sh /path/to/Plugin.vst3}
addon_path=${TRANSMISSION_NAPI_ADDON:-native/build-napi-vst3/transmission_native.node}

if [[ ! -e "$plugin_path" ]]; then
  echo "VST3 bundle not found: $plugin_path" >&2
  exit 1
fi
if [[ ! -e "$addon_path" ]]; then
  echo "N-API addon not found: $addon_path" >&2
  echo "Build it with -DTRANSMISSION_WITH_NAPI=ON -DTRANSMISSION_WITH_VST3=ON" >&2
  exit 1
fi

node - "$plugin_path" "$addon_path" <<'NODE'
const { createRequire } = require('node:module')
const requireFromHere = createRequire(process.cwd() + '/package.json')
const pluginPath = process.argv[2]
const addonPath = process.argv[3]
const native = requireFromHere('./' + addonPath)

native.createEngine({ channels: 2, blockSize: 512, sampleRate: 48000 })
native.loadProject({
  nodes: [
    { id: 'input' },
    { id: 'fx', settings: { pluginPath } },
    { id: 'output' },
  ],
  connections: [
    { kind: 'audio', from: 'input', to: 'fx' },
    { kind: 'audio', from: 'fx', to: 'output' },
  ],
})
native.setParameter('fx', '1', 1.0, 0)
const diagnostics = native.getDiagnostics()
if (!diagnostics.graphLoaded || diagnostics.running) {
  throw new Error('parameter smoke test left the native engine in an invalid state')
}
console.log(`VST3 parameter test passed: graphLoaded=${diagnostics.graphLoaded}`)
native.disposeEngine()
NODE
