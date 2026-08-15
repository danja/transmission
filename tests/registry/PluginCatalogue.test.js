import { fileURLToPath } from 'node:url'
import { describe, expect, it } from 'vitest'
import { PluginCatalogue } from '../../src/registry/PluginCatalogue.js'
import { parseTurtle } from '../../src/rdf/TransmissionRdf.js'

const downspoutProfile = fileURLToPath(new URL('../../profiles/downspout.ttl', import.meta.url))

describe('PluginCatalogue', () => {
  it('loads Downspout RDF profiles and merges authoritative semantics with discovered facts', async () => {
    const catalogue = new PluginCatalogue()
    expect(await catalogue.loadProfileFile(downspoutProfile)).toBeGreaterThan(30)
    catalogue.replaceDiscoveries([{
      classId: 'drumgen-class',
      name: 'DrumGen',
      vendor: 'danja',
      category: 'Audio Module Class',
      modulePath: '/plugins/drumgen.vst3',
      bundleName: 'drumgen.vst3',
      audioInputs: 0,
      audioOutputs: 2,
      midiInputs: 0,
      midiOutputs: 1,
      roles: ['MidiGenerator'],
      produces: ['Midi', 'Audio'],
      accepts: [],
      parameters: [{ id: 3, title: 'Genre', defaultNormalized: 0 }]
    }])
    const drumgen = catalogue.get('drumgen.vst3')
    expect(drumgen).toMatchObject({
      installed: true,
      classId: 'drumgen-class',
      audioOutputs: 2,
      produces: ['Midi', 'DrumMidi'],
      technicalProduces: ['Midi', 'Audio']
    })
    expect(drumgen.parameters[0].title).toBe('Genre')
    const turtle = catalogue.discoveredTurtle()
    expect(turtle).toContain('<urn:vst3:drumgen-class> a trn:DiscoveredPlugin')
    expect((await parseTurtle(turtle)).size).toBeGreaterThan(15)
  })

  it('searches semantic capabilities and validates curated chains', async () => {
    const catalogue = new PluginCatalogue()
    await catalogue.loadProfileFile(downspoutProfile)
    expect(catalogue.search({ produces: ['BassMidi'] }).map(plugin => plugin.name))
      .toEqual(['BassGen', 'Ground'])
    const validation = catalogue.validateChain([
      'http://purl.org/stuff/transmissions/plugins/downspout/drumgen',
      'http://purl.org/stuff/transmissions/plugins/downspout/drumkit',
      'http://purl.org/stuff/transmissions/plugins/downspout/rift'
    ])
    expect(validation.valid).toBe(true)
    expect(validation.links.map(link => link.connectionKinds)).toEqual([['midi'], ['audio']])
    expect(validation.links[0].notes).toContain('This is a curated recommended pairing.')
  })

  it('reports incompatible signal flow', async () => {
    const catalogue = new PluginCatalogue()
    await catalogue.loadProfileFile(downspoutProfile)
    const validation = catalogue.validateChain([
      'http://purl.org/stuff/transmissions/plugins/downspout/rift',
      'http://purl.org/stuff/transmissions/plugins/downspout/drumkit'
    ])
    expect(validation.valid).toBe(false)
    expect(validation.links[0].message).toContain('No compatible')
  })

  it('validates the generative-suite test chains', async () => {
    const catalogue = new PluginCatalogue()
    await catalogue.loadProfileFile(downspoutProfile)
    const profile = name =>
      `http://purl.org/stuff/transmissions/plugins/downspout/${name}`
    for (const chain of [
      ['conductor', 'harmonic-atlas', 'canticle', 'orbit', 'guardian'],
      ['polymeter', 'mnemosyne', 'drumkit', 'guardian'],
      ['canticle', 'oracle', 'resonance-garden', 'drift', 'orbit', 'guardian'],
      ['drift', 'mosaic', 'guardian']
    ]) {
      const validation = catalogue.validateChain(chain.map(profile))
      expect(validation.valid, validation.links
        .map(link => link.message).join('\n')).toBe(true)
    }
  })
})
