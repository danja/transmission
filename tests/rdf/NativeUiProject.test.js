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
      'TRANSMISSION_UI\t1',
      `PROJECT\t${hex('main')}\t${hex('Drum graph')}`,
      'TRANSPORT\t132\t8\t1',
      `INPUT\t0\t${hex('capture:left')}`,
      `INPUT\t1\t${hex('capture:right')}`,
      `OUTPUT\t0\t${hex('playback:left')}`,
      `OUTPUT\t1\t${hex('playback:right')}`,
      `NODE\t${hex('system-input')}\t${hex('System Input')}\t0\t0\t2\t0\t1\t20\t40\t-`,
      `NODE\t${hex('drumgen')}\t${hex('drumgen')}\t3\t0\t2\t1\t1\t260\t40\t${hex('/home/test/drumgen.vst3')}`,
      `NODE\t${hex('drumkit')}\t${hex('drumkit')}\t3\t2\t2\t1\t1\t500\t40\t${hex('/home/test/drumkit.vst3')}`,
      `NODE\t${hex('system-output')}\t${hex('System Output')}\t1\t2\t0\t1\t0\t740\t40\t-`,
      `EDGE\t${hex('system-input')}\t${hex('drumgen')}\t1\t0\t0`,
      `EDGE\t${hex('drumgen')}\t${hex('drumkit')}\t1\t0\t0`,
      `EDGE\t${hex('drumkit')}\t${hex('system-output')}\t0\t0\t0`,
      'END',
      ''
    ].join('\n')

    await runHelper('save', filePath, interchange)
    const turtle = await readFile(filePath, 'utf8')
    expect(turtle).toContain(':connections')
    expect(turtle).toContain('/home/test/drumgen.vst3')
    expect(turtle).toContain('playback:left')

    const restored = await runHelper('load', filePath)
    expect(restored).toContain('TRANSPORT\t132\t8\t1')
    expect(restored).toContain(`NODE\t${hex('drumgen')}`)
    expect(restored).toContain(`EDGE\t${hex('drumgen')}\t${hex('drumkit')}\t1\t0\t0`)
    expect(restored).toContain(`OUTPUT\t0\t${hex('playback:left')}`)
  }, 15_000)

  it('rejects a Turtle file without a Transmission project', async () => {
    const directory = await mkdtemp(join(tmpdir(), 'transmission-native-ui-'))
    temporaryDirectories.push(directory)
    const filePath = join(directory, 'invalid.ttl')
    await writeFile(filePath, '@prefix : <http://example.test/> . :thing :value 1 .\n')

    await expect(runHelper('load', filePath)).rejects.toThrow('No transmission found')
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
