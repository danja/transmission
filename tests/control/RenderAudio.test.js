import { mkdtemp, rm } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { afterEach, describe, expect, it } from 'vitest'
import { TransmissionControlService } from '../../src/control/TransmissionControlService.js'
import { ProjectSession } from '../../src/session/ProjectSession.js'

const TRN = 'http://purl.org/stuff/transmissions/'

const graph = {
  id: `${TRN}render-test`,
  nodes: [
    { id: 'gen', type: 'VST3Plugin', ports: { audioOutputs: 2 } },
    { id: 'out', type: 'AudioOutput', ports: { audioInputs: 2 } }
  ],
  connections: [{ from: 'gen', to: 'out', kind: 'audio' }]
}

let directory
afterEach(async () => {
  if (directory) await rm(directory, { recursive: true, force: true })
  directory = null
})

function stubbedControl(calls = []) {
  const session = new ProjectSession()
  const engine = {
    project: session,
    state: 'loaded',
    open: (definition, filePath) => session.open(definition, filePath),
    renderAudio: (args) => {
      calls.push(args)
      return { outputPath: args.outputPath, framesWritten: 48000, peak: 0.5 }
    }
  }
  return { controlPromise: (async () => {
    directory = await mkdtemp(join(tmpdir(), 'transmission-render-'))
    const control = new TransmissionControlService({ engine, allowedRoots: [directory] })
    control.newProject(structuredClone(graph))
    return control
  })(), calls }
}

describe('control renderAudio validation', () => {
  it('requires the native engine', async () => {
    const control = new TransmissionControlService()
    control.newProject(structuredClone(graph))
    await expect(control.renderAudio({ filePath: 'out.wav', totalBeats: 4 }))
      .rejects.toThrow('Native engine is required')
  })

  it('requires a .wav path', async () => {
    const { controlPromise } = stubbedControl()
    const control = await controlPromise
    await expect(control.renderAudio({ filePath: 'out.mp3', totalBeats: 4 }))
      .rejects.toThrow('must end in .wav')
  })

  it('requires a positive length when the arrangement is empty', async () => {
    const { controlPromise } = stubbedControl()
    const control = await controlPromise
    await expect(control.renderAudio({ filePath: 'out.wav' }))
      .rejects.toThrow('must be positive')
  })

  it('caps the render length', async () => {
    const { controlPromise } = stubbedControl()
    const control = await controlPromise
    await expect(control.renderAudio({ filePath: 'out.wav', totalBeats: 512 }))
      .rejects.toThrow('must not exceed 256')
  })

  it('resolves below allowed roots and forwards graph, arrangement, and tempo', async () => {
    const calls = []
    const { controlPromise } = stubbedControl(calls)
    const control = await controlPromise
    const rendered = await control.renderAudio({ filePath: 'bounce.wav', totalBeats: 4 })
    expect(rendered).toMatchObject({ framesWritten: 48000, peak: 0.5 })
    expect(rendered.filePath).toBe(join(directory, 'bounce.wav'))
    expect(calls).toHaveLength(1)
    expect(calls[0]).toEqual({
      outputPath: join(directory, 'bounce.wav'),
      totalBeats: 4,
      tempo: undefined,
      sampleRate: 48000,
      blockSize: 1024
    })
  })

  it('refuses paths outside the allowed roots', async () => {
    const { controlPromise } = stubbedControl()
    const control = await controlPromise
    await expect(control.renderAudio({ filePath: '../outside.wav', totalBeats: 4 }))
      .rejects.toThrow('outside the allowed roots')
  })
})
