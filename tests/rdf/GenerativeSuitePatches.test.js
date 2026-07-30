import { basename } from 'node:path'
import { describe, expect, it } from 'vitest'
import { ProjectSession } from '../../src/session/ProjectSession.js'

const patchUrls = [
  new URL('../../patches/generative-suite-harmony.ttl', import.meta.url),
  new URL('../../patches/generative-suite-rhythm-memory.ttl', import.meta.url),
  new URL('../../patches/generative-suite-reactive.ttl', import.meta.url),
  new URL('../../patches/generative-suite-mosaic.ttl', import.meta.url)
]

const expectedBundles = [
  'conductor.vst3',
  'drift.vst3',
  'guardian.vst3',
  'harmonic_atlas.vst3',
  'mnemosyne.vst3',
  'mosaic.vst3',
  'oracle.vst3',
  'orbit.vst3',
  'polymeter.vst3',
  'resonance_garden.vst3'
]
const pluginPathSetting =
  'http://purl.org/stuff/transmissions/pluginPath'

describe('generative-suite test patches', () => {
  it('loads every topology and covers all new plugin bundles', async () => {
    const covered = new Set()
    for (const url of patchUrls) {
      const session = await ProjectSession.load(url.pathname)
      expect(session.graph.nodes.size).toBeGreaterThan(3)
      for (const node of session.graph.nodes.values()) {
        const pluginPath = node.settings[pluginPathSetting]?.[0]
        if (pluginPath) covered.add(basename(pluginPath))
      }
    }
    expect([...covered].filter(bundle => expectedBundles.includes(bundle)).sort())
      .toEqual(expectedBundles)
  })

  it('places Guardian immediately before both System Output channels', async () => {
    for (const url of patchUrls) {
      const session = await ProjectSession.load(url.pathname)
      const output = [...session.graph.nodes.values()]
        .find(node => node.label === 'System Output')
      const guardian = [...session.graph.nodes.values()]
        .find(node => node.label === 'Guardian')
      expect(output, url.pathname).toBeDefined()
      expect(guardian, url.pathname).toBeDefined()
      const safetyConnections = session.graph.connections.filter(connection =>
        connection.kind === 'audio' &&
        connection.from === guardian.id &&
        connection.to === output.id)
      expect(safetyConnections.map(connection => [
        connection.fromPort, connection.toPort
      ])).toEqual([[0, 0], [1, 1]])
    }
  })
})
