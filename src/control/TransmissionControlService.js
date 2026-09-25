import { isAbsolute, relative, resolve } from 'node:path'
import { compileGraph } from '../compiler/GraphCompiler.js'
import { ProjectSession } from '../session/ProjectSession.js'
import { serializeGraph } from '../rdf/TransmissionRdf.js'
import { jigdawGraphNode, readJigdawProfile } from '../registry/JigdawProfile.js'

export class ProjectRevisionError extends Error {
  constructor(expected, actual) {
    super(`Project revision mismatch: expected ${expected}, current revision is ${actual}`)
    this.name = 'ProjectRevisionError'
    this.expected = expected
    this.actual = actual
  }
}

export class TransmissionControlService {
  #changeListeners = new Set()

  constructor({
    project = new ProjectSession(),
    engine = null,
    allowedRoots = [process.cwd()],
    pluginCatalogue = null,
    pluginRoots = [],
    defaultOutputConnections = null
  } = {}) {
    this.project = engine?.project ?? project
    this.engine = engine
    this.pluginCatalogue = pluginCatalogue
    this.pluginRoots = [...pluginRoots]
    this.allowedRoots = allowedRoots.map(root => resolve(root))
    this.defaultOutputConnections = defaultOutputConnections
    if (!this.allowedRoots.length) throw new TypeError('At least one allowed project root is required')
  }

  onStatusChange(listener) {
    this.#changeListeners.add(listener)
    return () => this.#changeListeners.delete(listener)
  }

