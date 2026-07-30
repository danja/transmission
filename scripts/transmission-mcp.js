#!/usr/bin/env node

import { createRequire } from 'node:module'
import { availableParallelism, homedir } from 'node:os'
import { dirname, join, resolve } from 'node:path'
import { existsSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import { StdioServerTransport } from '@modelcontextprotocol/sdk/server/stdio.js'
import { NativeBridge } from '../src/bridge/NativeBridge.js'
import { TransmissionControlService } from '../src/control/TransmissionControlService.js'
import { createTransmissionMcpServer } from '../src/mcp/TransmissionMcpServer.js'
import { EngineSession } from '../src/session/EngineSession.js'
import { ProjectSession } from '../src/session/ProjectSession.js'
import { PluginCatalogue } from '../src/registry/PluginCatalogue.js'
import { Vst3Discovery } from '../src/registry/Vst3Discovery.js'

const options = parseArguments(process.argv.slice(2))
const repositoryRoot = resolve(dirname(fileURLToPath(import.meta.url)), '..')
const profilePaths = options.pluginProfiles.length
  ? options.pluginProfiles
  : [join(repositoryRoot, 'profiles/downspout.ttl')]
const pluginRoots = options.pluginRoots.length
  ? options.pluginRoots
  : [join(homedir(), '.vst3')]
const inspectorPath = options.vst3Inspector ??
  join(repositoryRoot, 'native/build-ui-jack-vst3/transmission_vst3_inspect')
const discovery = existsSync(inspectorPath)
  ? new Vst3Discovery({
      inspectorPath,
      concurrency: Math.min(8, Math.max(1, availableParallelism()))
    })
  : null
const pluginCatalogue = new PluginCatalogue({ discovery })
for (const profilePath of profilePaths) await pluginCatalogue.loadProfileFile(resolve(profilePath))
const project = new ProjectSession()
const engine = options.nativeAddon
  ? new EngineSession({
      bridge: new NativeBridge(loadNativeAddon(options.nativeAddon)),
      project,
      engineOptions: options.engineOptions
    })
  : null
const control = new TransmissionControlService({
  project,
  engine,
  allowedRoots: options.projectRoots.length ? options.projectRoots : [process.cwd()],
  pluginCatalogue,
  pluginRoots: pluginRoots.map(root => resolve(root))
})

if (options.project) await control.openProject(options.project)

const server = createTransmissionMcpServer(control)
const transport = new StdioServerTransport()

for (const signal of ['SIGINT', 'SIGTERM']) {
  process.once(signal, async () => {
    control.dispose()
    await server.close()
    process.exit(0)
  })
}

await server.connect(transport)
console.error(`Transmission MCP server ready (${engine ? 'native audio enabled' : 'project-only mode'})`)
const initialPluginScan = discovery && !options.noPluginScan
  ? pluginCatalogue.startScan(pluginRoots.map(root => resolve(root))).then(scan => {
      console.error(`Transmission plugin catalogue: ${scan.discovered} classes, ${scan.profiled} profiled, ${scan.failures.length} scan failures`)
    })
  : Promise.resolve()
process.stdin.resume()
await new Promise(resolveInputEnd => {
  if (process.stdin.readableEnded) resolveInputEnd()
  else process.stdin.once('end', resolveInputEnd)
})
await initialPluginScan
control.dispose()
await server.close()

function loadNativeAddon(filePath) {
  const require = createRequire(import.meta.url)
  return require(resolve(filePath))
}

function parseArguments(arguments_) {
  const result = {
    nativeAddon: null,
    project: null,
    projectRoots: [],
    engineOptions: {},
    pluginProfiles: [],
    pluginRoots: [],
    vst3Inspector: null,
    noPluginScan: false
  }
  for (let index = 0; index < arguments_.length; index += 1) {
    const argument = arguments_[index]
    const value = arguments_[index + 1]
    if (argument === '--jack') {
      result.engineOptions.device = 'jack'
      continue
    } else if (argument === '--auto-connect') {
      result.engineOptions.autoConnect = true
      continue
    } else if (argument === '--no-plugin-scan') {
      result.noPluginScan = true
      continue
    } else if (argument === '--native-addon' && value) result.nativeAddon = value
    else if (argument === '--project' && value) result.project = value
    else if (argument === '--project-root' && value) result.projectRoots.push(value)
    else if (argument === '--plugin-profile' && value) result.pluginProfiles.push(value)
    else if (argument === '--plugin-root' && value) result.pluginRoots.push(value)
    else if (argument === '--vst3-inspector' && value) result.vst3Inspector = value
    else if (argument === '--block-size' && value) result.engineOptions.blockSize = positiveNumber(argument, value)
    else if (argument === '--sample-rate' && value) result.engineOptions.sampleRate = positiveNumber(argument, value)
    else if (argument === '--channels' && value) result.engineOptions.channels = positiveNumber(argument, value)
    else if (argument === '--help') {
      console.error([
        'Usage: transmission-mcp [--project-root DIR] [--project FILE] [--native-addon FILE]',
        '                        [--jack] [--auto-connect] [--block-size FRAMES]',
        '                        [--sample-rate HZ] [--channels COUNT]',
        '                        [--plugin-profile FILE] [--plugin-root DIR]',
        '                        [--vst3-inspector FILE] [--no-plugin-scan]'
      ].join('\n'))
      process.exit(0)
    } else if (argument.startsWith('--')) {
      throw new Error(`Unknown option: ${argument}`)
    } else {
      continue
    }
    index += 1
  }
  return result
}

function positiveNumber(option, value) {
  const number = Number(value)
  if (!Number.isFinite(number) || number <= 0) throw new Error(`${option} requires a positive number`)
  return number
}
