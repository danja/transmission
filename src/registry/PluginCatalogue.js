import { readFile } from 'node:fs/promises'
import { parsePluginProfiles, serializeDiscoveredPlugins } from '../rdf/PluginProfileRdf.js'

export class PluginCatalogue {
  #profiles = new Map()
  #discoveries = new Map()
  #profileSources = new Map()

  constructor({ discovery = null } = {}) {
    this.discovery = discovery
    this.scanFailures = []
    this.scanState = 'idle'
    this.scanPromise = null
  }

  async loadProfileFile(filePath) {
    const text = await readFile(filePath, 'utf8')
    const profiles = await parsePluginProfiles(text, filePath)
    for (const profile of profiles) this.addProfile(profile)
    this.#profileSources.set(filePath, text)
    return profiles.length
  }

  addProfile(profile) {
    if (!profile?.id || !profile.name) throw new TypeError('A plugin profile requires id and name')
    this.#profiles.set(profile.id, freezeProfile(profile))
  }

  replaceDiscoveries(discoveries) {
    this.#discoveries.clear()
    for (const discovery of discoveries) {
      if (!discovery?.classId) throw new TypeError('Discovered plugins require a VST3 classId')
      this.#discoveries.set(discovery.classId, freezeProfile(discovery))
    }
  }

  async scan(roots) {
    if (!this.discovery) throw new Error('VST3 discovery is unavailable')
    const result = await this.discovery.scan(roots)
    this.replaceDiscoveries(result.plugins)
    this.scanFailures = result.failures
    return {
      discovered: result.plugins.length,
      profiled: this.list().filter(plugin => plugin.profileSource).length,
      failures: result.failures
    }
  }

  startScan(roots) {
    if (this.scanState === 'scanning') return this.scanPromise
    this.scanState = 'scanning'
    this.scanPromise = this.scan(roots).finally(() => {
      this.scanState = 'idle'
    })
    return this.scanPromise
  }

  ready() {
    return this.scanPromise ?? Promise.resolve(null)
  }