  #emitStatusChange() {
    if (this.#changeListeners.size === 0) return
    const status = this.status()
    for (const listener of this.#changeListeners) listener(status)
  }

  status() {
    return {
      projectOpen: Boolean(this.project.graph),
      projectId: this.project.graph?.id ?? null,
      filePath: this.project.filePath,
      revision: this.project.revision,
      generation: this.project.generation,
      dirty: Boolean(this.project.graph) &&
        (!this.project.filePath || this.project.savedRevision !== this.project.revision),
      engineAvailable: Boolean(this.engine),
      engineState: this.engine?.state ?? 'unavailable',
      transport: this.project.transport.toJSON()
    }
  }

  describeProject() {
    this.#requireProject()
    return {
      ...this.status(),
      graph: this.project.graph.toJSON(),
      arrangement: this.project.arrangement.toJSON(),
      executionOrder: [...this.project.compiledGraph.executionOrder]
    }
  }

  projectTurtle() {
    this.#requireProject()
    return serializeGraph(this.project.graph, this.project.transport.toJSON(), this.project.arrangement)
  }

  newProject(definition) {
    const metadata = { ...definition.metadata }
    if (this.defaultOutputConnections &&
        !metadata.systemOutputConnections?.length) {
      metadata.systemOutputConnections = this.defaultOutputConnections
    }
    const normalized = {
      id: definition.id,
      label: definition.label ?? '',
      nodes: definition.nodes ?? [],
      connections: definition.connections ?? [],
      metadata,
      transport: definition.transport,
      arrangement: definition.arrangement
    }
    if (this.engine) this.engine.open(normalized)
    else this.project.open(normalized)
    this.#emitStatusChange()
    return this.describeProject()
  }

  async openProject(filePath) {
    this.#requireStopped()
    const resolvedPath = this.#allowedPath(filePath)
    const loaded = await ProjectSession.load(resolvedPath, { compiler: this.project.compiler })
    const definition = {
      ...loaded.graph.toJSON(),
      transport: loaded.transport.toJSON(),
      arrangement: loaded.arrangement.toJSON()
    }
    if (this.engine) this.engine.open(definition, resolvedPath)
    else this.project.open(definition, resolvedPath)
    this.#emitStatusChange()
    return this.describeProject()
  }

  async saveProject(filePath = null) {
    this.#requireProject()
    const target = filePath ? this.#allowedPath(filePath) : this.project.filePath
    if (!target) throw new Error('A project file path is required')
    if (!this.#isAllowed(target)) throw new Error(`Project path is outside the allowed roots: ${target}`)
    await this.project.save(target)
    this.#emitStatusChange()
    return { filePath: target, revision: this.project.revision }
  }

  applyGraphChanges({ expectedRevision, operations, dryRun = false }) {
    this.#requireProject()
    this.#requireStopped()
    this.#checkRevision(expectedRevision)
    if (!Array.isArray(operations) || operations.length === 0) throw new TypeError('At least one graph operation is required')
    const nextDefinition = applyGraphOperations(this.project.graph.toJSON(), operations)
    const compiled = compileGraph(nextDefinition)
    if (!dryRun) {
      if (this.engine) this.engine.update(() => nextDefinition)
      else this.project.update(() => nextDefinition)
      this.#emitStatusChange()
    }
    return {
      dryRun,
      revision: this.project.revision,
      executionOrder: [...compiled.executionOrder],
      graph: nextDefinition
    }
  }

  configureTransport({ expectedRevision, tempo, atBeat, loop, clearLoop = false, positionBeats }) {
    this.#requireProject()
    this.#requireStopped()
    this.#checkRevision(expectedRevision)
    const transport = this.project.transport
    if (tempo !== undefined) transport.setTempo(tempo, atBeat ?? transport.positionBeats)
    if (clearLoop) transport.clearLoop()
    else if (loop) transport.setLoop(loop.startBeat, loop.endBeat, loop.enabled !== false)
    if (positionBeats !== undefined) transport.seek(positionBeats)
    if (this.engine) this.engine.synchronizeTransport()
    this.project.markChanged()
    this.#emitStatusChange()
    return { revision: this.project.revision, transport: transport.toJSON() }
  }

  startTransport() {
    this.#requireProject()
    if (!this.engine) throw new Error('Native audio is unavailable; start the MCP server with --native-addon')
    this.engine.start()
    this.#emitStatusChange()
    return this.status()
  }

  stopTransport() {
    if (this.engine) this.engine.stop()
    else this.project.transport.stop()
    this.#emitStatusChange()
    return this.status()
  }

  setParameters({ expectedRevision, nodeId, parameters, sampleOffset = 0 }) {
    this.#requireProject()
    this.#checkRevision(expectedRevision)
    const node = this.project.graph.node(nodeId)
    if (!node) throw new Error(`Node does not exist: ${nodeId}`)
    const definition = this.project.graph.toJSON()
    const nextDefinition = {
      ...definition,
      nodes: definition.nodes.map(current => {
        if (current.id !== nodeId) return current
        const kept = current.parameters.filter(p => !parameters.some(up => up.id === p.id))
        const updated = parameters.map(({ id, normalizedValue }) => {
          if (!Number.isInteger(id) || id < 0) throw new RangeError(`parameterId must be a non-negative integer: ${id}`)
          if (!Number.isFinite(normalizedValue) || normalizedValue < 0 || normalizedValue > 1)
            throw new RangeError(`value must be between 0 and 1 for parameter ${id}`)
          return { id, normalizedValue }
        })
        const merged = [...kept, ...updated].sort((a, b) => a.id - b.id)
        return { ...current, parameters: merged }
      })
    }
    compileGraph(nextDefinition)
    if (this.engine) {
      for (const { id, normalizedValue } of parameters)
        this.engine.setParameter(nodeId, id, normalizedValue, sampleOffset)
    }
    this.project.update(() => nextDefinition)
    return {
      revision: this.project.revision,
      nodeId,
      parameters,
      appliedToRuntime: Boolean(this.engine)
    }
  }

  setParameter({ expectedRevision, nodeId, parameterId, value, sampleOffset = 0 }) {
    this.#requireProject()
    this.#checkRevision(expectedRevision)
    if (!Number.isInteger(parameterId) || parameterId < 0) throw new RangeError('parameterId must be a non-negative integer')
    if (!Number.isFinite(value) || value < 0 || value > 1) throw new RangeError('value must be between 0 and 1')
    const node = this.project.graph.node(nodeId)
    if (!node) throw new Error(`Node does not exist: ${nodeId}`)
    const definition = this.project.graph.toJSON()
    const nextDefinition = {
      ...definition,
      nodes: definition.nodes.map(current => {
        if (current.id !== nodeId) return current
        const parameters = current.parameters.filter(parameter => parameter.id !== parameterId)
        parameters.push({ id: parameterId, normalizedValue: value })
        parameters.sort((a, b) => a.id - b.id)
        return { ...current, parameters }
      })
    }
    compileGraph(nextDefinition)
    if (this.engine) this.engine.setParameter(nodeId, parameterId, value, sampleOffset)
    this.project.update(() => nextDefinition)
    return {
      revision: this.project.revision,
      nodeId,
      parameterId,
      value,
      appliedToRuntime: Boolean(this.engine)
    }
  }

  getArrangement() {
    this.#requireProject()
    return { revision: this.project.revision, arrangement: this.project.arrangement.toJSON() }
  }

  updateArrangement({ expectedRevision, lengthBeats, midiClips, gainLanes }) {
    this.#requireProject()
    this.#checkRevision(expectedRevision)
    this.project.updateArrangement(current => ({
      lengthBeats: lengthBeats ?? current.lengthBeats,
      midiClips: midiClips ?? current.midiClips,
      gainLanes: gainLanes ?? current.gainLanes
    }))
    return { revision: this.project.revision, arrangement: this.project.arrangement.toJSON() }
  }

  addArrangementClip({ expectedRevision, clip }) {
    this.#requireProject()
    this.#checkRevision(expectedRevision)
    this.project.updateArrangement(current => {
      if (current.midiClips.some(c => c.id === clip.id))
        throw new Error(`MIDI clip already exists: ${clip.id}`)
      return { ...current, midiClips: [...current.midiClips, clip] }
    })
    return { revision: this.project.revision, arrangement: this.project.arrangement.toJSON() }
  }

  removeArrangementClip({ expectedRevision, clipId }) {
    this.#requireProject()
    this.#checkRevision(expectedRevision)
    this.project.updateArrangement(current => ({
      ...current,
      midiClips: current.midiClips.filter(c => c.id !== clipId)
    }))
    return { revision: this.project.revision, arrangement: this.project.arrangement.toJSON() }
  }

  freezeGenerator({ expectedRevision, sourceNodeId, targetNodeId, clipId, startBeat = 0, lengthBeats, durationBeats }) {
    this.#requireProject()
    this.#requireStopped()
    if (!this.engine) throw new Error('Native engine is required to freeze a generator; start the MCP server with --native-addon')
    this.#checkRevision(expectedRevision)
    if (!sourceNodeId || typeof sourceNodeId !== 'string') throw new TypeError('freezeGenerator sourceNodeId is required')
    if (!this.project.graph.node(sourceNodeId)) throw new Error(`Freeze source does not exist: ${sourceNodeId}`)
    const arrangement = this.project.arrangement.toJSON()
    const beats = durationBeats ?? arrangement.lengthBeats
    if (!Number.isFinite(beats) || beats <= 0) {
      throw new RangeError('freezeGenerator durationBeats must be positive (the arrangement length is 0; pass durationBeats explicitly)')
    }
    const events = this.engine.captureMidi(beats)
    const clip = freezeClipFromEvents(events, {
      sourceNodeId,
      targetNodeId,
      clipId: clipId ?? `freeze-${sourceNodeId}-${startBeat}`,
      startBeat,
      lengthBeats: lengthBeats ?? beats
    })
    this.project.updateArrangement(current => {
      if (current.midiClips.some(c => c.id === clip.id))
        throw new Error(`MIDI clip already exists: ${clip.id}`)
      return { ...current, midiClips: [...current.midiClips, clip] }
    })
    return { revision: this.project.revision, arrangement: this.project.arrangement.toJSON(), clip }
  }

  diagnostics() {
    let native = null
    if (this.engine) native = this.engine.diagnostics()
    const peaks = this.peaks()
    return { ...this.status(), native, peaks }
  }

  peaks() {
    if (this.engine) return this.engine.peaks()
    return { peakL: 0, peakR: 0 }
  }

  plugins({ installedOnly = false } = {}) {
    this.#requirePluginCatalogue()
    return {
      ...this.pluginCatalogue.status(),
      scanFailures: this.pluginCatalogue.scanFailures,
      entries: this.pluginCatalogue.list()
        .filter(plugin => !installedOnly || plugin.installed)
        .map(compactPlugin)
    }
  }

  searchPlugins(query) {
    this.#requirePluginCatalogue()
    const entries = this.pluginCatalogue.search(query)
    return { matches: entries.length, entries: entries.map(compactPlugin) }
  }

  describePlugin(identifier) {
    this.#requirePluginCatalogue()
    const plugin = this.pluginCatalogue.get(identifier)
    if (!plugin) throw new Error(`Unknown plugin: ${identifier}`)
    return plugin
  }

  /**
   * Dereference a JigDAW plugin IRI and report what it is, what it needs and
   * what a node for it should look like.
   *
   * There is no catalogue to consult and no scan to wait for: a JigDAW plugin's
   * identity, its metadata and its delivery are one thing, so describing it is
   * fetching it. The returned `node` carries the port counts a project must
   * declare, because the native host overwrites whatever a project guessed and
   * a mismatch fails validation with nothing useful to say.
   */
  async describeJigdawPlugin(iri, { id = 'jigdaw-1' } = {}) {
    const profile = await readJigdawProfile(iri)
    return { profile, node: jigdawGraphNode(profile, { id }) }
  }

  validatePluginChain(identifiers) {
    this.#requirePluginCatalogue()
    return this.pluginCatalogue.validateChain(identifiers)
  }

  async scanPlugins() {
    this.#requirePluginCatalogue()
    return this.pluginCatalogue.startScan(this.pluginRoots)
  }

  async waitForPluginScan() {
    this.#requirePluginCatalogue()
    await this.pluginCatalogue.ready()
  }

  pluginProfilesTurtle() {
    this.#requirePluginCatalogue()
    return this.pluginCatalogue.profilesTurtle()
  }

  discoveredPluginsTurtle() {
    this.#requirePluginCatalogue()
    return this.pluginCatalogue.discoveredTurtle()
  }

  async captureProjectMidi({ filePath, durationBeats = 64 }) {
    this.#requireProject()
    this.#requireStopped()
    if (!this.engine) throw new Error('Native engine is required for MIDI capture; start the MCP server with --native-addon')
    const target = this.#allowedPath(filePath)
    const events = this.engine.captureMidi(durationBeats)
    const nodeLabels = Object.fromEntries(
      [...this.project.graph.nodes.values()].map(n => [n.id, n.label || n.id.split('/').pop()])
    )
    const { writeSmfFromEvents } = await import('../midi/SmfWriter.js')
    return writeSmfFromEvents(target, events, nodeLabels, this.project.transport.toJSON())
  }

  async renderMidi(filePath) {
    this.#requireProject()
    const target = this.#allowedPath(filePath)
    const { writeSmf } = await import('../midi/SmfWriter.js')
    return writeSmf(target, this.project.arrangement.toJSON(), this.project.transport.toJSON())
  }

  async renderAudio({ filePath, totalBeats, tempo, sampleRate = 48000, blockSize = 1024 }) {
    this.#requireProject()
    this.#requireStopped()
    if (!this.engine) throw new Error('Native engine is required for audio render; start the MCP server with --native-addon')
    if (typeof filePath !== 'string' || !filePath.toLowerCase().endsWith('.wav')) {
      throw new Error('renderAudio filePath must end in .wav (the offline renderer writes a WAV container)')
    }
    const beats = totalBeats ?? this.project.arrangement.toJSON().lengthBeats
    if (!Number.isFinite(beats) || beats <= 0) {
      throw new RangeError('totalBeats must be positive (the arrangement length is 0; pass totalBeats explicitly)')
    }
    // The NAPI call blocks the event loop for the whole render.
    if (beats > 256) throw new RangeError('totalBeats must not exceed 256 (render longer pieces in sections)')
    const target = this.#allowedPath(filePath)
    const result = this.engine.renderAudio({ outputPath: target, totalBeats: beats, tempo, sampleRate, blockSize })
    return { filePath: target, ...result }
  }

  dispose() {
    this.engine?.dispose()
  }

  #checkRevision(expectedRevision) {
    if (expectedRevision !== this.project.revision) {
      throw new ProjectRevisionError(expectedRevision, this.project.revision)
    }
  }

  #requireProject() {
    if (!this.project.graph) throw new Error('No project is open')
  }

