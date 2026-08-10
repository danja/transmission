// src/http/TransmissionHttpClient.js
// HTTP client that mirrors TransmissionControlService's method signatures.
// Used by the MCP server when --live connects it to a running transmission-live server.

import { request as httpRequest } from 'node:http'
import { parseTurtle, graphFromDataset, transportFromDataset } from '../rdf/TransmissionRdf.js'

const TRN = 'http://purl.org/stuff/transmissions/'

export class TransmissionHttpClient {
  constructor(endpoint = 'http://localhost:7878') {
    this._endpoint = endpoint.replace(/\/$/, '')
    // pluginCatalogue presence flag — the server exposes plugins if it has one
    this.pluginCatalogue = true
  }

  // ── Reads ─────────────────────────────────────────────────────────────────

  async status() {
    return this._get('/status', 'json')
  }

  async describeProject() {
    return this._get('/graph', 'json')
  }

  projectTurtle() {
    return this._get('/graph', 'turtle')
  }

  async diagnostics() {
    return this._get('/diagnostics', 'json')
  }

  async plugins({ installedOnly = false } = {}) {
    return this._get(`/plugins${installedOnly ? '?installedOnly=true' : ''}`, 'json')
  }

  async waitForPluginScan() {
    // Server handles scan timing; a GET /plugins will block until ready
  }

  pluginProfilesTurtle() {
    return this._get('/plugins/profiles', 'turtle')
  }

  discoveredPluginsTurtle() {
    return this._get('/plugins/discovered', 'turtle')
  }

  async searchPlugins(query) {
    return this._get(`/plugins?search=${encodeURIComponent(JSON.stringify(query))}`, 'json')
  }

  async describePlugin(identifier) {
    return this._get(`/plugins/${encodeURIComponent(identifier)}`, 'json')
  }

  async validatePluginChain(identifiers) {
    // No dedicated endpoint yet — exercise plugins/chain through the server
    return this._post('/plugins/validate-chain', JSON.stringify({ identifiers }), 'application/json')
  }

  // ── Mutations ─────────────────────────────────────────────────────────────

  async newProject(definition) {
    const turtle = projectDefinitionToTurtle(definition)
    return this._post('/projects/new', turtle, 'text/turtle')
  }

  async openProject(filePath) {
    const turtle = actionTurtle('OpenProject', { filePath })
    return this._post('/projects/open', turtle, 'text/turtle')
  }

  async saveProject(filePath = null) {
    const turtle = actionTurtle('SaveProject', filePath ? { filePath } : {})
    return this._post('/projects/save', turtle, 'text/turtle')
  }

  async applyGraphChanges({ expectedRevision, operations, dryRun = false }) {
    const turtle = changeSetTurtle(expectedRevision, operations, dryRun)
    return this._post('/graph/changes', turtle, 'text/turtle')
  }

  async configureTransport({ expectedRevision, tempo, atBeat, loop, clearLoop = false, positionBeats }) {
    const props = { expectedRevision }
    if (tempo !== undefined) props.tempo = tempo
    if (atBeat !== undefined) props.atBeat = atBeat
    if (positionBeats !== undefined) props.positionBeats = positionBeats
    if (clearLoop) props.clearLoop = true
    if (loop) {
      props.loopStartBeat = loop.startBeat
      props.loopEndBeat = loop.endBeat
      if (loop.enabled !== undefined) props.loopEnabled = loop.enabled
    }
    const turtle = actionTurtle('ConfigureTransport', props)
    const body = await this._postRaw('/transport/configure', turtle, 'text/turtle')
    return parseTurtleStatus(body)
  }

  async startTransport() {
    const body = await this._postRaw('/transport/play', '', 'text/turtle')
    return parseTurtleStatus(body)
  }

  async stopTransport() {
    const body = await this._postRaw('/transport/stop', '', 'text/turtle')
    return parseTurtleStatus(body)
  }

  async setParameter({ expectedRevision, nodeId, parameterId, value, sampleOffset = 0 }) {
    const turtle = actionTurtle('SetParameter', { expectedRevision, normalizedValue: value, sampleOffset })
    return this._post(`/parameters/${encodeURIComponent(nodeId)}/${parameterId}`, turtle, 'text/turtle')
  }

  async scanPlugins() {
    return this._post('/plugins/scan', '', 'text/turtle')
  }

  dispose() {}

  // ── Transport helpers ─────────────────────────────────────────────────────

  async _get(path, format) {
    const accept = format === 'turtle' ? 'text/turtle' : 'application/json'
    const body = await fetch_(this._endpoint + path, { method: 'GET', headers: { Accept: accept } })
    if (format === 'turtle') return body
    return JSON.parse(body)
  }

  async _post(path, body, contentType) {
    const response = await this._postRaw(path, body, contentType)
    try { return JSON.parse(response) } catch { return response }
  }

  _postRaw(path, body, contentType) {
    return fetch_(this._endpoint + path, {
      method: 'POST',
      headers: { 'Content-Type': contentType, Accept: 'application/json, text/turtle' },
      body
    })
  }
}

// ── Turtle serializers for outgoing requests ──────────────────────────────────

function actionTurtle(typeName, props) {
  const lines = [`@prefix trn: <${TRN}> .`, '@prefix xsd: <http://www.w3.org/2001/XMLSchema#> .', '']
  lines.push(`[] a trn:${typeName} ;`)
  const entries = Object.entries(props)
  entries.forEach(([key, value], index) => {
    const sep = index < entries.length - 1 ? ' ;' : ' .'
    if (typeof value === 'boolean') lines.push(`    trn:${key} ${value}${sep}`)
    else if (typeof value === 'number') lines.push(`    trn:${key} ${value}${sep}`)
    else lines.push(`    trn:${key} ${turtleLiteral(String(value))}${sep}`)
  })
  if (!entries.length) lines[lines.length - 1] = lines[lines.length - 1].replace(/ ;$/, ' .')
  return lines.join('\n') + '\n'
}

