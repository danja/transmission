import { describe, expect, it } from 'vitest'
import { parsePluginProfiles } from '../../src/rdf/PluginProfileRdf.js'

const prefix = `
  @prefix trn: <http://purl.org/stuff/transmissions/> .
  @prefix ex: <http://example.test/plugins/> .
  @prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .
`

describe('PluginProfileRdf', () => {
  it('parses semantic signal, requirement, pairing, and caution data', async () => {
    const profiles = await parsePluginProfiles(`${prefix}
      ex:generator a trn:PluginProfile ;
        rdfs:label "Generator" ;
        rdfs:comment "Makes related notes." ;
        trn:bundleName "generator.vst3" ;
        trn:role trn:MidiGenerator ;
        trn:produces trn:Midi, trn:MelodyMidi ;
        trn:requires trn:HostTransport ;
        trn:recommendedBefore ex:synth ;
        trn:caution "Needs an instrument." .
    `, 'test.ttl')
    expect(profiles).toEqual([expect.objectContaining({
      id: 'http://example.test/plugins/generator',
      name: 'Generator',
      roles: ['MidiGenerator'],
      produces: ['Midi', 'MelodyMidi'],
      requirements: ['HostTransport'],
      recommendedBefore: ['http://example.test/plugins/synth'],
      cautions: ['Needs an instrument.'],
      profileSource: 'test.ttl'
    })])
  })

  it('rejects profiles without a discovery identity', async () => {
    await expect(parsePluginProfiles(`${prefix}
      ex:broken a trn:PluginProfile ; rdfs:label "Broken" .
    `)).rejects.toThrow('bundleName or trn:vstClassId')
  })
})
