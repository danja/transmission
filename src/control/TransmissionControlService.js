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
  constructor({
    project = new ProjectSession(),
    engine = null,
    allowedRoots = [process.cwd()],
    pluginCatalogue = null,
    pluginRoots = []
  } = {}) {
    this.project = engine?.project ?? project
    this.engine = engine
    this.pluginCatalogue = pluginCatalogue
    this.pluginRoots = [...pluginRoots]
    this.allowedRoots = allowedRoots.map(root => resolve(root))
    if (!this.allowedRoots.length) throw new TypeError('At least one allowed project root is required')
  }

  status() {
    return {
      projectOpen: Boolean(this.project.graph),
      projectId: this.project.graph?.id ?? null,
      filePath: this.project.filePath,
      revision: this.project.revision,
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
      executionOrder: [...this.project.compiledGraph.executionOrder]
    }
  }

  projectTurtle() {
    this.#requireProject()
    return serializeGraph(this.project.graph, this.project.transport.toJSON())
  }

  newProject(definition) {
    const normalized = {
      id: definition.id,
      label: definition.label ?? '',
      nodes: definition.nodes ?? [],
      connections: definition.connections ?? [],
      metadata: definition.metadata ?? {},
      transport: definition.transport
    }
    if (this.engine) this.engine.open(normalized)
    else this.project.open(normalized)
    return this.describeProject()
  }

  async openProject(filePath) {
    this.#requireStopped()
    const resolvedPath = this.#allowedPath(filePath)
    const loaded = await ProjectSession.load(resolvedPath, { compiler: this.project.compiler })
    const definition = {
      ...loaded.graph.toJSON(),
      transport: loaded.transport.toJSON()
    }
    if (this.engine) this.engine.open(definition, resolvedPath)
    else this.project.open(definition, resolvedPath)
    return this.describeProject()
  }

  async saveProject(filePath = null) {
    this.#requireProject()
    const target = filePath ? this.#allowedPath(filePath) : this.project.filePath
    if (!target) throw new Error('A project file path is required')
    if (!this.#isAllowed(target)) throw new Error(`Project path is outside the allowed roots: ${target}`)
    await this.project.save(target)
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
    return { revision: this.project.revision, transport: transport.toJSON() }
  }

  startTransport() {
    this.#requireProject()
    if (!this.engine) throw new Error('Native audio is unavailable; start the MCP server with --native-addon')
    this.engine.start()
    return this.status()
  }

  stopTransport() {
    if (this.engine) this.engine.stop()
    else this.project.transport.stop()
    return this.status()
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

  diagnostics() {
    let native = null
    if (this.engine) native = this.engine.diagnostics()
    return { ...this.status(), native }
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
