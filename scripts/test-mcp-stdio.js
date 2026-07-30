#!/usr/bin/env node

import { Client } from '@modelcontextprotocol/sdk/client/index.js'
import { StdioClientTransport } from '@modelcontextprotocol/sdk/client/stdio.js'

const transport = new StdioClientTransport({
  command: process.execPath,
  args: ['scripts/transmission-mcp.js', '--project-root', process.cwd()],
  cwd: process.cwd(),
  stderr: 'inherit'
})
const client = new Client({ name: 'transmission-stdio-smoke', version: '1.0.0' })

try {
  await client.connect(transport)
  const { tools } = await client.listTools()
  const { resources } = await client.listResources()
  const created = await client.callTool({
    name: 'project_new',
    arguments: {
      project: {
        id: 'http://purl.org/stuff/transmissions/mcp-smoke',
        nodes: [
          { id: 'source', type: 'Generator', ports: { audioOutputs: 1 } },
          { id: 'output', type: 'Output', ports: { audioInputs: 1 } }
        ],
        connections: [{ from: 'source', to: 'output', kind: 'audio' }]
      }
    }
  })
  if (created.isError) throw new Error(created.content?.[0]?.text ?? 'project_new failed')
  const turtle = await client.readResource({ uri: 'transmission://project/turtle' })
  if (!turtle.contents[0]?.text?.includes(':mcp-smoke a :Transmission')) {
    throw new Error('Turtle project resource did not contain the smoke project')
  }
  const pluginSearch = await client.callTool({
    name: 'plugins_search',
    arguments: { produces: ['DrumMidi'], installedOnly: true }
  })
  if (pluginSearch.isError || pluginSearch.structuredContent.matches < 1) {
    throw new Error('Installed DrumMidi plugin search returned no matches')
  }
  const chain = await client.callTool({
    name: 'plugin_validate_chain',
    arguments: {
      identifiers: [
        'http://purl.org/stuff/transmissions/plugins/downspout/drumgen',
        'http://purl.org/stuff/transmissions/plugins/downspout/drumkit'
      ]
    }
  })
  if (chain.isError || !chain.structuredContent.valid) {
    throw new Error('Curated DrumGen to DrumKit chain did not validate')
  }
  const drumgen = await client.callTool({
    name: 'plugin_describe',
    arguments: { identifier: 'http://purl.org/stuff/transmissions/plugins/downspout/drumgen' }
  })
  if (drumgen.isError || !drumgen.structuredContent.parameters?.some(parameter => parameter.title === 'Genre')) {
    throw new Error('Discovered DrumGen parameter metadata did not include Genre')
  }
  const catalogueResource = await client.readResource({ uri: 'transmission://plugins' })
  const catalogue = JSON.parse(catalogueResource.contents[0].text)
  console.log(
    `Transmission MCP stdio smoke passed: ${tools.length} tools, ${resources.length} resources, ` +
    `${catalogue.installed} installed plugins, ${pluginSearch.structuredContent.matches} installed DrumMidi generators`
  )
  for (const failure of catalogue.scanFailures) {
    console.warn(`Plugin scan warning: ${failure.modulePath}: ${failure.error}`)
  }
} finally {
  await client.close()
}
