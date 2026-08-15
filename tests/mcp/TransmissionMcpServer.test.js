import { fileURLToPath } from 'node:url'
import { afterEach, describe, expect, it } from 'vitest'
import { Client } from '@modelcontextprotocol/sdk/client/index.js'
import { InMemoryTransport } from '@modelcontextprotocol/sdk/inMemory.js'
import { createTransmissionMcpServer } from '../../src/mcp/TransmissionMcpServer.js'
import { TransmissionControlService } from '../../src/control/TransmissionControlService.js'
import { PluginCatalogue } from '../../src/registry/PluginCatalogue.js'

let server
let client
afterEach(async () => {
  await client?.close()
  await server?.close()
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
})
