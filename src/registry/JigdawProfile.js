// src/registry/JigdawProfile.js

import { readFile, stat } from 'node:fs/promises'
import { fileURLToPath } from 'node:url'
import rdf from 'rdf-ext'
import { parseTurtle } from '../rdf/TransmissionRdf.js'

const JIG = 'http://purl.org/stuff/jigdaw/'
const TRN = 'http://purl.org/stuff/transmissions/'
const LV2 = 'http://lv2plug.in/ns/lv2core#'
const UNITS = 'http://lv2plug.in/ns/extensions/units#'
const RDF = 'http://www.w3.org/1999/02/22-rdf-syntax-ns#'
const RDFS = 'http://www.w3.org/2000/01/rdf-schema#'

export const jigdawAbi1 = `${JIG}Abi1`
export const jigdawAbi2 = `${JIG}Abi2`

/** A JigDAW profile could be read but does not describe something this host can run. */
export class JigdawProfileError extends Error {
  constructor(message, iri) {
    super(message)
    this.name = 'JigdawProfileError'
    this.iri = iri
  }
}

/**
 * Dereference a JigDAW plugin IRI and report what a transmission project needs
 * to agree with it: the port counts the graph is validated against, and the
 * parameters an agent can address.
 *
 * The native host reads the same profile through jigdaw_core and is
 * authoritative; this exists so that the control plane can answer questions
 * about a plugin without a native build, which is what `graph_apply_changes`
 * needs before it wires anything up.
 *
 * `fetch` is injected so that tests never reach the network.
 */
export async function readJigdawProfile(iri, { fetch: fetchImplementation = fetchResource } = {}) {
  if (!iri || typeof iri !== 'string') throw new TypeError('A JigDAW plugin IRI is required')
  const { text, base } = await fetchImplementation(iri)
  const dataset = await parseTurtle(text)

  const subject = webPluginSubject(dataset, iri)
  const values = (predicate, from = subject) =>
    [...dataset.match(from, named(predicate))].map(quad => quad.object)
  const value = (predicate, from = subject) => values(predicate, from)[0] ?? null
  const number = (predicate, from, fallback = 0) => {
    const term = value(predicate, from)
    const parsed = Number(term?.value)
    return Number.isFinite(parsed) ? parsed : fallback
  }

  const moduleNode = value(`${JIG}module`)
  const abi = moduleNode ? value(`${JIG}abi`, moduleNode)?.value ?? '' : ''
  const accepts = values(`${TRN}accepts`).map(term => term.value)
  const produces = values(`${TRN}produces`).map(term => term.value)
  const requires = values(`${TRN}requires`).map(term => term.value)

  // jig:audioOutputs counts Web Audio ports; jig:outputChannels counts the
  // channels in them, and a transmission port is a channel.
  const audioInputPorts = number(`${JIG}audioInputs`, subject, 0)
  const audioOutputPorts = number(`${JIG}audioOutputs`, subject, 0)

  const profile = {
    iri: subject.value,
    retrievedFrom: base,
    label: value(`${RDFS}label`)?.value ?? '',
    comment: value(`${RDFS}comment`)?.value ?? '',
    vendor: value(`${TRN}vendor`)?.value ?? '',
    abi,
    roles: values(`${TRN}role`).map(term => term.value),
    accepts,
    produces,
    requires,
    ports: {
      audioInputs: audioInputPorts > 0 ? number(`${JIG}inputChannels`, subject, 2) : 0,
      audioOutputs: audioOutputPorts > 0 ? number(`${JIG}outputChannels`, subject, 2) : 0,
      midiInputs: accepts.includes(`${TRN}Midi`) ? 1 : 0,
      midiOutputs: produces.includes(`${TRN}Midi`) ? 1 : 0
    },
    requiresTransport: requires.includes(`${TRN}HostTransport`),
    // Declared processing latency in frames. Surfaced, not compensated:
    // this engine has no delay-compensation framework (VST3 latency is not
    // read either), so per-node compensation would be architecture, not a fix.
    latencyFrames: Math.trunc(number(`${JIG}latencyFrames`, subject, 0)),
    module: moduleNode
      ? {
          location: value(`${JIG}location`, moduleNode)?.value ?? '',
          integrity: value(`${JIG}integrity`, moduleNode)?.value ?? '',
          mediaType: value(`${JIG}mediaType`, moduleNode)?.value ?? ''
        }
      : null,
    parameters: parameters(dataset, values(`${LV2}port`))
  }

  if (!profile.module)
    throw new JigdawProfileError(
      `${profile.label || iri} declares no WebAssembly module, so there is nothing to run`, iri)
  if (abi !== jigdawAbi1 && abi !== jigdawAbi2)
    throw new JigdawProfileError(
      abi
        ? `Unsupported JigDAW ABI: ${abi}. This host implements jig:Abi1 and jig:Abi2.`
        : 'This plugin declares no jig:abi, so its module is private to its JavaScript ' +
          'processor and a host without a JavaScript engine cannot load it', iri)
  return profile
}

