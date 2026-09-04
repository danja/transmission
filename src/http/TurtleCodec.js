// src/http/TurtleCodec.js
// Parse RDF Turtle request bodies → action objects; serialize results → Turtle.

import { parseTurtle, graphFromDataset, transportFromDataset, arrangementFromDataset } from '../rdf/TransmissionRdf.js'

const TRN = 'http://purl.org/stuff/transmissions/'
const RDF_TYPE = 'http://www.w3.org/1999/02/22-rdf-syntax-ns#type'
const RDF_FIRST = 'http://www.w3.org/1999/02/22-rdf-syntax-ns#first'
const RDF_REST = 'http://www.w3.org/1999/02/22-rdf-syntax-ns#rest'
const RDF_NIL = 'http://www.w3.org/1999/02/22-rdf-syntax-ns#nil'

// ── Dataset helpers ───────────────────────────────────────────────────────────

function findSubjectOfType(dataset, typeIri) {
  for (const quad of dataset) {
    if (quad.predicate.value === RDF_TYPE && quad.object.value === typeIri) {
      return quad.subject
    }
  }
  return null
}

function getOne(dataset, subject, predicateIri) {
  for (const quad of dataset) {
    if (quad.subject.value === subject.value && quad.predicate.value === predicateIri) {
      return quad.object
    }
  }
  return null
}

function getList(dataset, headNode) {
  const items = []
  let current = headNode
  while (current && current.value !== RDF_NIL) {
    const first = getOne(dataset, current, RDF_FIRST)
    if (first) items.push(first)
    const rest = getOne(dataset, current, RDF_REST)
    current = rest
  }
  return items
}

function numericValue(term) {
  if (!term) return undefined
  const n = Number(term.value)
  return Number.isNaN(n) ? undefined : n
}

function stringValue(term) {
  return term?.value ?? undefined
}

function boolValue(term) {
  if (!term) return undefined
  return term.value === 'true'
}

// ── Request parsers ───────────────────────────────────────────────────────────

export async function parseChangeSet(turtleBody) {
  const dataset = await parseTurtle(turtleBody)
  const subject = findSubjectOfType(dataset, `${TRN}ChangeSet`)
  if (!subject) throw new ParseError('Body must contain a trn:ChangeSet subject')
  const expectedRevision = numericValue(getOne(dataset, subject, `${TRN}expectedRevision`))
  const dryRun = boolValue(getOne(dataset, subject, `${TRN}dryRun`)) ?? false
  const opHead = getOne(dataset, subject, `${TRN}operations`)
  const opNodes = opHead ? getList(dataset, opHead) : []
  const operations = opNodes.map(opNode => decodeOperation(dataset, opNode))
  return { expectedRevision, dryRun, operations }
}

function decodeOperation(dataset, opNode) {
  const typeNode = getOne(dataset, opNode, RDF_TYPE)
  if (!typeNode) throw new ParseError('Each operation must have rdf:type')
  const type = typeNode.value.replace(TRN, '')
  const lcType = type.charAt(0).toLowerCase() + type.slice(1)

  if (lcType === 'addNode') {
    const nodeJson = stringValue(getOne(dataset, opNode, `${TRN}nodeJson`))
    if (!nodeJson) throw new ParseError('trn:AddNode requires trn:nodeJson')
    return { type: 'addNode', node: JSON.parse(nodeJson) }
  }
  if (lcType === 'updateNode') {
    const nodeId = stringValue(getOne(dataset, opNode, `${TRN}nodeId`))
    const changesJson = stringValue(getOne(dataset, opNode, `${TRN}changesJson`))
    if (!nodeId) throw new ParseError('trn:UpdateNode requires trn:nodeId')
    return { type: 'updateNode', nodeId, changes: changesJson ? JSON.parse(changesJson) : {} }
  }
  if (lcType === 'removeNode') {
    const nodeId = stringValue(getOne(dataset, opNode, `${TRN}nodeId`))
    if (!nodeId) throw new ParseError('trn:RemoveNode requires trn:nodeId')
    return { type: 'removeNode', nodeId }
  }
  if (lcType === 'addConnection') {
    const connectionJson = stringValue(getOne(dataset, opNode, `${TRN}connectionJson`))
    if (!connectionJson) throw new ParseError('trn:AddConnection requires trn:connectionJson')
    return { type: 'addConnection', connection: JSON.parse(connectionJson) }
  }
  if (lcType === 'removeConnection') {
    const connectionJson = stringValue(getOne(dataset, opNode, `${TRN}connectionJson`))
    if (!connectionJson) throw new ParseError('trn:RemoveConnection requires trn:connectionJson')
    return { type: 'removeConnection', connection: JSON.parse(connectionJson) }
  }
  if (lcType === 'setProjectMetadata') {
    const metadataJson = stringValue(getOne(dataset, opNode, `${TRN}metadataJson`))
    return { type: 'setProjectMetadata', metadata: metadataJson ? JSON.parse(metadataJson) : {} }
  }
  throw new ParseError(`Unknown operation type: ${type}`)
}

