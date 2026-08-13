import { mkdir, writeFile } from 'node:fs/promises'
import { dirname } from 'node:path'

const PPQ = 480

export function arrangementToSmf(arrangement, transport) {
  const tempoMap = (transport?.tempoMap?.length ? transport.tempoMap : [{ beat: 0, bpm: 120 }])
    .slice()
    .sort((a, b) => a.beat - b.beat)

  const byNode = new Map()
  for (const clip of (arrangement?.midiClips ?? [])) {
    if (!byNode.has(clip.targetNodeId)) byNode.set(clip.targetNodeId, [])
    byNode.get(clip.targetNodeId).push(clip)
  }

  const tracks = [buildTempoTrack(tempoMap)]
  for (const [nodeId, clips] of byNode) tracks.push(buildNoteTrack(nodeId, clips))

  return buildSmf(tracks)
}

export async function writeSmf(filePath, arrangement, transport) {
  const buffer = arrangementToSmf(arrangement, transport)
  await mkdir(dirname(filePath), { recursive: true })
  await writeFile(filePath, buffer)
  const clips = arrangement?.midiClips ?? []
  const tracks = new Set(clips.map(c => c.targetNodeId)).size
  return { filePath, bytes: buffer.length, tracks, clips: clips.length }
}

function buildTempoTrack(tempoMap) {
  const events = [trackNameEvent(0, 'Tempo')]
  for (const { beat, bpm } of tempoMap) {
    const usPerBeat = Math.round(60_000_000 / bpm)
    events.push({
      tick: beatToTick(beat),
      bytes: [0xFF, 0x51, 0x03,
        (usPerBeat >> 16) & 0xFF, (usPerBeat >> 8) & 0xFF, usPerBeat & 0xFF]
    })
  }
  events.push(endOfTrack(events[events.length - 1].tick))
  return eventsToTrack(events)
}

function buildNoteTrack(nodeId, clips) {
  const label = nodeId.includes('/') ? nodeId.split('/').pop() : nodeId
  const events = [trackNameEvent(0, label)]

  for (const clip of clips) {
    for (const note of clip.notes) {
      const absStart = clip.startBeat + note.startBeat
      const absEnd = absStart + note.durationBeats
      const ch = note.channel & 0x0F
      events.push({ tick: beatToTick(absStart), bytes: [0x90 | ch, note.pitch, note.velocity] })
      events.push({ tick: beatToTick(absEnd),   bytes: [0x80 | ch, note.pitch, 0] })
    }
  }

  events.sort((a, b) => {
    if (a.tick !== b.tick) return a.tick - b.tick
    const aOff = (a.bytes[0] & 0xF0) === 0x80
    const bOff = (b.bytes[0] & 0xF0) === 0x80
    return aOff === bOff ? 0 : aOff ? -1 : 1
  })

  events.push(endOfTrack(events[events.length - 1].tick))
  return eventsToTrack(events)
}

function eventsToTrack(events) {
  const data = []
  let currentTick = 0
  for (const event of events) {
    const delta = Math.max(0, event.tick - currentTick)
    currentTick = event.tick
    data.push(...varLen(delta), ...event.bytes)
  }
  const chunk = Buffer.alloc(8 + data.length)
  chunk.write('MTrk', 0, 'ascii')
  chunk.writeUInt32BE(data.length, 4)
  for (let i = 0; i < data.length; i++) chunk[8 + i] = data[i]
  return chunk
}

function buildSmf(trackBuffers) {
  const header = Buffer.alloc(14)
  header.write('MThd', 0, 'ascii')
  header.writeUInt32BE(6, 4)
  header.writeUInt16BE(trackBuffers.length === 1 ? 0 : 1, 8)
  header.writeUInt16BE(trackBuffers.length, 10)
  header.writeUInt16BE(PPQ, 12)
  return Buffer.concat([header, ...trackBuffers])
}

export async function writeSmfFromEvents(filePath, events, nodeLabels = {}, transport = null) {
  const tempoMap = (transport?.tempoMap?.length ? transport.tempoMap : [{ beat: 0, bpm: 120 }])
    .slice().sort((a, b) => a.beat - b.beat)

  const byNode = new Map()
  for (const event of (events ?? [])) {
    if (!byNode.has(event.nodeId)) byNode.set(event.nodeId, [])
    byNode.get(event.nodeId).push(event)
  }

  const tracks = [buildTempoTrack(tempoMap)]
  for (const [nodeId, nodeEvents] of byNode) {
    const label = nodeLabels[nodeId] ?? nodeId.split('/').pop()
    tracks.push(buildRawEventTrack(label, nodeEvents))
  }

  const buffer = buildSmf(tracks)
  await mkdir(dirname(filePath), { recursive: true })
  await writeFile(filePath, buffer)
  return { filePath, bytes: buffer.length, tracks: byNode.size, events: (events ?? []).length }
}

function buildRawEventTrack(label, events) {
  const sorted = events
    .filter(e => e.status >= 0x80 && e.status <= 0xEF)
    .map(e => ({ tick: beatToTick(e.beatPosition ?? 0), bytes: [e.status, e.data1 ?? 0, e.data2 ?? 0] }))
    .sort((a, b) => a.tick - b.tick)

  const trackEvents = [trackNameEvent(0, label), ...sorted, endOfTrack(sorted[sorted.length - 1]?.tick ?? 0)]
  return eventsToTrack(trackEvents)
}

function trackNameEvent(tick, name) {
  const bytes = Array.from(name.slice(0, 127)).map(c => c.charCodeAt(0))
  return { tick, bytes: [0xFF, 0x03, bytes.length, ...bytes] }
}

function endOfTrack(tick) {
  return { tick, bytes: [0xFF, 0x2F, 0x00] }
}

function beatToTick(beat) {
  return Math.round(beat * PPQ)
}

function varLen(value) {
  if (value < 0x80) return [value]
  const bytes = []
  let v = value
  while (v > 0) { bytes.unshift(v & 0x7F); v >>>= 7 }
  for (let i = 0; i < bytes.length - 1; i++) bytes[i] |= 0x80
  return bytes
}