function changeSetTurtle(expectedRevision, operations, dryRun) {
  const opTerms = operations.map(op => encodeOperation(op))
  const opList = opTerms.length ? `(\n    ${opTerms.join('\n    ')}\n  )` : '()'
  return [
    `@prefix trn: <${TRN}> .`,
    '@prefix xsd: <http://www.w3.org/2001/XMLSchema#> .',
    '',
    `[] a trn:ChangeSet ;`,
    `    trn:expectedRevision ${expectedRevision} ;`,
    `    trn:dryRun ${dryRun} ;`,
    `    trn:operations ${opList} .`
  ].join('\n') + '\n'
}

function encodeOperation(op) {
  if (op.type === 'addNode') {
    return `[ a trn:AddNode ; trn:nodeJson ${turtleLiteral(JSON.stringify(op.node))} ]`
  }
  if (op.type === 'updateNode') {
    return `[ a trn:UpdateNode ; trn:nodeId ${turtleLiteral(op.nodeId)} ; trn:changesJson ${turtleLiteral(JSON.stringify(op.changes ?? {}))} ]`
  }
  if (op.type === 'removeNode') {
    return `[ a trn:RemoveNode ; trn:nodeId ${turtleLiteral(op.nodeId)} ]`
  }
  if (op.type === 'addConnection') {
    return `[ a trn:AddConnection ; trn:connectionJson ${turtleLiteral(JSON.stringify(op.connection))} ]`
  }
  if (op.type === 'removeConnection') {
    return `[ a trn:RemoveConnection ; trn:connectionJson ${turtleLiteral(JSON.stringify(op.connection))} ]`
  }
  if (op.type === 'setProjectMetadata') {
    return `[ a trn:SetProjectMetadata ; trn:metadataJson ${turtleLiteral(JSON.stringify(op.metadata ?? {}))} ]`
  }
  throw new Error(`Unsupported operation type: ${op.type}`)
}

function projectDefinitionToTurtle(definition) {
  // For new project, reuse the existing serializer via dynamic import would require async.
  // Instead, send the definition as trn:NewProject with a JSON payload that the server can parse.
  // The server's parseNewProject handles trn:Transmission subjects, so embed one here.
  const id = definition.id ?? `${TRN}main`
  const nodeIds = (definition.nodes ?? []).map(n => `<${n.id}>`).join(' ')
  const lines = [
    `@prefix : <${TRN}> .`,
    '@prefix rdf: <http://www.w3.org/1999/02/22-rdf-syntax-ns#> .',
    '',
    `<${id}> a :Transmission ;`,
    `    :pipe ( ${nodeIds} ) .`
  ]
  // Minimal encoding — for full fidelity, callers should send a serialized Turtle file.
  // The MCP layer always has the full project available as Turtle via control.projectTurtle().
  return lines.join('\n') + '\n'
}

async function parseTurtleStatus(body) {
  // Parse Turtle status response back to a plain status object
  try {
    const dataset = await parseTurtle(body)
    const STATUS = 'urn:transmission:status'
    const status = {}
    for (const quad of dataset) {
      if (quad.subject.value !== STATUS) continue
      const prop = quad.predicate.value.replace(TRN, '')
      const val = quad.object.value
      if (prop === 'revision') status.revision = Number(val)
      else if (prop === 'dirty') status.dirty = val === 'true'
      else if (prop === 'projectOpen') status.projectOpen = val === 'true'
      else if (prop === 'engineAvailable') status.engineAvailable = val === 'true'
      else if (prop === 'engineState') status.engineState = val
      else if (prop === 'projectId') status.projectId = val
      else if (prop === 'filePath') status.filePath = val
      else if (prop === 'transportRunning') status.transport = { ...(status.transport ?? {}), running: val === 'true' }
      else if (prop === 'bpm') status.transport = { ...(status.transport ?? {}), tempoMap: [{ beat: 0, bpm: Number(val) }] }
      else if (prop === 'positionBeats') status.transport = { ...(status.transport ?? {}), positionBeats: Number(val) }
    }
    return status
  } catch {
    return {}
  }
}

function turtleLiteral(value) {
  const escaped = value.replace(/\\/g, '\\\\').replace(/"/g, '\\"').replace(/\n/g, '\\n')
  return `"${escaped}"`
}

// Minimal HTTP fetch using node:http (no node-fetch dependency needed)
function fetch_(url, { method, headers = {}, body = '' } = {}) {
  return new Promise((resolve, reject) => {
    const parsed = new URL(url)
    const bodyBuffer = Buffer.from(body, 'utf8')
    const options = {
      hostname: parsed.hostname,
      port: parsed.port || 80,
      path: parsed.pathname + parsed.search,
      method,
      headers: { ...headers, 'Content-Length': bodyBuffer.length }
    }
    const req = httpRequest(options, res => {
      const chunks = []
      res.on('data', chunk => chunks.push(chunk))
      res.on('end', () => {
        const text = Buffer.concat(chunks).toString('utf8')
        if (res.statusCode >= 400) reject(new Error(`HTTP ${res.statusCode}: ${text}`))
        else resolve(text)
      })
    })
    req.on('error', reject)
    if (bodyBuffer.length) req.write(bodyBuffer)
    req.end()
  })
}
