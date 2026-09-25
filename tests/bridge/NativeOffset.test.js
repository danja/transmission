import { existsSync } from 'node:fs'
import { homedir } from 'node:os'
import { join, dirname } from 'node:path'
import { createRequire } from 'node:module'
import { fileURLToPath } from 'node:url'
import { describe, expect, it } from 'vitest'

const require = createRequire(import.meta.url)
const repositoryRoot = join(dirname(fileURLToPath(import.meta.url)), '..', '..')
const addonPath = join(repositoryRoot, 'native/build-napi-vst3/transmission_native.node')
const bassgen = join(homedir(), '.vst3/bassgen.vst3')

// Exercises the real NAPI offset path: deviceless engines still run the live
// enqueue branch, so bounds apply without JACK or audio hardware.
const available = existsSync(addonPath) && existsSync(bassgen)

describe.skipIf(!available)('sample-offset delivery through the native addon', () => {
  it('accepts in-block offsets and rejects the rest', () => {
    const addon = require(addonPath)
    const TRN = 'http://purl.org/stuff/transmissions/'
    addon.createEngine({})
    addon.loadProject({
      nodes: [{
        id: 'gen', type: `${TRN}VST3Plugin`,
        ports: { audioOutputs: 2, midiOutputs: 1 },
        settings: { pluginPath: bassgen }
      }],
      connections: [],
      metadata: {}
    })
    addon.startAudio()
    try {
      expect(() => addon.setParameter('gen', 0, 0.5, 5)).not.toThrow()
      expect(() => addon.setParameter('gen', 0, 0.5, 1023)).not.toThrow()
      expect(() => addon.setParameter('gen', 0, 0.5, 1024)).toThrow(/out of range/)
      expect(() => addon.setParameter('gen', 0, 0.5, 5000)).toThrow(/out of range/)
      expect(() => addon.setParameter('gen', 0, 0.5, 'x')).toThrow(/non-negative integer/)
    } finally {
      addon.stopAudio()
      addon.disposeEngine()
    }
  })
})
