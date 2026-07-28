// src/rdf/TransmissionRdf.js

import { Readable } from 'node:stream'
import rdf from 'rdf-ext'
import Parser from '@rdfjs/parser-n3'
import { Graph } from '../model/Graph.js'
import { vocabulary as ns } from './Vocabulary.js'

export async function parseTurtle(text) {
  const parser = new Parser({ format: 'text/turtle' })
  const dataset = rdf.dataset()
  const stream = parser.import(Readable.from([text]))
  for await (const quad of stream) dataset.add(quad)
  return dataset
}

export function graphFromDataset(dataset, transmissionId) {
  const subject = rdf.namedNode(transmissionId)
  const typeValues = terms(dataset, subject, ns.rdf.type)
  if (!typeValues.some(term => [ns.trn.transmission, ns.trn.entryTransmission].includes(term.value))) {
    throw new Error(`Not a transmission: ${transmissionId}`)
  }

  const nodes = list(dataset, first(dataset, subject, ns.trn.pipe))
    .map(node => ({
      id: node.value,
      type: first(dataset, node, ns.rdf.type)?.value,
      label: first(dataset, node, ns.rdfs.label)?.value ?? '',
      settings: settingsObject(dataset, first(dataset, node, ns.trn.settings))
    }))

  return new Graph({
    id: transmissionId,
    label: first(dataset, subject, ns.rdfs.label)?.value ?? '',
    nodes,
    connections: nodes.slice(0, -1).map((node, index) => ({
      from: node.id,
      to: nodes[index + 1].id,
      kind: 'audio'
    }))
  })
}

function terms(dataset, subject, predicate) {
  return [...dataset.match(subject, rdf.namedNode(predicate))].map(quad => quad.object)
}

function first(dataset, subject, predicate) {
  return subject ? terms(dataset, subject, predicate)[0] : undefined
}

function list(dataset, head) {
  const result = []
  const seen = new Set()
  let current = head
  while (current && current.value !== ns.rdf.nil) {
    if (seen.has(current.value)) throw new Error(`Circular RDF list at ${current.value}`)
    seen.add(current.value)
    const item = first(dataset, current, ns.rdf.first)
    if (!item) throw new Error(`Malformed RDF list at ${current.value}`)
    result.push(item)
    current = first(dataset, current, ns.rdf.rest)
  }
  return result
}

function settingsObject(dataset, settingsNode) {
  if (!settingsNode) return {}
  const values = {}
  for (const quad of dataset.match(settingsNode)) {
    if (quad.predicate.value === ns.rdf.type) continue
    const current = values[quad.predicate.value] ?? []
    current.push(quad.object.value)
    values[quad.predicate.value] = current
  }
  return values
}
