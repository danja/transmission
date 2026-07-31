import { spawn } from 'node:child_process'
import { mkdtemp, readFile, rm, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { afterEach, describe, expect, it } from 'vitest'

const temporaryDirectories = []

afterEach(async () => {
  await Promise.all(temporaryDirectories.splice(0).map(path => rm(path, { recursive: true, force: true })))
})

describe('native UI Turtle project helper', () => {
  it('round trips graph, editor, device, plugin, and transport state', async () => {
    const directory = await mkdtemp(join(tmpdir(), 'transmission-native-ui-'))
    temporaryDirectories.push(directory)
    const filePath = join(directory, 'drums.ttl')
    const interchange = [
      'TRANSMISSION_UI\t3',
      `PROJECT\t${hex('main')}\t${hex('Drum graph')}`,
      'TRANSPORT\t132\t8\t1',
      `INPUT\t0\t${hex('capture:left')}`,
      `INPUT\t1\t${hex('capture:right')}`,
      `OUTPUT\t0\t${hex('playback:left')}`,
      `OUTPUT\t1\t${hex('playback:right')}`,
      `NODE\t${hex('system-input')}\t${hex('System Input')}\t0\t0\t2\t0\t1\t20\t40\t-`,
      `NODE\t${hex('drumgen')}\t${hex('drumgen')}\t3\t0\t2\t1\t1\t260\t40\t${hex('/home/test/drumgen.vst3')}`,
      `PARAM\t${hex('drumgen')}\t42\t0.75`,
      `STATE\t${hex('drumgen')}\t000102ff\t1020`,
      `NODE\t${hex('drumkit')}\t${hex('drumkit')}\t3\t2\t2\t1\t1\t500\t40\t${hex('/home/test/drumkit.vst3')}`,
      `NODE\t${hex('midi-output-1')}\t${hex('MIDI Output')}\t5\t0\t0\t1\t0\t620\t180\t${hex('synth-42:midi_in')}`,
      `NODE\t${hex('system-output')}\t${hex('System Output')}\t1\t2\t0\t1\t0\t740\t40\t-`,
      `NODE\t${hex('master-gain')}\t${hex('Master Gain')}\t6\t2\t2\t0\t0\t620\t40\t-`,
      `NODE_GAIN\t${hex('master-gain')}\t-3`,
      `EDGE\t${hex('system-input')}\t${hex('drumgen')}\t1\t0\t0`,
      `EDGE\t${hex('drumgen')}\t${hex('drumkit')}\t1\t0\t0`,
      `EDGE\t${hex('drumgen')}\t${hex('midi-output-1')}\t1\t0\t0`,
      `EDGE\t${hex('drumkit')}\t${hex('system-output')}\t0\t0\t0`,
      'ARRANGEMENT\t16',
      `CLIP\t${hex('intro')}\t${hex('drumgen')}\t0\t4`,
      `NOTE\t${hex('intro')}\t0.5\t0.25\t36\t100\t9`,
      `GAIN_LANE\t${hex('master-gain')}`,
      `GAIN_POINT\t${hex('master-gain')}\t12\t0\t1`,
      `GAIN_POINT\t${hex('master-gain')}\t16\t-120\t1`,
      'END',
      ''
    ].join('\n')

    await runHelper('save', filePath, interchange)
    const turtle = await readFile(filePath, 'utf8')
    expect(turtle).toContain(':connections')
    expect(turtle).toContain('/home/test/drumgen.vst3')
    expect(turtle).toContain('playback:left')
    expect(turtle).toContain(':parameterId 42')
    expect(turtle).toContain(':normalizedValue 0.75')
    expect(turtle).toContain(':componentState "AAEC/w=="')
    expect(turtle).toContain(':controllerState "ECA="')

    const restored = await runHelper('load', filePath)
    expect(restored).toContain('TRANSMISSION_UI\t3')
    expect(restored).toContain('TRANSPORT\t132\t8\t1')
    expect(restored).toContain(`NODE\t${hex('drumgen')}`)
    expect(restored).toContain(`EDGE\t${hex('drumgen')}\t${hex('drumkit')}\t1\t0\t0`)
    expect(restored).toContain(`NODE\t${hex('midi-output-1')}\t${hex('MIDI Output')}\t5`)
    expect(restored).toContain(hex('synth-42:midi_in'))
    expect(restored).toContain(`OUTPUT\t0\t${hex('playback:left')}`)
    expect(restored).toContain(`PARAM\t${hex('drumgen')}\t42\t0.75`)
    expect(restored).toContain(`STATE\t${hex('drumgen')}\t000102ff\t1020`)
    expect(restored).toContain(`NOTE\t${hex('intro')}\t0.5\t0.25\t36\t100\t9`)
    expect(restored).toContain(`GAIN_POINT\t${hex('master-gain')}\t16\t-120\t1`)
  }, 15_000)

  it('rejects a Turtle file without a Transmission project', async () => {
    const directory = await mkdtemp(join(tmpdir(), 'transmission-native-ui-'))
    temporaryDirectories.push(directory)
    const filePath = join(directory, 'invalid.ttl')
    await writeFile(filePath, '@prefix : <http://example.test/> . :thing :value 1 .\n')

    await expect(runHelper('load', filePath)).rejects.toThrow('No transmission found')
  }, 15_000)

  it('writes the complete Gen Omega interchange through a pipe', async () => {
    const restored = await runHelper(
      'load', join(process.cwd(), 'projects/gen-omega.ttl'))
    expect(Buffer.byteLength(restored)).toBeGreaterThan(65_536)
    expect(restored.match(/^NOTE\t/gm)).toHaveLength(2059)
    expect(restored).toContain(
      `GAIN_POINT\t${hex('master-gain')}\t420\t-120\t1`)
    expect(restored.endsWith('END\n')).toBe(true)
  }, 15_000)
})

function hex(value) {
  return Buffer.from(value, 'utf8').toString('hex')
}

async function runHelper(command, filePath, input = '') {
  const interchangePath = `${filePath}.interchange`
  const argumentsList = ['scripts/native-ui-project.js', command, filePath]
  if (input) {
    await writeFile(interchangePath, input)
    argumentsList.push(interchangePath)
  }
  return new Promise((resolve, reject) => {
    const child = spawn(process.execPath, argumentsList, {
      cwd: process.cwd(),
      stdio: ['pipe', 'pipe', 'pipe']
    })
    let output = ''
    let error = ''
    child.stdout.setEncoding('utf8')
    child.stderr.setEncoding('utf8')
    child.stdout.on('data', chunk => { output += chunk })
    child.stderr.on('data', chunk => { error += chunk })
    child.on('error', reject)
    child.on('close', code => {
      if (code === 0) resolve(output)
      else reject(new Error(error.trim() || `Project helper exited with code ${code}`))
    })
    child.stdin.end()
  })
}
