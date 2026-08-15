import { execFile } from 'node:child_process'
import { availableParallelism } from 'node:os'
import { basename, join } from 'node:path'
import { readdir } from 'node:fs/promises'
import { promisify } from 'node:util'

const executeFile = promisify(execFile)

export class Vst3Discovery {
  constructor({ inspectorPath, timeoutMs = 5000, concurrency = Math.min(8, Math.max(1, availableParallelism())) } = {}) {
    if (!inspectorPath) throw new TypeError('VST3 discovery requires an inspector executable')
    this.inspectorPath = inspectorPath
    this.timeoutMs = timeoutMs
    this.concurrency = concurrency
  }

  async scan(roots) {
    const bundles = []
    for (const root of roots) bundles.push(...await findBundles(root))
    const plugins = []
    const failures = []
    let nextIndex = 0
    const worker = async () => {
      while (nextIndex < bundles.length) {
        const bundlePath = bundles[nextIndex]
        nextIndex += 1
        try {
          const { stdout } = await executeFile(this.inspectorPath, [bundlePath], {
            timeout: this.timeoutMs,
            maxBuffer: 1024 * 1024,
            encoding: 'utf8',
            shell: process.platform === 'win32'
          })
          plugins.push(...parseInspectorOutput(stdout, bundlePath))
        } catch (error) {
          failures.push({
            modulePath: bundlePath,
            error: error.killed ? `inspection timed out after ${this.timeoutMs}ms` : firstLine(error.stderr || error.message)
          })
        }
      }
    }
    await Promise.all(Array.from({ length: Math.min(this.concurrency, bundles.length) }, worker))
    return { plugins, failures }
  }
}

export function parseInspectorOutput(text, modulePath) {
  const descriptors = []
  let descriptor = null
  const topology = { audioInputs: 0, audioOutputs: 0, midiInputs: 0, midiOutputs: 0 }
  const parameters = new Map()
  for (const line of text.split(/\r?\n/)) {
    const separator = line.indexOf('=')
    if (separator < 0) continue
    const key = line.slice(0, separator)
    const value = line.slice(separator + 1)
    if (key === 'id') {
      if (descriptor) descriptors.push(descriptor)
      descriptor = { classId: value }
    } else if (descriptor && ['name', 'vendor', 'category', 'module'].includes(key)) {
      descriptor[key === 'module' ? 'modulePath' : key] = value
    } else if (Object.hasOwn(topology, key)) {
      topology[key] = Number(value)
    } else {
      const match = /^parameter\.(\d+)\.(id|title|shortTitle|units|defaultNormalized|stepCount|flags)$/.exec(key)
      if (!match) continue
      const index = Number(match[1])
      const property = match[2]
      const parameter = parameters.get(index) ?? {}
      parameter[property] = ['id', 'defaultNormalized', 'stepCount', 'flags'].includes(property)
        ? Number(value)
        : value
      parameters.set(index, parameter)
    }
  }
  if (descriptor) descriptors.push(descriptor)
  if (!descriptors.length) {
    throw new Error('inspector output did not contain a plugin descriptor')
  }
  const processorDescriptors = descriptors.filter(item => item.category === 'Audio Module Class')
  return (processorDescriptors.length ? processorDescriptors : descriptors).map(item => {
    if (!item.classId || !item.name) {
      throw new Error('inspector output requires non-empty id and name fields')
    }
    const facts = {
      ...item,
      modulePath: item.modulePath || modulePath,
      bundleName: basename(modulePath),
      ...topology,
      parameters: [...parameters.entries()].sort(([left], [right]) => left - right).map(([, value]) => value)
    }
    return {
      ...facts,
      roles: inferRoles(facts),
      produces: inferProduces(facts),
      accepts: inferAccepts(facts),
      discoverySource: 'vst3-inspector'
    }
  })
}

async function findBundles(root) {
  const bundles = []
  const visit = async directory => {
    let entries
    try {
      entries = await readdir(directory, { withFileTypes: true })
    } catch {
      return
    }
    for (const entry of entries) {
      const path = join(directory, entry.name)
      if (entry.name.toLowerCase().endsWith('.vst3')) bundles.push(path)
      else if (entry.isDirectory()) await visit(path)
    }
  }
  await visit(root)
  return bundles
}

function inferRoles(plugin) {
  const roles = []
  if (plugin.midiOutputs > 0) roles.push(plugin.midiInputs > 0 ? 'MidiProcessor' : 'MidiGenerator')
  if (plugin.audioInputs > 0 && plugin.audioOutputs > 0) roles.push('AudioEffect')
  if (plugin.midiInputs > 0 && plugin.audioOutputs > 0 && plugin.midiOutputs === 0) roles.push('Instrument')
  if (!roles.length && plugin.audioOutputs > 0) roles.push('AudioGenerator')
  return roles
}

function inferProduces(plugin) {
  const result = []
  if (plugin.midiOutputs > 0) result.push('Midi')
  if (plugin.audioOutputs > 0) result.push('Audio')
  return result
}

function inferAccepts(plugin) {
  const result = []
  if (plugin.midiInputs > 0) result.push('Midi')
  if (plugin.audioInputs > 0) result.push('Audio')
  return result
}

function firstLine(value) {
  return String(value).split(/\r?\n/)[0]
}
