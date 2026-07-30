// src/session/ProjectSession.js

import { readFile, rename, writeFile } from 'node:fs/promises'
import { dirname, join } from 'node:path'
import { mkdir } from 'node:fs/promises'
import { Graph } from '../model/Graph.js'
import { compileGraph } from '../compiler/GraphCompiler.js'
import { graphFromDataset, parseTurtle, serializeGraph, transportFromDataset } from '../rdf/TransmissionRdf.js'
import { Transport } from '../transport/Transport.js'

export class ProjectSession {
  constructor({ compiler = compileGraph } = {}) {
    this.compiler = compiler
    this.graph = null
    this.compiledGraph = null
    this.filePath = null
    this.revision = 0
    this.savedRevision = null
    this.history = []
    this.future = []
    this.transport = new Transport()
  }

  open(definition, filePath = null) {
    this.graph = definition instanceof Graph ? definition : new Graph(definition)
    this.transport = new Transport()
    if (!(definition instanceof Graph) && definition.transport) this.transport.load(definition.transport)
    this.compiledGraph = this.compiler(this.graph)
    this.filePath = filePath
    this.revision = 0
    this.savedRevision = filePath ? 0 : null
    this.history = []
    this.future = []
    return this.compiledGraph
  }

  update(mutator) {
    if (!this.graph) throw new Error('No project is open')
    const nextDefinition = mutator(this.graph.toJSON())
    const next = nextDefinition instanceof Graph ? nextDefinition : new Graph(nextDefinition)
    const compiled = this.compiler(next)
    this.history.push(this.graph)
    this.future = []
    this.graph = next
    this.compiledGraph = compiled
    this.revision += 1
    return compiled
  }

  undo() {
    if (!this.history.length) return false
    this.future.push(this.graph)
    this.graph = this.history.pop()
    this.compiledGraph = this.compiler(this.graph)
    this.revision += 1
    return true
  }

  redo() {
    if (!this.future.length) return false
    this.history.push(this.graph)
    this.graph = this.future.pop()
    this.compiledGraph = this.compiler(this.graph)
    this.revision += 1
    return true
  }

  markChanged() {
    if (!this.graph) throw new Error('No project is open')
    this.revision += 1
    return this.revision
  }

  async save(filePath = this.filePath) {
    if (!this.graph) throw new Error('No project is open')
    if (!filePath) throw new Error('A project file path is required')
    await mkdir(dirname(filePath), { recursive: true })
    const temporaryPath = join(dirname(filePath), `.${filePath.split('/').pop()}.${process.pid}.tmp`)
    const definition = { ...this.graph.toJSON(), transport: this.transport.toJSON() }
    const content = filePath.endsWith('.ttl') ? serializeGraph(this.graph, this.transport.toJSON()) : `${JSON.stringify(definition, null, 2)}\n`
    await writeFile(temporaryPath, content, 'utf8')
    await rename(temporaryPath, filePath)
    this.filePath = filePath
    this.savedRevision = this.revision
    return filePath
  }

  static async load(filePath, options) {
    const session = new ProjectSession(options)
    if (filePath.endsWith('.ttl')) {
      const dataset = await parseTurtle(await readFile(filePath, 'utf8'))
      const transmission = [...dataset.match(null)].find(quad => quad.predicate.value === 'http://www.w3.org/1999/02/22-rdf-syntax-ns#type' && quad.object.value === 'http://purl.org/stuff/transmissions/Transmission')
      if (!transmission) throw new Error(`No transmission found in ${filePath}`)
      session.open({
        ...graphFromDataset(dataset, transmission.subject.value).toJSON(),
        transport: transportFromDataset(dataset, transmission.subject.value)
      }, filePath)
    } else {
      session.open(JSON.parse(await readFile(filePath, 'utf8')), filePath)
    }
    return session
  }
}
