#!/usr/bin/env node
// scripts/transmission-live.js
// Persistent live server: combines TransmissionControlService with an HTTP REST API.
// The GTK app and MCP server (in --live mode) connect to this process.

import { createRequire } from 'node:module'
import { availableParallelism, homedir } from 'node:os'
import { dirname, join, resolve } from 'node:path'
import { existsSync } from 'node:fs'
import { readFile } from 'node:fs/promises'
import { fileURLToPath } from 'node:url'
import { NativeBridge } from '../src/bridge/NativeBridge.js'
import { TransmissionControlService } from '../src/control/TransmissionControlService.js'
import { TransmissionHttpServer } from '../src/http/TransmissionHttpServer.js'
import { EngineSession } from '../src/session/EngineSession.js'
import { ProjectSession } from '../src/session/ProjectSession.js'
import { PluginCatalogue } from '../src/registry/PluginCatalogue.js'
import { Vst3Discovery } from '../src/registry/Vst3Discovery.js'
import { parseServerConfig } from '../src/http/TurtleCodec.js'

const repositoryRoot = resolve(dirname(fileURLToPath(import.meta.url)), '..')
const options = parseArguments(process.argv.slice(2))

// ── Load configuration ────────────────────────────────────────────────────────

let serverConfig = { port: 7878, bindAddress: '127.0.0.1', allowedRoots: ['.'] }
const configPath = options.config
  ?? (existsSync(join(repositoryRoot, 'config.ttl')) ? join(repositoryRoot, 'config.ttl') : null)
  ?? join(repositoryRoot, 'config.defaults.ttl')
try {
  const configTurtle = await readFile(configPath, 'utf8')
  serverConfig = await parseServerConfig(configTurtle)
} catch (error) {
  console.error(`[transmission-live] Warning: could not load config from ${configPath}: ${error.message}`)
}
const port = options.port ?? serverConfig.port
const bindAddress = options.bindAddress ?? serverConfig.bindAddress
const allowedRoots = options.projectRoots.length ? options.projectRoots : serverConfig.allowedRoots
const defaultOutputConnections = serverConfig.defaultOutputConnections ?? null
if (serverConfig.blockSize && !options.engineOptions.blockSize) {
  options.engineOptions.blockSize = serverConfig.blockSize
}

// ── Plugin discovery ──────────────────────────────────────────────────────────

const profilePaths = options.pluginProfiles.length
  ? options.pluginProfiles
  : [join(repositoryRoot, 'profiles/downspout.ttl')]
const pluginRoots = options.pluginRoots.length
  ? options.pluginRoots
  : [join(homedir(), '.vst3')]
const packagedInspectorPath = join(repositoryRoot, 'bin/transmission_vst3_inspect')
const inspectorPath = options.vst3Inspector ??
  (existsSync(packagedInspectorPath)
    ? packagedInspectorPath
    : join(repositoryRoot, 'native/build-ui-jack-vst3/transmission_vst3_inspect'))
const discovery = existsSync(inspectorPath)
  ? new Vst3Discovery({
      inspectorPath,
      concurrency: Math.min(8, Math.max(1, availableParallelism()))
    })
  : null
const pluginCatalogue = new PluginCatalogue({ discovery })
for (const profilePath of profilePaths) await pluginCatalogue.loadProfileFile(resolve(profilePath))

// ── Control service ───────────────────────────────────────────────────────────

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
  allowedRoots: allowedRoots.map(root => resolve(root)),
  pluginCatalogue,
  pluginRoots: pluginRoots.map(root => resolve(root)),
  defaultOutputConnections
})

if (options.project) await control.openProject(options.project)

// ── HTTP server ───────────────────────────────────────────────────────────────

const httpServer = new TransmissionHttpServer(control, { port, bindAddress })
await httpServer.listen()
console.log(`Transmission live server ready on http://${bindAddress}:${port}`)

// ── Plugin scan ───────────────────────────────────────────────────────────────

if (discovery && !options.noPluginScan) {
  pluginCatalogue.startScan(pluginRoots.map(root => resolve(root)))
    .then(scan => {
      console.log(`Plugin catalogue: ${scan.discovered} classes, ${scan.profiled} profiled, ${scan.failures.length} scan failures`)
    })
    .catch(error => {
      console.error(`Plugin catalogue scan failed: ${error.message}`)
    })
}

// ── Shutdown ──────────────────────────────────────────────────────────────────

let disposed = false
const dispose = async () => {
  if (disposed) return
  disposed = true
  control.dispose()
  await httpServer.close()
}

for (const signal of ['SIGINT', 'SIGTERM']) {
  process.once(signal, async () => {
    await dispose()
    process.exit(0)
  })
}
process.once('exit', () => { if (!disposed) control.dispose() })

// ── Helpers ───────────────────────────────────────────────────────────────────

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
    noPluginScan: false,
    config: null,
    port: null,
    bindAddress: null
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
    else if (argument === '--config' && value) result.config = value
    else if (argument === '--port' && value) result.port = positiveNumber('--port', value)
    else if (argument === '--bind' && value) result.bindAddress = value
    else if (argument === '--block-size' && value) result.engineOptions.blockSize = positiveNumber(argument, value)
    else if (argument === '--sample-rate' && value) result.engineOptions.sampleRate = positiveNumber(argument, value)
    else if (argument === '--channels' && value) result.engineOptions.channels = positiveNumber(argument, value)
    else if (argument === '--help') {
      console.log([
        'Usage: transmission-live [--config FILE] [--port PORT] [--bind ADDRESS]',
        '                         [--project-root DIR] [--project FILE]',
        '                         [--native-addon FILE] [--jack] [--auto-connect]',
        '                         [--block-size FRAMES] [--sample-rate HZ] [--channels COUNT]',
        '                         [--plugin-profile FILE] [--plugin-root DIR]',
        '                         [--vst3-inspector FILE] [--no-plugin-scan]'
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