export async function parseTransportConfigure(turtleBody) {
  const dataset = await parseTurtle(turtleBody)
  const subject = findSubjectOfType(dataset, `${TRN}ConfigureTransport`)
  if (!subject) throw new ParseError('Body must contain a trn:ConfigureTransport subject')
  const expectedRevision = numericValue(getOne(dataset, subject, `${TRN}expectedRevision`))
  const tempo = numericValue(getOne(dataset, subject, `${TRN}tempo`))
  const atBeat = numericValue(getOne(dataset, subject, `${TRN}atBeat`))
  const positionBeats = numericValue(getOne(dataset, subject, `${TRN}positionBeats`))
  const clearLoop = boolValue(getOne(dataset, subject, `${TRN}clearLoop`)) ?? false
  const loopStartBeat = numericValue(getOne(dataset, subject, `${TRN}loopStartBeat`))
  const loopEndBeat = numericValue(getOne(dataset, subject, `${TRN}loopEndBeat`))
  const loopEnabled = boolValue(getOne(dataset, subject, `${TRN}loopEnabled`))
  const loop = (loopStartBeat !== undefined && loopEndBeat !== undefined)
    ? { startBeat: loopStartBeat, endBeat: loopEndBeat, enabled: loopEnabled !== false }
    : undefined
  return { expectedRevision, tempo, atBeat, loop, clearLoop, positionBeats }
}

export async function parseSetParameter(turtleBody) {
  const dataset = await parseTurtle(turtleBody)
  const subject = findSubjectOfType(dataset, `${TRN}SetParameter`)
  if (!subject) throw new ParseError('Body must contain a trn:SetParameter subject')
  const expectedRevision = numericValue(getOne(dataset, subject, `${TRN}expectedRevision`))
  const value = numericValue(getOne(dataset, subject, `${TRN}normalizedValue`))
  const sampleOffset = numericValue(getOne(dataset, subject, `${TRN}sampleOffset`)) ?? 0
  return { expectedRevision, value, sampleOffset }
}

export async function parseProjectOpen(turtleBody) {
  const dataset = await parseTurtle(turtleBody)
  const subject = findSubjectOfType(dataset, `${TRN}OpenProject`)
  if (!subject) throw new ParseError('Body must contain a trn:OpenProject subject')
  const filePath = stringValue(getOne(dataset, subject, `${TRN}filePath`))
  if (!filePath) throw new ParseError('trn:OpenProject requires trn:filePath')
  return { filePath }
}

export async function parseProjectSave(turtleBody) {
  const dataset = await parseTurtle(turtleBody)
  const subject = findSubjectOfType(dataset, `${TRN}SaveProject`)
  if (!subject) throw new ParseError('Body must contain a trn:SaveProject subject')
  const filePath = stringValue(getOne(dataset, subject, `${TRN}filePath`))
  return { filePath: filePath ?? null }
}