  #requireStopped() {
    if (this.engine?.state === 'running') throw new Error('Stop audio before changing project structure or transport configuration')
  }

  #requirePluginCatalogue() {
    if (!this.pluginCatalogue) throw new Error('Plugin catalogue is unavailable')
  }

  #allowedPath(filePath) {
    if (typeof filePath !== 'string' || !filePath) throw new TypeError('A project file path is required')
    const resolvedPath = isAbsolute(filePath) ? resolve(filePath) : resolve(this.allowedRoots[0], filePath)
    if (!this.#isAllowed(resolvedPath)) throw new Error(`Project path is outside the allowed roots: ${resolvedPath}`)
    return resolvedPath
  }

  #isAllowed(filePath) {
    return this.allowedRoots.some(root => {
      const pathFromRoot = relative(root, resolve(filePath))
      return pathFromRoot === '' || (!pathFromRoot.startsWith('..') && !isAbsolute(pathFromRoot))
    })
  }
}

function compactPlugin(plugin) {
  const { parameters = [], ...summary } = plugin
  return { ...summary, parameterCount: parameters.length }
}

export function applyGraphOperations(definition, operations) {
  let next = {
    ...definition,
    nodes: definition.nodes.map(node => ({ ...node })),
    connections: definition.connections.map(connection => ({ ...connection })),
    metadata: { ...(definition.metadata ?? {}) }
  }
  for (const operation of operations) {
    switch (operation.type) {
      case 'addNode':
        if (next.nodes.some(node => node.id === operation.node?.id)) throw new Error(`Node already exists: ${operation.node?.id}`)
        next.nodes.push(operation.node)
        break
      case 'updateNode': {
        const index = next.nodes.findIndex(node => node.id === operation.nodeId)
        if (index < 0) throw new Error(`Node does not exist: ${operation.nodeId}`)
        const changes = operation.changes ?? {}
        if (changes.id !== undefined && changes.id !== operation.nodeId) throw new Error('Node IDs cannot be changed')
        next.nodes[index] = {
          ...next.nodes[index],
          ...changes,
          ports: changes.ports ? { ...next.nodes[index].ports, ...changes.ports } : next.nodes[index].ports,
          settings: changes.settings ? { ...next.nodes[index].settings, ...changes.settings } : next.nodes[index].settings,
          metadata: changes.metadata ? { ...next.nodes[index].metadata, ...changes.metadata } : next.nodes[index].metadata
        }
        break
      }
      case 'removeNode':
        if (!next.nodes.some(node => node.id === operation.nodeId)) throw new Error(`Node does not exist: ${operation.nodeId}`)
        next.nodes = next.nodes.filter(node => node.id !== operation.nodeId)
        next.connections = next.connections.filter(connection => connection.from !== operation.nodeId && connection.to !== operation.nodeId)
        if (Array.isArray(next.metadata.midiMappings)) {
          next.metadata.midiMappings = next.metadata.midiMappings
            .filter(mapping => mapping?.targetNodeId !== operation.nodeId)
        }
        break
      case 'addConnection':
        next.connections.push(operation.connection)
        break
      case 'removeConnection': {
        const index = next.connections.findIndex(connection => connectionMatches(connection, operation.connection))
        if (index < 0) throw new Error('Connection does not exist')
        next.connections.splice(index, 1)
        break
      }
      case 'setProjectMetadata': {
        const merged = { ...next.metadata, ...(operation.metadata ?? {}) }
        if (merged.midiMappings !== undefined) {
          if (!Array.isArray(merged.midiMappings)) throw new TypeError('midiMappings must be an array')
          merged.midiMappings = merged.midiMappings.map(mapping => validateMidiMapping(mapping, next.nodes))
        }
        next.metadata = merged
        break
      }
      case 'addMidiMapping': {
        const mapping = validateMidiMapping(operation.mapping, next.nodes)
        const mappings = Array.isArray(next.metadata.midiMappings) ? [...next.metadata.midiMappings] : []
        if (mappings.some(existing => midiMappingEquals(existing, mapping))) {
          throw new Error(`MIDI mapping already exists: channel ${mapping.channel} controller ${mapping.controller} -> ${mapping.targetNodeId} parameter ${mapping.parameterId}`)
        }
        mappings.push(mapping)
        next.metadata = { ...next.metadata, midiMappings: mappings }
        break
      }
      case 'removeMidiMapping': {
        const mapping = validateMidiMapping(operation.mapping, next.nodes, { requireTargetNode: false })
        const mappings = Array.isArray(next.metadata.midiMappings) ? [...next.metadata.midiMappings] : []
        const index = mappings.findIndex(existing => midiMappingEquals(existing, mapping))
        if (index < 0) throw new Error('MIDI mapping does not exist')
        mappings.splice(index, 1)
        next.metadata = { ...next.metadata, midiMappings: mappings }
        break
      }
      default:
        throw new Error(`Unsupported graph operation: ${operation.type}`)
    }
  }
  return next
}

