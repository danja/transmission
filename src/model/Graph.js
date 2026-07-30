// src/model/Graph.js

/**
 * Immutable-friendly in-memory representation of a transmission runtime graph.
 * RDF adapters may construct this model, but the audio engine never consumes RDF.
 */
export class Graph {
  constructor({ id, label = '', nodes = [], connections = [], metadata = {} } = {}) {
    if (!id || typeof id !== 'string') throw new TypeError('Graph id must be a non-empty string')
    this.id = id
    this.label = label
    this.nodes = new Map(nodes.map(node => [node.id, freezeNode(node)]))
    this.connections = connections.map(connection => Object.freeze({ ...connection }))
    this.metadata = Object.freeze({ ...metadata })
    Object.freeze(this.connections)
    Object.freeze(this)
  }

  node(id) {
    return this.nodes.get(id)
  }

  toJSON() {
    return {
      id: this.id,
      label: this.label,
      nodes: [...this.nodes.values()],
      connections: [...this.connections],
      metadata: this.metadata
    }
  }
}

function freezeNode(node) {
  if (!node?.id || typeof node.id !== 'string') throw new TypeError('Graph node id is required')
  if (!node.type || typeof node.type !== 'string') throw new TypeError(`Graph node ${node.id} type is required`)
  return Object.freeze({
    id: node.id,
    type: node.type,
    label: node.label ?? '',
    ports: Object.freeze({
      audioInputs: Number(node.ports?.audioInputs ?? 0),
      audioOutputs: Number(node.ports?.audioOutputs ?? 0),
      midiInputs: Number(node.ports?.midiInputs ?? 0),
      midiOutputs: Number(node.ports?.midiOutputs ?? 0)
    }),
    settings: Object.freeze({ ...(node.settings ?? {}) }),
    parameters: Object.freeze((node.parameters ?? []).map(parameter =>
      Object.freeze({
        id: Number(parameter.id),
        normalizedValue: Number(parameter.normalizedValue)
      }))),
    state: Object.freeze({
      component: String(node.state?.component ?? ''),
      controller: String(node.state?.controller ?? '')
    }),
    metadata: Object.freeze({ ...(node.metadata ?? {}) })
  })
}

export function createGraph(input) {
  return new Graph(input)
}
