// tests/registry/JigdawProfile.test.js

import { describe, expect, it } from 'vitest'
import {
  JigdawProfileError,
  denormalizeParameter,
  jigdawGraphNode,
  normalizeParameter,
  readJigdawProfile
} from '../../src/registry/JigdawProfile.js'

// A profile is fetched, so every test injects the fetch. Nothing here touches
// the network or the filesystem, and the Turtle is written out rather than read
// from the jigdaw checkout so that a change there is a test failure here rather
// than a silent change of meaning.
const instrument = `
@base <https://example.org/plugins/pulse/> .
@prefix jig:   <http://purl.org/stuff/jigdaw/> .
@prefix trn:   <http://purl.org/stuff/transmissions/> .
@prefix lv2:   <http://lv2plug.in/ns/lv2core#> .
@prefix units: <http://lv2plug.in/ns/extensions/units#> .
@prefix rdf:   <http://www.w3.org/1999/02/22-rdf-syntax-ns#> .
@prefix rdfs:  <http://www.w3.org/2000/01/rdf-schema#> .

<> a jig:WebPlugin , trn:PluginProfile ;
    rdfs:label "Pulse" ;
    trn:vendor "danja" ;
    trn:role trn:Instrument ;
    trn:accepts trn:Midi ;
    trn:produces trn:Audio ;
    trn:requires jig:MidiEvents ;
    jig:audioInputs 0 ;
    jig:audioOutputs 1 ;
    jig:outputChannels 2 ;
    jig:module <#module> ;
    lv2:port <#waveform> , <#cutoff> .

<#module> a jig:Module ;
    jig:location <pulse.wasm> ;
    jig:abi jig:Abi1 ;
    jig:integrity "sha384-abc" .

<#cutoff> a lv2:InputPort , lv2:ControlPort ;
    lv2:symbol "cutoff" ; lv2:name "Cutoff" ;
    lv2:default 6000 ; lv2:minimum 100 ; lv2:maximum 18000 ;
    jig:paramIndex 3 ;
    units:unit units:hz .

<#waveform> a lv2:InputPort , lv2:ControlPort ;
    lv2:symbol "waveform" ; lv2:name "Waveform" ;
    lv2:default 0 ; lv2:minimum 0 ; lv2:maximum 2 ;
    jig:paramIndex 0 ;
    lv2:portProperty lv2:enumeration ;
    lv2:scalePoint
        [ rdfs:label "Triangle" ; rdf:value 2 ] ,
        [ rdfs:label "Saw" ; rdf:value 0 ] ,
        [ rdfs:label "Square" ; rdf:value 1 ] .
`

const generator = `
@base <https://example.org/plugins/bassgen/> .
@prefix jig:   <http://purl.org/stuff/jigdaw/> .
@prefix trn:   <http://purl.org/stuff/transmissions/> .
@prefix lv2:   <http://lv2plug.in/ns/lv2core#> .
@prefix rdfs:  <http://www.w3.org/2000/01/rdf-schema#> .

<> a jig:WebPlugin , trn:PluginProfile ;
    rdfs:label "BassGen" ;
    trn:accepts trn:Midi ;
    trn:produces trn:Midi , trn:BassMidi ;
    trn:requires jig:MidiEvents , jig:MidiOut , trn:HostTransport ;
    jig:audioInputs 0 ;
    jig:audioOutputs 0 ;
    jig:module <#module> .

<#module> a jig:Module ;
    jig:location <bassgen.wasm> ;
    jig:abi jig:Abi2 ;
    jig:integrity "sha384-def" .
`

const processorOnly = `
@base <https://example.org/plugins/private/> .
@prefix jig:  <http://purl.org/stuff/jigdaw/> .
@prefix trn:  <http://purl.org/stuff/transmissions/> .
@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .

<> a jig:WebPlugin , trn:PluginProfile ;
    rdfs:label "Private" ;
    jig:audioOutputs 1 ;
    jig:module <#module> .

<#module> a jig:Module ;
    jig:location <private.wasm> ;
    jig:integrity "sha384-ghi" .
`

const serve = text => async iri => ({ text, base: iri })

