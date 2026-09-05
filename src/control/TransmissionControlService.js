import { isAbsolute, relative, resolve } from 'node:path'
import { compileGraph } from '../compiler/GraphCompiler.js'
import { ProjectSession } from '../session/ProjectSession.js'
import { serializeGraph } from '../rdf/TransmissionRdf.js'

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
      case 'setProjectMetadata':
        next.metadata = { ...next.metadata, ...(operation.metadata ?? {}) }
        break
      default:
        throw new Error(`Unsupported graph operation: ${operation.type}`)
    }
  }
  return next
}

function connectionMatches(left, right) {
  return left.from === right?.from &&
    left.to === right?.to &&
    left.kind === right?.kind &&
    (left.fromPort ?? 0) === (right?.fromPort ?? 0) &&
    (left.toPort ?? 0) === (right?.toPort ?? 0)
}
