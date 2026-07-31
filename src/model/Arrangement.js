export class Arrangement {
  constructor(definition = {}, graph = null) {
    const lengthBeats = finiteNumber(definition.lengthBeats ?? 0, 'arrangement length')
    if (lengthBeats < 0) throw new RangeError('arrangement length must not be negative')
    this.lengthBeats = lengthBeats
    this.midiClips = (definition.midiClips ?? []).map(clip => validateClip(clip, lengthBeats, graph))
    this.gainLanes = (definition.gainLanes ?? []).map(lane => validateGainLane(lane, lengthBeats, graph))
    uniqueIds(this.midiClips, 'MIDI clip')
  }

  toJSON() {
    return {
      lengthBeats: this.lengthBeats,
      midiClips: this.midiClips.map(clip => ({ ...clip, notes: clip.notes.map(note => ({ ...note })) })),
      gainLanes: this.gainLanes.map(lane => ({ ...lane, points: lane.points.map(point => ({ ...point })) }))
    }
  }
}

function validateClip(source, arrangementLength, graph) {
  const id = requiredString(source?.id, 'MIDI clip ID')
  const targetNodeId = requiredString(source?.targetNodeId, `MIDI clip ${id} target`)
  assertTarget(graph, targetNodeId, `MIDI clip ${id}`)
  const startBeat = nonNegative(source.startBeat, `MIDI clip ${id} start`)
  const lengthBeats = positive(source.lengthBeats, `MIDI clip ${id} length`)
  if (arrangementLength > 0 && startBeat + lengthBeats > arrangementLength)
    throw new RangeError(`MIDI clip ${id} exceeds the arrangement length`)
  const notes = (source.notes ?? []).map((note, index) => validateNote(note, id, index, lengthBeats))
  return { id, targetNodeId, startBeat, lengthBeats, notes }
}

function validateNote(source, clipId, index, clipLength) {
  const label = `MIDI clip ${clipId} note ${index}`
  const startBeat = nonNegative(source?.startBeat, `${label} start`)
  const durationBeats = positive(source?.durationBeats, `${label} duration`)
  if (startBeat + durationBeats > clipLength) throw new RangeError(`${label} exceeds its clip`)
  const pitch = midiInteger(source?.pitch, `${label} pitch`, 0, 127)
  const velocity = midiInteger(source?.velocity, `${label} velocity`, 1, 127)
  const channel = midiInteger(source?.channel ?? 0, `${label} channel`, 0, 15)
  return { startBeat, durationBeats, pitch, velocity, channel }
}

function validateGainLane(source, arrangementLength, graph) {
  const targetNodeId = requiredString(source?.targetNodeId, 'gain lane target')
  assertTarget(graph, targetNodeId, `gain lane for ${targetNodeId}`)
  const points = (source.points ?? []).map((point, index) => {
    const beat = nonNegative(point?.beat, `gain point ${index} beat`)
    if (arrangementLength > 0 && beat > arrangementLength)
      throw new RangeError(`gain point ${index} exceeds the arrangement length`)
    const valueDb = finiteNumber(point?.valueDb, `gain point ${index} value`)
    const shape = point?.shape ?? 'linear'
    if (!['step', 'linear'].includes(shape)) throw new RangeError(`invalid gain point ${index} shape`)
    return { beat, valueDb, shape }
  })
  for (let index = 1; index < points.length; ++index)
    if (points[index].beat <= points[index - 1].beat)
      throw new RangeError('gain points must have strictly increasing beats')
  return { targetNodeId, points }
}

function assertTarget(graph, target, label) {
  if (graph && !graph.nodes.has(target)) throw new RangeError(`${label} targets a missing graph node: ${target}`)
}

function uniqueIds(values, label) {
  const ids = new Set()
  for (const value of values)
    if (ids.has(value.id)) throw new RangeError(`duplicate ${label} ID: ${value.id}`)
    else ids.add(value.id)
}

function requiredString(value, label) {
  if (typeof value !== 'string' || !value) throw new TypeError(`${label} must be a non-empty string`)
  return value
}

function finiteNumber(value, label) {
  const result = Number(value)
  if (!Number.isFinite(result)) throw new TypeError(`${label} must be finite`)
  return result
}

function nonNegative(value, label) {
  const result = finiteNumber(value, label)
  if (result < 0) throw new RangeError(`${label} must not be negative`)
  return result
}

function positive(value, label) {
  const result = finiteNumber(value, label)
  if (result <= 0) throw new RangeError(`${label} must be positive`)
  return result
}

function midiInteger(value, label, minimum, maximum) {
  const result = finiteNumber(value, label)
  if (!Number.isInteger(result) || result < minimum || result > maximum)
    throw new RangeError(`${label} must be an integer from ${minimum} to ${maximum}`)
  return result
}
