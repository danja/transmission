import { readFile } from 'node:fs/promises'
import rdf from 'rdf-ext'
import Serializer from '@rdfjs/serializer-turtle'
import { parseTurtle } from './TransmissionRdf.js'
import { vocabulary as ns } from './Vocabulary.js'

const TRN = 'http://purl.org/stuff/transmissions/'

const terms = Object.freeze({
  ...ns.trn
})

export async function loadPluginProfileFile(filePath) {
  return parsePluginProfiles(await readFile(filePath, 'utf8'), filePath)
}

export async function parsePluginProfiles(text, source = null) {
  const dataset = await parseTurtle(text)
  const profiles = []
  for (const quad of dataset.match(null, rdf.namedNode(ns.rdf.type), rdf.namedNode(terms.pluginProfile))) {
    profiles.push(profileFromDataset(dataset, quad.subject, source))
  }
  return profiles
}

function profileFromDataset(dataset, subject, source) {
  const profile = {
    id: subject.value,
    name: firstValue(dataset, subject, ns.rdfs.label),
    description: firstValue(dataset, subject, ns.rdfs.comment),
    bundleName: firstValue(dataset, subject, terms.bundleName),
    vstClassId: firstValue(dataset, subject, terms.vstClassId),
    vendor: firstValue(dataset, subject, terms.vendor),
    homepage: firstValue(dataset, subject, terms.homepage),
    roles: values(dataset, subject, terms.role).map(compactTerm),
    produces: values(dataset, subject, terms.produces).map(compactTerm),
    accepts: values(dataset, subject, terms.accepts).map(compactTerm),
    requirements: values(dataset, subject, terms.requires).map(compactTerm),
    recommendedBefore: values(dataset, subject, terms.recommendedBefore).map(term => term.value),
    recommendedAfter: values(dataset, subject, terms.recommendedAfter).map(term => term.value),
    companions: values(dataset, subject, terms.companion).map(term => term.value),
    genres: values(dataset, subject, terms.genre).map(term => term.value),
    cautions: values(dataset, subject, terms.caution).map(term => term.value),
    profileSource: source
  }
  if (!profile.name) throw new Error(`Plugin profile is missing rdfs:label: ${profile.id}`)
  if (!profile.bundleName && !profile.vstClassId) {
    throw new Error(`Plugin profile requires trn:bundleName or trn:vstClassId: ${profile.id}`)
  }
  return profile
}

export function serializeDiscoveredPlugins(plugins) {
  const dataset = rdf.dataset()
  for (const plugin of plugins) addDiscoveredPlugin(dataset, plugin)
  return new Serializer({
    prefixes: new Map([
      ['trn', rdf.namedNode(TRN)],
      ['rdfs', rdf.namedNode('http://www.w3.org/2000/01/rdf-schema#')],
      ['xsd', rdf.namedNode('http://www.w3.org/2001/XMLSchema#')]
    ])
  }).transform(dataset)
}

function addDiscoveredPlugin(dataset, plugin) {
  const subject = rdf.namedNode(`urn:vst3:${plugin.classId}`)
  addNamed(dataset, subject, ns.rdf.type, terms.discoveredPlugin)
  addLiteral(dataset, subject, ns.rdfs.label, plugin.name)
  addLiteral(dataset, subject, terms.vstClassId, plugin.classId)
  addLiteral(dataset, subject, terms.bundleName, plugin.bundleName)
  addLiteral(dataset, subject, terms.vendor, plugin.vendor)
  addLiteral(dataset, subject, terms.category, plugin.category)
  addLiteral(dataset, subject, terms.modulePath, plugin.modulePath)
  addLiteral(dataset, subject, terms.discoverySource, plugin.discoverySource)
  addNumber(dataset, subject, terms.audioInputs, plugin.audioInputs)
  addNumber(dataset, subject, terms.audioOutputs, plugin.audioOutputs)
  addNumber(dataset, subject, terms.midiInputs, plugin.midiInputs)
  addNumber(dataset, subject, terms.midiOutputs, plugin.midiOutputs)
  for (const role of plugin.roles ?? []) addNamed(dataset, subject, terms.role, semanticTerm(role))
  for (const signal of plugin.produces ?? []) addNamed(dataset, subject, terms.produces, semanticTerm(signal))
  for (const signal of plugin.accepts ?? []) addNamed(dataset, subject, terms.accepts, semanticTerm(signal))
  for (const parameter of plugin.parameters ?? []) {
    const parameterNode = rdf.namedNode(`urn:vst3:${plugin.classId}:parameter:${parameter.id}`)
    addNamed(dataset, subject, terms.parameter, parameterNode.value)
    addNumber(dataset, parameterNode, terms.parameterId, parameter.id)
    addLiteral(dataset, parameterNode, terms.parameterTitle, parameter.title)
    addLiteral(dataset, parameterNode, terms.shortTitle, parameter.shortTitle)
    addLiteral(dataset, parameterNode, terms.units, parameter.units)
    addNumber(dataset, parameterNode, terms.defaultNormalized, parameter.defaultNormalized)
    addNumber(dataset, parameterNode, terms.stepCount, parameter.stepCount)
    addNumber(dataset, parameterNode, terms.parameterFlags, parameter.flags)
  }
}

function addNamed(dataset, subject, predicate, object) {
  if (object) dataset.add(rdf.quad(subject, rdf.namedNode(predicate), rdf.namedNode(object)))
}

function addLiteral(dataset, subject, predicate, value) {
  if (value !== undefined && value !== null && value !== '') {
    dataset.add(rdf.quad(subject, rdf.namedNode(predicate), rdf.literal(String(value))))
  }
}

function addNumber(dataset, subject, predicate, value) {
  if (Number.isFinite(value)) {
    const datatype = Number.isInteger(value)
      ? 'http://www.w3.org/2001/XMLSchema#integer'
      : 'http://www.w3.org/2001/XMLSchema#double'
    dataset.add(rdf.quad(subject, rdf.namedNode(predicate), rdf.literal(String(value), rdf.namedNode(datatype))))
  }
}

function semanticTerm(value) {
  return value.includes(':') ? value : `${TRN}${value}`
}

function values(dataset, subject, predicate) {
  return [...dataset.match(subject, rdf.namedNode(predicate))].map(quad => quad.object)
}

function firstValue(dataset, subject, predicate) {
  return values(dataset, subject, predicate)[0]?.value ?? ''
}

function compactTerm(term) {
  return term.termType === 'NamedNode' && term.value.startsWith(TRN)
    ? term.value.slice(TRN.length)
    : term.value
}