describe('readJigdawProfile', () => {
  it('reports the port shape a transmission graph is validated against', async () => {
    const profile = await readJigdawProfile('https://example.org/plugins/pulse/',
                                            { fetch: serve(instrument) })
    expect(profile.iri).toBe('https://example.org/plugins/pulse/')
    expect(profile.label).toBe('Pulse')
    expect(profile.abi).toBe('http://purl.org/stuff/jigdaw/Abi1')
    // One Web Audio output port carrying two channels is two transmission ports.
    expect(profile.ports).toEqual({
      audioInputs: 0, audioOutputs: 2, midiInputs: 1, midiOutputs: 0
    })
    expect(profile.module.location).toBe('https://example.org/plugins/pulse/pulse.wasm')
    expect(profile.module.integrity).toBe('sha384-abc')
  })

  it('orders parameters by jig:paramIndex, not by document order', async () => {
    const profile = await readJigdawProfile('https://example.org/plugins/pulse/',
                                            { fetch: serve(instrument) })
    expect(profile.parameters.map(parameter => parameter.id)).toEqual([0, 3])
    const [waveform, cutoff] = profile.parameters
    expect(waveform.symbol).toBe('waveform')
    expect(waveform.enumeration).toBe(true)
    expect(waveform.scalePoints.map(point => point.label))
      .toEqual(['Saw', 'Square', 'Triangle'])
    expect(cutoff.unit).toBe('http://lv2plug.in/ns/extensions/units#hz')
    expect(cutoff.maximum).toBe(18000)
  })

  it('reads a MIDI generator with no audio and a transport requirement', async () => {
    const profile = await readJigdawProfile('https://example.org/plugins/bassgen/',
                                            { fetch: serve(generator) })
    expect(profile.ports).toEqual({
      audioInputs: 0, audioOutputs: 0, midiInputs: 1, midiOutputs: 1
    })
    expect(profile.requiresTransport).toBe(true)
    expect(profile.parameters).toEqual([])
  })

  it('refuses a module that is private to its JavaScript processor', async () => {
    await expect(readJigdawProfile('https://example.org/plugins/private/',
                                   { fetch: serve(processorOnly) }))
      .rejects.toThrow(JigdawProfileError)
  })

  it('refuses a document that declares no jig:WebPlugin', async () => {
    await expect(readJigdawProfile('https://example.org/nothing/',
                                   { fetch: serve('@prefix x: <http://example.org/> .\n') }))
      .rejects.toThrow(/no jig:WebPlugin/)
  })
})

describe('jigdawGraphNode', () => {
  it('takes its port counts from the profile rather than from the caller', async () => {
    const profile = await readJigdawProfile('https://example.org/plugins/pulse/',
                                            { fetch: serve(instrument) })
    const node = jigdawGraphNode(profile, { id: 'pulse', x: 10, y: 20 })
    expect(node.type).toBe('http://purl.org/stuff/transmissions/JigdawPlugin')
    expect(node.label).toBe('Pulse')
    expect(node.settings).toEqual({ pluginIri: 'https://example.org/plugins/pulse/' })
    expect(node.ports).toEqual(profile.ports)
    expect(node.metadata).toEqual({ x: 10, y: 20 })
  })
})

describe('parameter scaling', () => {
  it('round-trips a declared range through the engine\'s normalized form', async () => {
    const profile = await readJigdawProfile('https://example.org/plugins/pulse/',
                                            { fetch: serve(instrument) })
    const cutoff = profile.parameters.find(parameter => parameter.symbol === 'cutoff')
    expect(normalizeParameter(cutoff, 100)).toBe(0)
    expect(normalizeParameter(cutoff, 18000)).toBe(1)
    expect(denormalizeParameter(cutoff, normalizeParameter(cutoff, 6000)))
      .toBeCloseTo(6000, 6)
  })

  it('snaps an enumeration to a named value rather than landing between two', async () => {
    const profile = await readJigdawProfile('https://example.org/plugins/pulse/',
                                            { fetch: serve(instrument) })
    const waveform = profile.parameters.find(parameter => parameter.symbol === 'waveform')
    expect(denormalizeParameter(waveform, 0.4)).toBe(1)
    expect(denormalizeParameter(waveform, 1)).toBe(2)
  })
})