function midiMappingEquals(left, right) {
  return left?.targetNodeId === right.targetNodeId &&
    left?.parameterId === right.parameterId &&
    (left?.channel ?? -1) === right.channel &&
    left?.controller === right.controller &&
    (left?.consume ?? true) === right.consume
}

// Bounds mirror the native interchange decoder (scripts/native-ui-project.js):
// channel -1 means any channel, otherwise 0–15; controller is a MIDI CC 0–127.
function validateMidiMapping(mapping, nodes, { requireTargetNode = true } = {}) {
  if (!mapping || typeof mapping !== 'object') throw new TypeError('MIDI mapping must be an object')
  const { targetNodeId, parameterId, channel = -1, controller, consume = true } = mapping
  if (!targetNodeId || typeof targetNodeId !== 'string') throw new TypeError('MIDI mapping targetNodeId is required')
  if (requireTargetNode && !nodes.some(node => node.id === targetNodeId)) {
    throw new Error(`MIDI mapping targets unknown node: ${targetNodeId}`)
  }
  if (!Number.isInteger(parameterId) || parameterId < 0) {
    throw new RangeError(`MIDI mapping parameterId must be a non-negative integer: ${parameterId}`)
  }
  if (!Number.isInteger(channel) || channel < -1 || channel > 15) {
    throw new RangeError(`MIDI mapping channel must be -1 (any) or 0–15: ${channel}`)
  }
  if (!Number.isInteger(controller) || controller < 0 || controller > 127) {
    throw new RangeError(`MIDI mapping controller must be 0–127: ${controller}`)
  }
  if (typeof consume !== 'boolean') throw new TypeError('MIDI mapping consume must be a boolean')
  return { targetNodeId, parameterId, channel, controller, consume }
}

