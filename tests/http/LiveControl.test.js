import { mkdtemp, readFile, rm, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { pathToFileURL } from 'node:url'
import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import { TransmissionControlService } from '../../src/control/TransmissionControlService.js'
import { TransmissionHttpServer } from '../../src/http/TransmissionHttpServer.js'
import { TransmissionHttpClient } from '../../src/http/TransmissionHttpClient.js'

const TRN = 'http://purl.org/stuff/transmissions/'

const definition = {
  id: `${TRN}live-control`,
  nodes: [
    { id: `${TRN}gen`, type: `${TRN}VST3Plugin`, ports: { audioOutputs: 2 } },
    { id: `${TRN}out`, type: `${TRN}AudioOutput`, ports: { audioInputs: 2 } }
  ],
  connections: [{ from: `${TRN}gen`, to: `${TRN}out`, kind: 'audio' }]
}

const fixtureProfile = `
@base <https://example.org/plugins/pulse/> .
@prefix jig:  <http://purl.org/stuff/jigdaw/> .
@prefix trn:  <http://purl.org/stuff/transmissions/> .
@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .

<> a jig:WebPlugin , trn:PluginProfile ;
    rdfs:label "Pulse" ;
    trn:accepts trn:Midi ;
    trn:produces trn:Audio ;
    jig:audioInputs 0 ;
    jig:audioOutputs 1 ;
    jig:outputChannels 2 ;
    jig:module <#module> .

<#module> a jig:Module ;
    jig:location <pulse.wasm> ;
    jig:abi jig:Abi1 ;
    jig:integrity "sha384-abc" .
`

describe('control operations over live HTTP', () => {
  let directory
  let server
  let client

  beforeEach(async () => {
    directory = await mkdtemp(join(tmpdir(), 'transmission-live-control-'))
    const control = new TransmissionControlService({ allowedRoots: [directory] })
    server = new TransmissionHttpServer(control, { port: 0, bindAddress: '127.0.0.1' })
    await server.listen()
    client = new TransmissionHttpClient(`http://127.0.0.1:${server._server.address().port}`)
    await client.newProject(definition)
  })

  afterEach(async () => {
    await server.close()
    await rm(directory, { recursive: true, force: true })
  })

  it('sets multiple parameters atomically', async () => {
    const { revision } = await client.getArrangement()
    const changed = await client.setParameters({
      expectedRevision: revision,
      nodeId: `${TRN}gen`,
      parameters: [{ id: 0, normalizedValue: 0.25 }, { id: 7, normalizedValue: 0.75 }]
    })
    expect(changed).toMatchObject({ nodeId: `${TRN}gen`, appliedToRuntime: false })
    const project = await client.describeProject()
    expect(project.graph.nodes.find(n => n.id === `${TRN}gen`).parameters).toEqual([
      { id: 0, normalizedValue: 0.25 },
      { id: 7, normalizedValue: 0.75 }
    ])
  })

  it('rejects invalid batch parameters without changing the project', async () => {
    const { revision } = await client.getArrangement()
    await expect(client.setParameters({
      expectedRevision: revision,
      nodeId: `${TRN}gen`,
      parameters: [{ id: 1, normalizedValue: 2 }]
    })).rejects.toThrow()
    await expect(client.setParameters({
      expectedRevision: revision,
      nodeId: `${TRN}missing`,
      parameters: [{ id: 1, normalizedValue: 0.5 }]
    })).rejects.toThrow('Node does not exist')
    const project = await client.describeProject()
    expect(project.graph.nodes.find(n => n.id === `${TRN}gen`).parameters ?? []).toEqual([])
  })

  it('describes a JigDAW plugin from a file IRI', async () => {
    const plugdir = await mkdtemp(join(tmpdir(), 'transmission-live-jigdaw-'))
    try {
      await writeFile(join(plugdir, 'profile.ttl'), fixtureProfile)
      const described = await client.describeJigdawPlugin(`${pathToFileURL(plugdir).href}/`, { id: 'pulse' })
      expect(described.profile.label).toBe('Pulse')
      expect(described.node.type).toBe(`${TRN}JigdawPlugin`)
    } finally {
      await rm(plugdir, { recursive: true, force: true })
    }
  })

  it('renders the arrangement to a MIDI file below the allowed root', async () => {
    const { revision } = await client.getArrangement()
    await client.updateArrangement({ expectedRevision: revision, lengthBeats: 16 })
    const rendered = await client.renderMidi('arrangement.mid')
    expect(rendered.bytes).toBeGreaterThan(0)
    const data = await readFile(join(directory, 'arrangement.mid'))
    expect(data.subarray(0, 4).toString('latin1')).toBe('MThd')
  })

  it('refuses to render outside the allowed roots', async () => {
    await expect(client.renderMidi('../outside.mid')).rejects.toThrow('403')
  })

  it('reports that MIDI capture needs the native engine', async () => {
    await expect(client.captureProjectMidi({ filePath: 'capture.mid', durationBeats: 4 }))
      .rejects.toThrow('Native engine is required')
  })

  it('reads peaks', async () => {
    await expect(client.peaks()).resolves.toMatchObject({ peakL: 0, peakR: 0 })
  })
})
