// src/compiler/GraphCompiler.js

import { Graph } from '../model/Graph.js'

export class GraphCompilationError extends Error {
  constructor(message, issues = []) {
    super(message)
    this.name = 'GraphCompilationError'
    this.issues = issues
  }
}

/** Compile a project graph into the stable representation consumed by native code. */
export function compileGraph(input) {
  const graph = input instanceof Graph ? input : new Graph(input)
  const issues = []
  const nodeIds = new Set(graph.nodes.keys())

  for (const connection of graph.connections) {
    if (!nodeIds.has(connection.from)) issues.push(`Connection source does not exist: ${connection.from}`)
    if (!nodeIds.has(connection.to)) issues.push(`Connection target does not exist: ${connection.to}`)
    if (connection.from === connection.to) issues.push(`Self-connection is not allowed: ${connection.from}`)
    if (!['audio', 'midi'].includes(connection.kind)) issues.push(`Unsupported connection kind: ${connection.kind}`)
    validatePorts(graph, connection, issues)
  }

  const duplicateConnections = new Set()
  for (const connection of graph.connections) {
    const key = `${connection.kind}:${connection.from}:${connection.fromPort ?? 0}:${connection.to}:${connection.toPort ?? 0}`
    if (duplicateConnections.has(key)) issues.push(`Duplicate connection: ${key}`)
    duplicateConnections.add(key)
  }

  const audioEdges = graph.connections.filter(connection => connection.kind === 'audio')
  const order = topologicalOrder([...nodeIds], audioEdges)
  if (!order) issues.push('Audio graph contains a cycle')
  if (issues.length) throw new GraphCompilationError(`Graph validation failed: ${issues.join('; ')}`, issues)

  return Object.freeze({
    version: 1,
    id: graph.id,
    label: graph.label,
    executionOrder: Object.freeze(order),
    nodes: Object.freeze([...graph.nodes.values()]),
    connections: Object.freeze([...graph.connections]),
    metadata: graph.metadata
  })
}

function validatePorts(graph, connection, issues) {
  const from = graph.node(connection.from)
  const to = graph.node(connection.to)
  if (!from || !to) return
  const portType = connection.kind === 'audio' ? 'audio' : 'midi'
  const fromCount = from.ports[`${portType}Outputs`]
  const toCount = to.ports[`${portType}Inputs`]
  const fromPort = connection.fromPort ?? 0
  const toPort = connection.toPort ?? 0
  if (!Number.isInteger(fromPort) || fromPort < 0 || fromPort >= fromCount) {
    issues.push(`Invalid ${connection.kind} output port ${fromPort} on ${connection.from}`)
  }
  if (!Number.isInteger(toPort) || toPort < 0 || toPort >= toCount) {
    issues.push(`Invalid ${connection.kind} input port ${toPort} on ${connection.to}`)
  }
}

function topologicalOrder(nodeIds, edges) {
  const incoming = new Map(nodeIds.map(id => [id, 0]))
  const outgoing = new Map(nodeIds.map(id => [id, []]))
  for (const edge of edges) {
    if (!incoming.has(edge.from) || !incoming.has(edge.to)) continue
    incoming.set(edge.to, incoming.get(edge.to) + 1)
    outgoing.get(edge.from).push(edge.to)
  }
  const ready = nodeIds.filter(id => incoming.get(id) === 0)
  const result = []
  while (ready.length) {
    const id = ready.shift()
    result.push(id)
    for (const next of outgoing.get(id)) {
      incoming.set(next, incoming.get(next) - 1)
      if (incoming.get(next) === 0) ready.push(next)
    }
  }
  return result.length === nodeIds.length ? result : null
}