  list() {
    const entries = []
    const matchedDiscoveries = new Set()
    for (const profile of this.#profiles.values()) {
      const discovery = [...this.#discoveries.values()].find(candidate => matches(profile, candidate))
      if (discovery) matchedDiscoveries.add(discovery.classId)
      entries.push(merge(profile, discovery))
    }
    for (const discovery of this.#discoveries.values()) {
      if (!matchedDiscoveries.has(discovery.classId)) entries.push(merge(null, discovery))
    }
    return entries.sort((left, right) => left.name.localeCompare(right.name))
  }

  get(identifier) {
    if (!identifier) return null
    const wanted = identifier.toLowerCase()
    return this.list().find(plugin =>
      [plugin.id, plugin.profileId, plugin.classId, plugin.bundleName, plugin.modulePath, plugin.name]
        .filter(Boolean)
        .some(value => value.toLowerCase() === wanted)
    ) ?? null
  }

  search({ query = '', roles = [], produces = [], accepts = [], vendor = '', installedOnly = false } = {}) {
    const words = query.toLowerCase().split(/\s+/).filter(Boolean)
    return this.list().filter(plugin => {
      if (installedOnly && !plugin.installed) return false
      if (vendor && plugin.vendor.toLowerCase() !== vendor.toLowerCase()) return false
      if (!includesAll(plugin.roles, roles) || !includesAll(plugin.produces, produces) || !includesAll(plugin.accepts, accepts)) return false
      const haystack = [
        plugin.name, plugin.vendor, plugin.description, plugin.bundleName,
        ...plugin.roles, ...plugin.produces, ...plugin.accepts, ...plugin.genres
      ].join(' ').toLowerCase()
      return words.every(word => haystack.includes(word))
    })
  }

  validateChain(identifiers) {
    if (!Array.isArray(identifiers) || identifiers.length < 2) {
      throw new TypeError('A plugin chain requires at least two plugin identifiers')
    }
    const plugins = identifiers.map(identifier => {
      const plugin = this.get(identifier)
      if (!plugin) throw new Error(`Unknown plugin: ${identifier}`)
      return plugin
    })
    const links = []
    for (let index = 0; index < plugins.length - 1; index += 1) {
      links.push(validateLink(plugins[index], plugins[index + 1]))
    }
    return {
      valid: links.every(link => link.compatible),
      chain: plugins.map(summary),
      links
    }
  }

  status() {
    const plugins = this.list()
    return {
      plugins: plugins.length,
      installed: plugins.filter(plugin => plugin.installed).length,
      profiled: plugins.filter(plugin => plugin.profileSource).length,
      scanFailureCount: this.scanFailures.length,
      scanState: this.scanState
    }
  }

  profilesTurtle() {
    return [...this.#profileSources.values()].join('\n')
  }

  discoveredTurtle() {
    return serializeDiscoveredPlugins([...this.#discoveries.values()])
  }
}

function merge(profile, discovery) {
  const technical = discovery ?? {}
  const curated = profile ?? {}
  return Object.freeze({
    ...technical,
    ...curated,
    id: curated.id ?? `urn:vst3:${technical.classId}`,
    profileId: curated.id ?? null,
    classId: technical.classId ?? curated.vstClassId ?? '',
    name: curated.name ?? technical.name,
    vendor: technical.vendor ?? curated.vendor ?? '',
    bundleName: technical.bundleName ?? curated.bundleName ?? '',
    modulePath: technical.modulePath ?? null,
    category: technical.category ?? '',
    audioInputs: technical.audioInputs ?? null,
    audioOutputs: technical.audioOutputs ?? null,
    midiInputs: technical.midiInputs ?? null,
    midiOutputs: technical.midiOutputs ?? null,
    roles: unique([...(technical.roles ?? []), ...(curated.roles ?? [])]),
    technicalProduces: unique(technical.produces ?? []),
    technicalAccepts: unique(technical.accepts ?? []),
    produces: curated.produces?.length ? unique(curated.produces) : unique(technical.produces ?? []),
    accepts: curated.accepts?.length ? unique(curated.accepts) : unique(technical.accepts ?? []),
    requirements: unique(curated.requirements ?? []),
    recommendedBefore: unique(curated.recommendedBefore ?? []),
    recommendedAfter: unique(curated.recommendedAfter ?? []),
    companions: unique(curated.companions ?? []),
    genres: unique(curated.genres ?? []),
    cautions: unique(curated.cautions ?? []),
    installed: Boolean(discovery)
  })
}

function matches(profile, discovery) {
  if (profile.vstClassId && profile.vstClassId === discovery.classId) return true
  if (profile.bundleName && profile.bundleName.toLowerCase() === discovery.bundleName.toLowerCase()) return true
  return profile.name.toLowerCase() === discovery.name.toLowerCase() &&
    (!profile.vendor ||
      profile.vendor.toLowerCase() === (discovery.vendor ?? '').toLowerCase())
}

function validateLink(source, target) {
  const sourceMidi = source.produces.some(isMidi)
  const sourceAudio = source.produces.includes('Audio')
  const targetMidi = target.accepts.some(isMidi)
  const targetAudio = target.accepts.includes('Audio')
  const kinds = []
  if (sourceMidi && targetMidi) kinds.push('midi')
  if (sourceAudio && targetAudio) kinds.push('audio')
  const cautions = []
  if (source.recommendedBefore.includes(target.id)) cautions.push('This is a curated recommended pairing.')
  if (source.cautions.length) cautions.push(...source.cautions)
  return {
    from: source.id,
    to: target.id,
    compatible: kinds.length > 0,
    connectionKinds: kinds,
    message: kinds.length
      ? `Compatible via ${kinds.join(' and ')}.`
      : `No compatible produced/accepted signal is described between ${source.name} and ${target.name}.`,
    notes: cautions
  }
}

function isMidi(value) {
  return value === 'Midi' || value.endsWith('Midi')
}

function summary(plugin) {
  return {
    id: plugin.id,
    name: plugin.name,
    roles: plugin.roles,
    installed: plugin.installed
  }
}

function includesAll(actual, wanted) {
  const normalized = new Set(actual.map(value => value.toLowerCase()))
  return wanted.every(value => normalized.has(value.toLowerCase()))
}

function unique(values) {
  return [...new Set(values)]
}

function freezeProfile(profile) {
  return Object.freeze(structuredClone(profile))
}