/**
 * The graph node a project should carry for this plugin. Port counts come from
 * the profile rather than from the caller, because the native host overwrites
 * whatever a project declares with what it reads, and a project that guessed
 * fails validation with no explanation.
 */
export function jigdawGraphNode(profile, { id, label = '', x = 0, y = 0 } = {}) {
  if (!id) throw new TypeError('A node id is required')
  return {
    id,
    type: `${TRN}JigdawPlugin`,
    label: label || profile.label,
    ports: { ...profile.ports },
    settings: { pluginIri: profile.iri },
    parameters: [],
    metadata: { x, y }
  }
}

function parameters(dataset, portTerms) {
  const described = portTerms.map(port => {
    const first = predicate => [...dataset.match(port, named(predicate))][0]?.object ?? null
    const numeric = (predicate, fallback) => {
      const parsed = Number(first(predicate)?.value)
      return Number.isFinite(parsed) ? parsed : fallback
    }
    const properties = [...dataset.match(port, named(`${LV2}portProperty`))]
      .map(quad => quad.object.value)
    const scalePoints = [...dataset.match(port, named(`${LV2}scalePoint`))]
      .map(quad => ({
        label: [...dataset.match(quad.object, named(`${RDFS}label`))][0]?.object.value ?? '',
        value: Number([...dataset.match(quad.object, named(`${RDF}value`))][0]?.object.value ?? 0)
      }))
      .sort((left, right) => left.value - right.value)
    return {
      // jig:paramIndex, which is what jig_set_param takes and therefore what
      // this engine's parameterId means for a JigDAW node. It is declared
      // rather than inferred from document order on purpose: re-serialising a
      // profile would otherwise silently rebind every control.
      id: numeric(`${JIG}paramIndex`, -1),
      symbol: first(`${LV2}symbol`)?.value ?? '',
      name: first(`${LV2}name`)?.value ?? '',
      unit: first(`${UNITS}unit`)?.value ?? '',
      minimum: numeric(`${LV2}minimum`, 0),
      maximum: numeric(`${LV2}maximum`, 1),
      defaultValue: numeric(`${LV2}default`, 0),
      toggled: properties.includes(`${LV2}toggled`),
      enumeration: properties.includes(`${LV2}enumeration`),
      scalePoints
    }
  })
  return described.filter(parameter => parameter.id >= 0)
                 .sort((left, right) => left.id - right.id)
}

/** Normalize a declared value onto the 0..1 the engine's parameter API takes. */
export function normalizeParameter(parameter, value) {
  const span = parameter.maximum - parameter.minimum
  if (!(span > 0)) return 0
  return Math.min(1, Math.max(0, (value - parameter.minimum) / span))
}

/** The inverse, for reporting what a normalized value currently means. */
export function denormalizeParameter(parameter, normalized) {
  const clamped = Math.min(1, Math.max(0, normalized))
  const value = parameter.minimum + clamped * (parameter.maximum - parameter.minimum)
  return parameter.toggled || parameter.enumeration ? Math.round(value) : value
}

function webPluginSubject(dataset, iri) {
  const typed = [...dataset.match(null, named(`${RDF}type`), named(`${JIG}WebPlugin`))]
    .map(quad => quad.subject)
  if (typed.length === 1) return typed[0]
  if (typed.length > 1)
    throw new JigdawProfileError(
      'This document describes more than one jig:WebPlugin, so which one it is about ' +
      'would depend on serialisation order', iri)
  throw new JigdawProfileError('This document declares no jig:WebPlugin', iri)
}

function named(value) {
  return rdf.namedNode(value)
}

/**
 * Read a plugin IRI. http and https ask for Turtle; a file URL naming a
 * directory reads profile.ttl inside it, which is the shape an http plugin IRI
 * has and which content negotiation supplies there.
 */
async function fetchResource(iri) {
  if (iri.startsWith('file://')) {
    let path = fileURLToPath(iri)
    const info = await stat(path).catch(() => null)
    if (info?.isDirectory()) path = `${path.replace(/\/$/, '')}/profile.ttl`
    return { text: await readFile(path, 'utf8'), base: iri }
  }
  const response = await fetch(iri, {
    headers: { Accept: 'text/turtle, application/ld+json;q=0.9' },
    redirect: 'follow'
  })
  if (!response.ok) throw new JigdawProfileError(`${iri} returned ${response.status}`, iri)
  return { text: await response.text(), base: response.url || iri }
}
