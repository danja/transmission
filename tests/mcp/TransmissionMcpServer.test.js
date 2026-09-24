import { mkdtemp, rm, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { fileURLToPath, pathToFileURL } from 'node:url'
import { afterEach, describe, expect, it } from 'vitest'
import { Client } from '@modelcontextprotocol/sdk/client/index.js'
import { InMemoryTransport } from '@modelcontextprotocol/sdk/inMemory.js'
import { createTransmissionMcpServer } from '../../src/mcp/TransmissionMcpServer.js'
import { TransmissionControlService } from '../../src/control/TransmissionControlService.js'
import { PluginCatalogue } from '../../src/registry/PluginCatalogue.js'

let server
let client
const temporaryDirectories = []
afterEach(async () => {
  await client?.close()
  await server?.close()
  await Promise.all(temporaryDirectories.splice(0)
    .map(path => rm(path, { recursive: true, force: true })))
})

describe('Transmission MCP server', () => {
  it('advertises resources and performs a revision-checked graph transaction', async () => {
    const control = new TransmissionControlService()
    server = createTransmissionMcpServer(control)
    client = new Client({ name: 'transmission-test', version: '1.0.0' })
    const [clientTransport, serverTransport] = InMemoryTransport.createLinkedPair()
    await server.connect(serverTransport)
    await client.connect(clientTransport)

    const tools = await client.listTools()
    expect(tools.tools.map(tool => tool.name)).toContain('graph_apply_changes')

    const created = await client.callTool({
      name: 'project_new',
      arguments: {
        project: {
          id: 'http://purl.org/stuff/transmissions/mcp',
          nodes: [
            { id: 'source', type: 'Generator', ports: { audioOutputs: 1 } },
            { id: 'sink', type: 'Output', ports: { audioInputs: 1 } }
          ],
          connections: [{ from: 'source', to: 'sink', kind: 'audio' }]
        }
      }
    })
    expect(created.isError).not.toBe(true)

    const changed = await client.callTool({
      name: 'graph_apply_changes',
      arguments: {
        expectedRevision: 0,
        operations: [{
          type: 'updateNode',
          nodeId: 'source',
          changes: { label: 'LLM generator' }
        }]
      }
    })
    expect(changed.isError).not.toBe(true)
    expect(changed.structuredContent).toMatchObject({ revision: 1 })

    const project = await client.readResource({ uri: 'transmission://project' })
    expect(JSON.parse(project.contents[0].text).graph.nodes[0].label).toBe('LLM generator')

    const stale = await client.callTool({
      name: 'graph_apply_changes',
      arguments: {
        expectedRevision: 0,
        operations: [{ type: 'removeNode', nodeId: 'source' }]
      }
    })
    expect(stale.isError).toBe(true)
    expect(stale.content[0].text).toContain('revision mismatch')
  })

  it('exposes RDF-backed plugin knowledge without loading it into every response', async () => {
    const catalogue = new PluginCatalogue()
    await catalogue.loadProfileFile(fileURLToPath(new URL('../../profiles/downspout.ttl', import.meta.url)))
    const control = new TransmissionControlService({ pluginCatalogue: catalogue })
    server = createTransmissionMcpServer(control)
    client = new Client({ name: 'transmission-catalogue-test', version: '1.0.0' })
    const [clientTransport, serverTransport] = InMemoryTransport.createLinkedPair()
    await server.connect(serverTransport)
    await client.connect(clientTransport)

    const tools = await client.listTools()
    expect(tools.tools.map(tool => tool.name)).toContain('plugins_search')
    const resources = await client.listResources()
    expect(resources.resources.map(resource => resource.uri)).toContain('transmission://plugins/profiles')
    expect(resources.resources.map(resource => resource.uri)).toContain('transmission://plugins/discovered')

    const search = await client.callTool({
      name: 'plugins_search',
      arguments: { produces: ['DrumMidi'] }
    })
    expect(search.structuredContent.matches).toBe(4)
    expect(search.structuredContent.entries.map(plugin => plugin.name))
      .toEqual(['DrumGen', 'Lifeform', 'Polymeter', 'Xoxolo'])

    const profile = await client.readResource({ uri: 'transmission://plugins/profiles' })
    expect(profile.contents[0].text).toContain('dsp:drumgen a trn:PluginProfile')
  })

  it('describes a JigDAW plugin from its IRI, with no catalogue and no scan', async () => {
    // No pluginCatalogue: a JigDAW plugin is not in a registry, and the tool has
    // to be there for a session that hosts nothing but JigDAW plugins.
    const control = new TransmissionControlService()
    server = createTransmissionMcpServer(control)
    client = new Client({ name: 'transmission-jigdaw-test', version: '1.0.0' })
    const [clientTransport, serverTransport] = InMemoryTransport.createLinkedPair()
    await server.connect(serverTransport)
    await client.connect(clientTransport)

    const tools = await client.listTools()
    expect(tools.tools.map(tool => tool.name)).toContain('jigdaw_describe')

    const directory = await mkdtemp(join(tmpdir(), 'transmission-jigdaw-'))
    temporaryDirectories.push(directory)
    await writeFile(join(directory, 'profile.ttl'), fixtureProfile)

    const described = await client.callTool({
      name: 'jigdaw_describe',
      arguments: { iri: `${pathToFileURL(directory).href}/`, id: 'pulse' }
    })
    expect(described.isError).not.toBe(true)
    expect(described.structuredContent.profile.label).toBe('Pulse')
    expect(described.structuredContent.node.type)
      .toBe('http://purl.org/stuff/transmissions/JigdawPlugin')
    expect(described.structuredContent.node.ports.audioOutputs).toBe(2)
    expect(described.structuredContent.node.settings.pluginIri)
      .toBe('https://example.org/plugins/pulse/')
  })

  it('adds and removes MIDI CC mappings over graph operations', async () => {
    const control = new TransmissionControlService()
    server = createTransmissionMcpServer(control)
    client = new Client({ name: 'transmission-midi-mapping-test', version: '1.0.0' })
    const [clientTransport, serverTransport] = InMemoryTransport.createLinkedPair()
    await server.connect(serverTransport)
    await client.connect(clientTransport)

    const tools = await client.listTools()
    expect(tools.tools.map(tool => tool.name)).toContain('midi_mapping_add')
    expect(tools.tools.map(tool => tool.name)).toContain('midi_mapping_remove')

    const created = await client.callTool({
      name: 'project_new',
      arguments: {
        project: {
          id: 'http://purl.org/stuff/transmissions/mcp-midi',
          nodes: [{ id: 'synth', type: 'VST3Plugin', ports: { audioOutputs: 2 } }]
        }
      }
    })
    expect(created.isError).not.toBe(true)

    const mapping = { targetNodeId: 'synth', parameterId: 3, controller: 19 }
    const added = await client.callTool({
      name: 'midi_mapping_add',
      arguments: { expectedRevision: 0, mapping }
    })
    expect(added.isError).not.toBe(true)
    expect(added.structuredContent.graph.metadata.midiMappings).toEqual([
      { targetNodeId: 'synth', parameterId: 3, channel: -1, controller: 19, consume: true }
    ])

    const duplicate = await client.callTool({
      name: 'midi_mapping_add',
      arguments: { expectedRevision: 1, mapping }
    })
    expect(duplicate.isError).toBe(true)

    const badController = await client.callTool({
      name: 'midi_mapping_add',
      arguments: { expectedRevision: 1, mapping: { ...mapping, controller: 200 } }
    })
    expect(badController.isError).toBe(true)

    const removed = await client.callTool({
      name: 'midi_mapping_remove',
      arguments: {
        expectedRevision: 1,
        mapping: { targetNodeId: 'synth', parameterId: 3, channel: -1, controller: 19, consume: true }
      }
    })
    expect(removed.isError).not.toBe(true)
    expect(removed.structuredContent.graph.metadata.midiMappings).toEqual([])
  })

  it('keeps JigDAW asset overrides on node_add', async () => {
    const control = new TransmissionControlService()
    server = createTransmissionMcpServer(control)
    client = new Client({ name: 'transmission-asset-test', version: '1.0.0' })
    const [clientTransport, serverTransport] = InMemoryTransport.createLinkedPair()
    await server.connect(serverTransport)
    await client.connect(clientTransport)

    const created = await client.callTool({
      name: 'project_new',
      arguments: { project: { id: 'http://purl.org/stuff/transmissions/assets', nodes: [] } }
    })
    expect(created.isError).not.toBe(true)

    const added = await client.callTool({
      name: 'node_add',
      arguments: {
        expectedRevision: 0,
        node: {
          id: 'pulse',
          type: 'http://purl.org/stuff/transmissions/JigdawPlugin',
          jigdawAssetOverrides: [{ key: 'nam', path: '/models/amp.nam' }]
        }
      }
    })
    expect(added.isError).not.toBe(true)
    const project = await client.readResource({ uri: 'transmission://project' })
    expect(JSON.parse(project.contents[0].text).graph.nodes[0].jigdawAssetOverrides)
      .toEqual([{ key: 'nam', path: '/models/amp.nam' }])
  })
})

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
