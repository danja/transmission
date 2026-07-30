#!/usr/bin/env node

import { spawnSync } from 'node:child_process'
import { existsSync, mkdtempSync, rmSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { dirname, join, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'

const repositoryRoot = resolve(dirname(fileURLToPath(import.meta.url)), '..')
const argumentsList = process.argv.slice(2)
const projectPath = argumentsList.shift()
let explicitProbe = null
const probeOption = argumentsList.indexOf('--probe')
if (probeOption >= 0) {
  explicitProbe = argumentsList[probeOption + 1]
  argumentsList.splice(probeOption, 2)
}

if (!projectPath || (probeOption >= 0 && !explicitProbe)) {
  process.stderr.write(
    'Usage: npm run probe:project -- <project.ttl> [--seconds N] ' +
    '[--block-size N] [--sample-rate N] [--probe FILE]\n')
  process.exit(2)
}

const candidates = explicitProbe
  ? [resolve(explicitProbe)]
  : [
      join(repositoryRoot, 'native/build-ui-jack-vst3/transmission_vst3_project_probe'),
      join(repositoryRoot, 'native/build-vst3/transmission_vst3_project_probe')
    ]
const probePath = candidates.find(existsSync)
if (!probePath) {
  process.stderr.write(
    'Headless project probe is not built. Build target ' +
    'transmission_vst3_project_probe first.\n')
  process.exit(1)
}

const helper = spawnSync(
  process.execPath,
  [join(repositoryRoot, 'scripts/native-ui-project.js'), 'load', resolve(projectPath)],
  { encoding: null, maxBuffer: 64 * 1024 * 1024 })
if (helper.status !== 0) {
  process.stderr.write(helper.stderr ?? Buffer.from('Unable to load project\n'))
  process.exit(helper.status ?? 1)
}

const temporaryDirectory = mkdtempSync(join(tmpdir(), 'transmission-probe-'))
const interchangePath = join(temporaryDirectory, 'project.interchange')
try {
  writeFileSync(interchangePath, helper.stdout)
  const result = spawnSync(probePath, [interchangePath, ...argumentsList], {
    stdio: 'inherit'
  })
  process.exitCode = result.status ?? 1
} finally {
  rmSync(temporaryDirectory, { recursive: true, force: true })
}
