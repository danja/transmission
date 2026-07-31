#!/usr/bin/env node

import { chmod, mkdir, mkdtemp, rm, writeFile } from 'node:fs/promises'
import { spawn } from 'node:child_process'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { createInterface } from 'node:readline'
import { once } from 'node:events'
import { LATEST_PROTOCOL_VERSION } from '@modelcontextprotocol/sdk/types.js'

const fixtureRoot = await mkdtemp(join(tmpdir(), 'transmission-mcp-smoke-'))
const pluginRoot = join(fixtureRoot, 'plugins')
const inspectorPath = join(fixtureRoot, 'inspector')
await mkdir(join(pluginRoot, 'drumgen.vst3'), { recursive: true })
await mkdir(join(pluginRoot, 'drumkit.vst3'), { recursive: true })
await writeFile(inspectorPath, `#!${process.execPath}
import { basename } from 'node:path'
const modulePath = process.argv[2]
const bundle = basename(modulePath).toLowerCase()
if (bundle === 'drumgen.vst3') {
  console.log([
    'id=smoke-drumgen',
    'name=DrumGen',
    'vendor=danja',
    'category=Audio Module Class',
    'midiOutputs=1',
    'parameter.0.id=3',
    'parameter.0.title=Genre',
    'parameter.0.defaultNormalized=0'
  ].join('\\n'))
} else if (bundle === 'drumkit.vst3') {
  console.log([
    'id=smoke-drumkit',
    'name=DrumKit',
    'vendor=danja',
    'category=Audio Module Class',
    'audioOutputs=2',
    'midiInputs=1'
  ].join('\\n'))
} else {
  process.exitCode = 2
}
`)
await chmod(inspectorPath, 0o755)

const child = spawn(
  process.execPath,
  [
    'scripts/transmission-mcp.js',
    '--project-root', process.cwd(),
    '--plugin-root', pluginRoot,
    '--vst3-inspector', inspectorPath
  ],
  { cwd: process.cwd(), stdio: ['pipe', 'pipe', 'pipe'] }
)
child.stderr.pipe(process.stderr)

const pending = new Map()
let nextId = 1
const output = createInterface({ input: child.stdout })
output.on('line', line => {
  let message
  try {
    message = JSON.parse(line)
  } catch (error) {
    rejectPending(new Error(`Invalid JSON from MCP server: ${error.message}`))
    return
  }
  if (message.id === undefined) return
  const request = pending.get(message.id)
  if (!request) return
  pending.delete(message.id)
  clearTimeout(request.timeout)
  if (message.error) {
    request.reject(new Error(`MCP ${message.error.code}: ${message.error.message}`))
  } else {
    request.resolve(message.result)
  }
})
child.once('exit', (code, signal) => {
  rejectPending(new Error(
    `MCP server exited before the smoke test completed (${signal ?? code})`
  ))
})

try {
  await once(child, 'spawn')
  await request('initialize', {
    protocolVersion: LATEST_PROTOCOL_VERSION,
    capabilities: {},
    clientInfo: { name: 'transmission-stdio-smoke', version: '1.0.0' }
  })
  notify('notifications/initialized')

  const { tools } = await request('tools/list')
  const { resources } = await request('resources/list')
  const created = await callTool('project_new', {
    project: {
      id: 'http://purl.org/stuff/transmissions/mcp-smoke',
      nodes: [
        { id: 'source', type: 'Generator', ports: { audioOutputs: 1 } },
        { id: 'output', type: 'Output', ports: { audioInputs: 1 } }
      ],
      connections: [{ from: 'source', to: 'output', kind: 'audio' }]
    }
  })
  if (created.isError) throw new Error(created.content?.[0]?.text ?? 'project_new failed')
  const turtle = await request('resources/read', {
    uri: 'transmission://project/turtle'
  })
  if (!turtle.contents[0]?.text?.includes(':mcp-smoke a :Transmission')) {
    throw new Error('Turtle project resource did not contain the smoke project')
  }
  const pluginSearch = await callTool('plugins_search', {
    produces: ['DrumMidi'],
    installedOnly: true
  })
  if (pluginSearch.isError || pluginSearch.structuredContent.matches < 1) {
    throw new Error('Installed DrumMidi plugin search returned no matches')
  }
  const chain = await callTool('plugin_validate_chain', {
    identifiers: [
      'http://purl.org/stuff/transmissions/plugins/downspout/drumgen',
      'http://purl.org/stuff/transmissions/plugins/downspout/drumkit'
    ]
  })
  if (chain.isError || !chain.structuredContent.valid) {
    throw new Error('Curated DrumGen to DrumKit chain did not validate')
  }
  const drumgen = await callTool('plugin_describe', {
    identifier: 'http://purl.org/stuff/transmissions/plugins/downspout/drumgen'
  })
  if (drumgen.isError || !drumgen.structuredContent.parameters?.some(parameter => parameter.title === 'Genre')) {
    throw new Error('Discovered DrumGen parameter metadata did not include Genre')
  }
  const catalogueResource = await request('resources/read', {
    uri: 'transmission://plugins'
  })
  const catalogue = JSON.parse(catalogueResource.contents[0].text)
  console.log(
    `Transmission MCP stdio smoke passed: ${tools.length} tools, ${resources.length} resources, ` +
    `${catalogue.installed} installed plugins, ${pluginSearch.structuredContent.matches} installed DrumMidi generators`
  )
  for (const failure of catalogue.scanFailures) {
    console.warn(`Plugin scan warning: ${failure.modulePath}: ${failure.error}`)
  }
} finally {
  child.stdin.end()
  await Promise.race([
    once(child, 'exit'),
    new Promise(resolve => setTimeout(resolve, 2000))
  ])
  if (child.exitCode === null) child.kill('SIGTERM')
  output.close()
  await rm(fixtureRoot, { recursive: true, force: true })
}

function request(method, params = undefined) {
  const id = nextId
  nextId += 1
  return new Promise((resolve, reject) => {
    const timeout = setTimeout(() => {
      pending.delete(id)
      reject(new Error(`MCP request timed out: ${method}`))
    }, 10_000)
    pending.set(id, { resolve, reject, timeout })
    child.stdin.write(`${JSON.stringify({
      jsonrpc: '2.0',
      id,
      method,
      ...(params === undefined ? {} : { params })
    })}\n`)
  })
}

function notify(method, params = undefined) {
  child.stdin.write(`${JSON.stringify({
    jsonrpc: '2.0',
    method,
    ...(params === undefined ? {} : { params })
  })}\n`)
}

function callTool(name, arguments_) {
  return request('tools/call', { name, arguments: arguments_ })
}

function rejectPending(error) {
  for (const request of pending.values()) {
    clearTimeout(request.timeout)
    request.reject(error)
  }
  pending.clear()
}