/**
 * Pair captured note on/off events into an arrangement MIDI clip.
 * Same-timestamp on/off pairs are dropped (clips require positive
 * durations); overlapping re-ons keep the first; stray offs are dropped;
 * notes still open at the clip end extend to it; non-note events are
 * ignored. Events are expected from engine.captureMidi().
 */
export function freezeClipFromEvents(events, { sourceNodeId, targetNodeId, clipId, startBeat = 0, lengthBeats }) {
  if (!targetNodeId || typeof targetNodeId !== 'string') throw new TypeError('freezeGenerator targetNodeId is required')
  const isOn = status => status === 0x90
  const sorted = [...(events ?? [])]
    .filter(event => event?.nodeId === sourceNodeId)
    .map(event => ({
      beat: Number(event.beatPosition),
      kind: Number(event.status) & 0xf0,
      channel: Number(event.status) & 0x0f,
      pitch: Number(event.data1),
      velocity: Number(event.data2)
    }))
    .filter(event => Number.isFinite(event.beat) && event.beat >= startBeat)
    .sort((a, b) => a.beat - b.beat || (isOn(a.kind) && a.velocity > 0 ? 0 : 1) - (isOn(b.kind) && b.velocity > 0 ? 0 : 1))
  const notes = []
  const open = new Map()
  for (const event of sorted) {
    const key = `${event.channel}:${event.pitch}`
    const rel = event.beat - startBeat
    if (event.kind === 0x90 && event.velocity > 0) {
      if (!open.has(key)) open.set(key, { beat: rel, velocity: event.velocity })
    } else if (event.kind === 0x80 || event.kind === 0x90) {
      const on = open.get(key)
      if (!on) continue
      open.delete(key)
      const end = Math.min(event.beat - startBeat, lengthBeats)
      if (end > on.beat) {
        notes.push({ startBeat: on.beat, durationBeats: end - on.beat, pitch: event.pitch, velocity: on.velocity, channel: event.channel })
      }
    }
  }
  for (const [key, on] of open) {
    const [channel, pitch] = key.split(':').map(Number)
    if (lengthBeats > on.beat) {
      notes.push({ startBeat: on.beat, durationBeats: lengthBeats - on.beat, pitch, velocity: on.velocity, channel })
    }
  }
  notes.sort((a, b) => a.startBeat - b.startBeat || a.pitch - b.pitch)
  return { id: clipId, targetNodeId, startBeat, lengthBeats, notes }
}

function connectionMatches(left, right) {
  return left.from === right?.from &&
    left.to === right?.to &&
    left.kind === right?.kind &&
    (left.fromPort ?? 0) === (right?.fromPort ?? 0) &&
    (left.toPort ?? 0) === (right?.toPort ?? 0)
}
