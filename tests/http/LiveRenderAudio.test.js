import { existsSync } from 'node:fs'
import { readFile, rm, mkdtemp } from 'node:fs/promises'
import { tmpdir, homedir } from 'node:os'
import { join, dirname } from 'node:path'
import { createRequire } from 'node:module'
import { fileURLToPath } from 'node:url'
import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import { TransmissionControlService } from '../../src/control/TransmissionControlService.js'
import { TransmissionHttpServer } from '../../src/http/TransmissionHttpServer.js'
import { TransmissionHttpClient } from '../../src/http/TransmissionHttpClient.js'
import { NativeBridge } from '../../src/bridge/NativeBridge.js'
import { EngineSession } from '../../src/session/EngineSession.js'

const require = createRequire(import.meta.url)
const repositoryRoot = join(dirname(fileURLToPath(import.meta.url)), '..', '..')
const TRN = 'http://purl.org/stuff/transmissions/'
const addonPath = join(repositoryRoot, 'native/build-napi-vst3/transmission_native.node')
const bassgen = join(homedir(), '.vst3/bassgen.vst3')
const basilico = join(homedir(), '.vst3/basilico.vst3')

const available = existsSync(addonPath) && existsSync(bassgen) && existsSync(basilico)

const definition = {
  id: `${TRN}live-render`,
  nodes: [
    {
      id: `${TRN}gen`, type: `${TRN}VST3Plugin`,
      ports: { audioOutputs: 2, midiInputs: 1, midiOutputs: 1 },
      settings: { pluginPath: bassgen }
    },
    {
      id: `${TRN}syn`, type: `${TRN}VST3Plugin`,
      ports: { audioOutputs: 2, midiInputs: 1 },
      settings: { pluginPath: basilico }
    },
    { id: `${TRN}out`, type: `${TRN}AudioOutput`, ports: { audioInputs: 2 } }
  ],
  connections: [
    { from: `${TRN}gen`, to: `${TRN}syn`, kind: 'midi' },
    { from: `${TRN}syn`, to: `${TRN}out`, kind: 'audio', fromPort: 0, toPort: 0 },
    { from: `${TRN}syn`, to: `${TRN}out`, kind: 'audio', fromPort: 1, toPort: 1 }
  ]
}

describe.skipIf(!available)('audio render over live HTTP with the real addon', () => {
  let directory
  let server
  let client

  beforeEach(async () => {
    directory = await mkdtemp(join(tmpdir(), 'transmission-live-render-'))
    const bridge = new NativeBridge(require(addonPath))
    const engine = new EngineSession({ bridge })
    const control = new TransmissionControlService({ engine, allowedRoots: [directory] })
    server = new TransmissionHttpServer(control, { port: 0, bindAddress: '127.0.0.1' })
    await server.listen()
    client = new TransmissionHttpClient(`http://127.0.0.1:${server._server.address().port}`)
    await client.newProject(definition)
  }, 120000)

  afterEach(async () => {
    await server.close()
    await rm(directory, { recursive: true, force: true })
  })

  it('bounces the project to a WAV file', async () => {
    const rendered = await client.renderAudio({ filePath: 'bounce.wav', totalBeats: 4 })
    expect(rendered.framesWritten).toBe(96000)
    expect(rendered.peak).toBeGreaterThan(0.05)
    const data = await readFile(join(directory, 'bounce.wav'))
    expect(data.subarray(0, 4).toString('latin1')).toBe('RIFF')
    let sum = 0
    const samples = 48000
    for (let i = 0; i < samples; i++) {
      const v = data.readFloatLE(44 + i * 8)
      sum += v * v
    }
    expect(Math.sqrt(sum / samples)).toBeGreaterThan(0.05)
  }, 120000)
})
