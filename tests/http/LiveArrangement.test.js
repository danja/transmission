import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import { TransmissionControlService } from '../../src/control/TransmissionControlService.js'
import { TransmissionHttpServer } from '../../src/http/TransmissionHttpServer.js'
import { TransmissionHttpClient } from '../../src/http/TransmissionHttpClient.js'

const TRN = 'http://purl.org/stuff/transmissions/'

const definition = {
  id: `${TRN}live-arrangement`,
  nodes: [
    { id: `${TRN}gen`, type: `${TRN}VST3Plugin`, ports: { audioOutputs: 2 } },
    { id: `${TRN}out`, type: `${TRN}AudioOutput`, ports: { audioInputs: 2 } }
  ],
  connections: [{ from: `${TRN}gen`, to: `${TRN}out`, kind: 'audio' }]
}

const clip = {
  id: 'intro',
  targetNodeId: `${TRN}gen`,
  startBeat: 0,
  lengthBeats: 4,
  notes: [{ startBeat: 0, durationBeats: 0.5, pitch: 36, velocity: 100, channel: 9 }]
}

describe('arrangement over live HTTP', () => {
  let server
  let client

  beforeEach(async () => {
    const control = new TransmissionControlService()
    server = new TransmissionHttpServer(control, { port: 0, bindAddress: '127.0.0.1' })
    await server.listen()
    client = new TransmissionHttpClient(`http://127.0.0.1:${server._server.address().port}`)
    await client.newProject(definition)
  })

  afterEach(async () => {
    await server.close()
  })

  it('reads an empty arrangement, then updates length and clips', async () => {
    const initial = await client.getArrangement()
    expect(initial.arrangement).toMatchObject({ lengthBeats: 0, midiClips: [] })

    const updated = await client.updateArrangement({ expectedRevision: initial.revision, lengthBeats: 16 })
    expect(updated.arrangement.lengthBeats).toBe(16)

    const added = await client.addArrangementClip({ expectedRevision: updated.revision, clip })
    expect(added.arrangement.midiClips).toHaveLength(1)
    expect(added.arrangement.midiClips[0]).toMatchObject({ id: 'intro', targetNodeId: `${TRN}gen` })

    const reread = await client.getArrangement()
    expect(reread.arrangement.midiClips).toHaveLength(1)

    const removed = await client.removeArrangementClip({ expectedRevision: added.revision, clipId: 'intro' })
    expect(removed.arrangement.midiClips).toEqual([])
  })

  it('rejects duplicate clip ids', async () => {
    const { revision } = await client.getArrangement()
    await client.addArrangementClip({ expectedRevision: revision, clip })
    await expect(client.addArrangementClip({ expectedRevision: revision + 1, clip }))
      .rejects.toThrow('already exists')
  })

  it('rejects invalid clips without changing the arrangement', async () => {
    const { revision } = await client.getArrangement()
    const badPitch = { ...clip, id: 'bad', notes: [{ ...clip.notes[0], pitch: 200 }] }
    await expect(client.addArrangementClip({ expectedRevision: revision, clip: badPitch }))
      .rejects.toThrow()
    const missingTarget = { ...clip, id: 'ghost', targetNodeId: `${TRN}nope` }
    await expect(client.addArrangementClip({ expectedRevision: revision, clip: missingTarget }))
      .rejects.toThrow('missing graph node')
    const reread = await client.getArrangement()
    expect(reread.arrangement.midiClips).toEqual([])
    expect(reread.revision).toBe(revision)
  })

  it('rejects stale revisions', async () => {
    await expect(client.updateArrangement({ expectedRevision: 999, lengthBeats: 4 }))
      .rejects.toThrow('409')
  })

  it('reads peaks', async () => {
    await expect(client.peaks()).resolves.toMatchObject({ peakL: 0, peakR: 0 })
  })
})