export async function parseNewProject(turtleBody) {
  const dataset = await parseTurtle(turtleBody)
  // Find the trn:Transmission or trn:NewProject subject
  let transmissionId = null
  for (const quad of dataset) {
    if (quad.predicate.value === RDF_TYPE &&
        (quad.object.value === `${TRN}Transmission` || quad.object.value === `${TRN}EntryTransmission`)) {
      transmissionId = quad.subject.value
      break
    }
  }
  if (!transmissionId) throw new ParseError('Body must contain a trn:Transmission subject')
  const graph = graphFromDataset(dataset, transmissionId)
  const transport = transportFromDataset(dataset, transmissionId)
  const arrangement = arrangementFromDataset(dataset, transmissionId, graph)
  return { ...graph.toJSON(), transport, arrangement: arrangement.toJSON() }
}

export async function parseServerConfig(turtleBody) {
  const dataset = await parseTurtle(turtleBody)
  const subject = findSubjectOfType(dataset, `${TRN}ServerConfig`)
  if (!subject) throw new ParseError('Config must contain a trn:ServerConfig subject')
  const port = numericValue(getOne(dataset, subject, `${TRN}port`)) ?? 7878
  const bindAddress = stringValue(getOne(dataset, subject, `${TRN}bindAddress`)) ?? '127.0.0.1'
  const rootsHead = getOne(dataset, subject, `${TRN}allowedRoots`)
  const allowedRoots = rootsHead ? getList(dataset, rootsHead).map(t => t.value) : ['.']
  const outputsHead = getOne(dataset, subject, `${TRN}defaultOutputConnections`)
  const defaultOutputConnections = outputsHead ? getList(dataset, outputsHead).map(t => t.value) : null
  return { port, bindAddress, allowedRoots, defaultOutputConnections }
}

// ── Response serializers ──────────────────────────────────────────────────────

export function serializeStatus(status) {
  const lines = [
    `@prefix trn: <${TRN}> .`,
    '@prefix xsd: <http://www.w3.org/2001/XMLSchema#> .',
    '',
    '<urn:transmission:status> a trn:Status ;'
  ]
  lines.push(`    trn:generation ${status.generation ?? 0} ;`)
  lines.push(`    trn:revision ${status.revision} ;`)
  lines.push(`    trn:dirty ${status.dirty} ;`)
  lines.push(`    trn:projectOpen ${status.projectOpen} ;`)
  lines.push(`    trn:engineAvailable ${status.engineAvailable} ;`)
  lines.push(`    trn:engineState ${literal(status.engineState)} ;`)
  if (status.projectId) lines.push(`    trn:projectId ${literal(status.projectId)} ;`)
  if (status.filePath) lines.push(`    trn:filePath ${literal(status.filePath)} ;`)
  if (status.transport) {
    const t = status.transport
    lines.push(`    trn:positionBeats ${t.positionBeats ?? 0} ;`)
    if (t.tempoMap?.length) {
      lines.push(`    trn:bpm ${t.tempoMap[0].bpm} ;`)
    }
    lines.push(`    trn:transportRunning ${t.running ?? false} ;`)
  }
  lines[lines.length - 1] = lines[lines.length - 1].replace(/ ;$/, ' .')
  return lines.join('\n') + '\n'
}

export function serializeError(message, code = 'Error') {
  return [
    `@prefix trn: <${TRN}> .`,
    '',
    `<urn:transmission:error> a trn:${code} ;`,
    `    trn:message ${literal(message)} .`
  ].join('\n') + '\n'
}

// ── Helpers ───────────────────────────────────────────────────────────────────

function literal(value) {
  return `"${String(value).replace(/\\/g, '\\\\').replace(/"/g, '\\"').replace(/\n/g, '\\n')}"`
}

export class ParseError extends Error {
  constructor(message) {
    super(message)
    this.name = 'ParseError'
  }
}
