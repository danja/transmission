import { describe, it, expect } from 'vitest'
import {
  parseChangeSet,
  parseTransportConfigure,
  parseSetParameter,
  parseProjectOpen,
  parseProjectSave,
  parseArrangementUpdate,
  parseArrangementClipAdd,
  parseArrangementClipRemove,
  parseFreezeGenerator,
  parseParametersBatch,
  parseJigdawDescribe,
  parseCaptureMidi,
  parseRenderMidi,
  parseRenderAudio,
  parseServerConfig,
  serializeStatus,
  serializeError,
  ParseError
} from '../../src/http/TurtleCodec.js'

const TRN = 'http://purl.org/stuff/transmissions/'

describe('parseChangeSet', () => {
  it('parses addNode operation', async () => {
    const node = { id: 'p1', type: `${TRN}VST3Plugin`, label: 'Test', ports: {}, settings: {}, parameters: [], state: {}, metadata: {} }
    const turtle = `
@prefix trn: <${TRN}> .
[] a trn:ChangeSet ;
   trn:expectedRevision 3 ;
   trn:dryRun false ;
   trn:operations (
     [ a trn:AddNode ; trn:nodeJson ${JSON.stringify(JSON.stringify(node))} ]
   ) .
`
    const result = await parseChangeSet(turtle)
    expect(result.expectedRevision).toBe(3)
    expect(result.dryRun).toBe(false)
    expect(result.operations).toHaveLength(1)
    expect(result.operations[0].type).toBe('addNode')
    expect(result.operations[0].node.id).toBe('p1')
  })

  it('parses removeNode operation', async () => {
    const turtle = `
@prefix trn: <${TRN}> .
[] a trn:ChangeSet ;
   trn:expectedRevision 1 ;
   trn:operations (
     [ a trn:RemoveNode ; trn:nodeId "old-node" ]
   ) .
`
    const result = await parseChangeSet(turtle)
    expect(result.operations[0]).toEqual({ type: 'removeNode', nodeId: 'old-node' })
  })

  it('parses addConnection operation', async () => {
    const connection = { from: 'a', to: 'b', kind: 'audio', fromPort: 0, toPort: 0 }
    const turtle = `
@prefix trn: <${TRN}> .
[] a trn:ChangeSet ;
   trn:expectedRevision 2 ;
   trn:operations (
     [ a trn:AddConnection ; trn:connectionJson ${JSON.stringify(JSON.stringify(connection))} ]
   ) .
`
    const result = await parseChangeSet(turtle)
    expect(result.operations[0].connection).toEqual(connection)
  })

  it('throws ParseError when trn:ChangeSet is missing', async () => {
    await expect(parseChangeSet('@prefix trn: <http://x/> . [] a trn:Other .')).rejects.toThrow(ParseError)
  })
})

describe('parseTransportConfigure', () => {
  it('parses tempo and loop', async () => {
    const turtle = `
@prefix trn: <${TRN}> .
[] a trn:ConfigureTransport ;
   trn:expectedRevision 5 ;
   trn:tempo 140.0 ;
   trn:loopStartBeat 0 ;
   trn:loopEndBeat 16 ;
   trn:loopEnabled true .
`
    const result = await parseTransportConfigure(turtle)
    expect(result.expectedRevision).toBe(5)
    expect(result.tempo).toBe(140)
    expect(result.loop).toEqual({ startBeat: 0, endBeat: 16, enabled: true })
  })

  it('parses clearLoop', async () => {
    const turtle = `
@prefix trn: <${TRN}> .
[] a trn:ConfigureTransport ;
   trn:expectedRevision 0 ;
   trn:clearLoop true .
`
    const result = await parseTransportConfigure(turtle)
    expect(result.clearLoop).toBe(true)
    expect(result.loop).toBeUndefined()
  })
})

describe('parseSetParameter', () => {
  it('parses normalizedValue and sampleOffset', async () => {
    const turtle = `
@prefix trn: <${TRN}> .
[] a trn:SetParameter ;
   trn:expectedRevision 4 ;
   trn:normalizedValue 0.75 ;
   trn:sampleOffset 128 .
`
    const result = await parseSetParameter(turtle)
    expect(result.expectedRevision).toBe(4)
    expect(result.value).toBeCloseTo(0.75)
    expect(result.sampleOffset).toBe(128)
  })
})

describe('parseProjectOpen', () => {
  it('parses filePath', async () => {
    const turtle = `
@prefix trn: <${TRN}> .
[] a trn:OpenProject ;
   trn:filePath "projects/patches/transmission.ttl" .
`
    const result = await parseProjectOpen(turtle)
    expect(result.filePath).toBe('projects/patches/transmission.ttl')
  })

  it('throws when filePath is missing', async () => {
    const turtle = `@prefix trn: <${TRN}> . [] a trn:OpenProject .`
    await expect(parseProjectOpen(turtle)).rejects.toThrow(ParseError)
  })
})

describe('parseProjectSave', () => {
  it('parses optional filePath', async () => {
    const turtle = `
@prefix trn: <${TRN}> .
[] a trn:SaveProject ;
   trn:filePath "out.ttl" .
`
    const result = await parseProjectSave(turtle)
    expect(result.filePath).toBe('out.ttl')
  })

  it('returns null filePath when omitted', async () => {
    const turtle = `@prefix trn: <${TRN}> . [] a trn:SaveProject .`
    const result = await parseProjectSave(turtle)
    expect(result.filePath).toBeNull()
  })
})

describe('arrangement codecs', () => {
  const clip = {
    id: 'intro', targetNodeId: 'synth', startBeat: 0, lengthBeats: 4,
    notes: [{ startBeat: 0, durationBeats: 0.5, pitch: 36, velocity: 100, channel: 9 }]
  }

  it('parses a partial arrangement update', async () => {
    const turtle = `
@prefix trn: <${TRN}> .
[] a trn:ArrangementUpdate ;
   trn:expectedRevision 2 ;
   trn:lengthBeats 16 ;
   trn:midiClipsJson ${JSON.stringify(JSON.stringify([clip]))} .
`
    const result = await parseArrangementUpdate(turtle)
    expect(result).toEqual({ expectedRevision: 2, lengthBeats: 16, midiClips: [clip] })
  })

  it('omits absent arrangement update fields', async () => {
    const turtle = `@prefix trn: <${TRN}> . [] a trn:ArrangementUpdate ; trn:expectedRevision 0 .`
    await expect(parseArrangementUpdate(turtle)).resolves.toEqual({ expectedRevision: 0 })
  })

  it('parses clip add and remove', async () => {
    const add = await parseArrangementClipAdd(`
@prefix trn: <${TRN}> .
[] a trn:AddArrangementClip ;
   trn:expectedRevision 1 ;
   trn:clipJson ${JSON.stringify(JSON.stringify(clip))} .
`)
    expect(add).toEqual({ expectedRevision: 1, clip })
    const remove = await parseArrangementClipRemove(`
@prefix trn: <${TRN}> .
[] a trn:RemoveArrangementClip ;
   trn:expectedRevision 1 ;
   trn:clipId "intro" .
`)
    expect(remove).toEqual({ expectedRevision: 1, clipId: 'intro' })
  })

  it('throws ParseError on missing subjects and payloads', async () => {
    const other = `@prefix trn: <${TRN}> . [] a trn:Other .`
    await expect(parseArrangementUpdate(other)).rejects.toThrow(ParseError)
    await expect(parseArrangementClipAdd(other)).rejects.toThrow(ParseError)
    await expect(parseArrangementClipRemove(other)).rejects.toThrow(ParseError)
    const noClip = `@prefix trn: <${TRN}> . [] a trn:AddArrangementClip ; trn:expectedRevision 0 .`
    await expect(parseArrangementClipAdd(noClip)).rejects.toThrow(ParseError)
    const noId = `@prefix trn: <${TRN}> . [] a trn:RemoveArrangementClip ; trn:expectedRevision 0 .`
    await expect(parseArrangementClipRemove(noId)).rejects.toThrow(ParseError)
  })
})

describe('live control codecs', () => {
  it('parses a batch parameter set', async () => {
    const parameters = [{ id: 0, normalizedValue: 0.25 }]
    const turtle = `
@prefix trn: <${TRN}> .
[] a trn:SetParametersBatch ;
   trn:expectedRevision 1 ;
   trn:nodeId "synth" ;
   trn:parametersJson ${JSON.stringify(JSON.stringify(parameters))} ;
   trn:sampleOffset 64 .
`
    await expect(parseParametersBatch(turtle)).resolves.toEqual({
      expectedRevision: 1, nodeId: 'synth', parameters, sampleOffset: 64
    })
  })

  it('parses a JigDAW describe request with a default id', async () => {
    const turtle = `@prefix trn: <${TRN}> . [] a trn:DescribeJigdawPlugin ; trn:iri "file:///plugins/pulse/" .`
    await expect(parseJigdawDescribe(turtle)).resolves.toEqual({ iri: 'file:///plugins/pulse/', id: 'jigdaw-1' })
  })

  it('parses capture and render requests', async () => {
    const capture = await parseCaptureMidi(`
@prefix trn: <${TRN}> .
[] a trn:CaptureProjectMidi ;
   trn:filePath "capture.mid" ;
   trn:durationBeats 4 .
`)
    expect(capture).toEqual({ filePath: 'capture.mid', durationBeats: 4 })
    const captureDefault = await parseCaptureMidi(
      `@prefix trn: <${TRN}> . [] a trn:CaptureProjectMidi ; trn:filePath "c.mid" .`)
    expect(captureDefault.durationBeats).toBe(64)
    const render = await parseRenderMidi(
      `@prefix trn: <${TRN}> . [] a trn:RenderMidi ; trn:filePath "out.mid" .`)
    expect(render).toEqual({ filePath: 'out.mid' })
  })

  it('throws ParseError on missing subjects and payloads', async () => {
    const other = `@prefix trn: <${TRN}> . [] a trn:Other .`
    await expect(parseParametersBatch(other)).rejects.toThrow(ParseError)
    await expect(parseJigdawDescribe(other)).rejects.toThrow(ParseError)
    await expect(parseCaptureMidi(other)).rejects.toThrow(ParseError)
    await expect(parseRenderMidi(other)).rejects.toThrow(ParseError)
    const noNode = `@prefix trn: <${TRN}> . [] a trn:SetParametersBatch ; trn:expectedRevision 0 ; trn:parametersJson "[]" .`
    await expect(parseParametersBatch(noNode)).rejects.toThrow(ParseError)
    const noParams = `@prefix trn: <${TRN}> . [] a trn:SetParametersBatch ; trn:expectedRevision 0 ; trn:nodeId "n" .`
    await expect(parseParametersBatch(noParams)).rejects.toThrow(ParseError)
    const noIri = `@prefix trn: <${TRN}> . [] a trn:DescribeJigdawPlugin .`
    await expect(parseJigdawDescribe(noIri)).rejects.toThrow(ParseError)
    const noFile = `@prefix trn: <${TRN}> . [] a trn:CaptureProjectMidi .`
    await expect(parseCaptureMidi(noFile)).rejects.toThrow(ParseError)
    const noRenderFile = `@prefix trn: <${TRN}> . [] a trn:RenderMidi .`
    await expect(parseRenderMidi(noRenderFile)).rejects.toThrow(ParseError)
  })
})

describe('parseRenderAudio', () => {
  it('parses file path with optional render options', async () => {
    const turtle = `
@prefix trn: <${TRN}> .
[] a trn:RenderAudio ;
   trn:filePath "bounce.wav" ;
   trn:totalBeats 16 ;
   trn:tempo 128 ;
   trn:sampleRate 44100 ;
   trn:blockSize 512 .
`
    await expect(parseRenderAudio(turtle)).resolves.toEqual({
      filePath: 'bounce.wav', totalBeats: 16, tempo: 128, sampleRate: 44100, blockSize: 512
    })
  })

  it('leaves absent options undefined for control defaults', async () => {
    const turtle = `@prefix trn: <${TRN}> . [] a trn:RenderAudio ; trn:filePath "b.wav" .`
    await expect(parseRenderAudio(turtle)).resolves.toEqual({
      filePath: 'b.wav', totalBeats: undefined, tempo: undefined,
      sampleRate: undefined, blockSize: undefined
    })
  })

  it('throws ParseError on missing subject and file path', async () => {
    await expect(parseRenderAudio(`@prefix trn: <${TRN}> . [] a trn:Other .`)).rejects.toThrow(ParseError)
    await expect(parseRenderAudio(`@prefix trn: <${TRN}> . [] a trn:RenderAudio .`)).rejects.toThrow(ParseError)
  })
})

describe('parseFreezeGenerator', () => {
  it('parses full and partial freeze requests', async () => {
    const full = await parseFreezeGenerator(`
@prefix trn: <${TRN}> .
[] a trn:FreezeGenerator ;
   trn:expectedRevision 2 ;
   trn:sourceNodeId "gen" ;
   trn:targetNodeId "syn" ;
   trn:clipId "frozen" ;
   trn:startBeat 4 ;
   trn:lengthBeats 16 ;
   trn:durationBeats 16 .
`)
    expect(full).toEqual({
      expectedRevision: 2, sourceNodeId: 'gen', targetNodeId: 'syn',
      clipId: 'frozen', startBeat: 4, lengthBeats: 16, durationBeats: 16
    })
    const partial = await parseFreezeGenerator(
      `@prefix trn: <${TRN}> . [] a trn:FreezeGenerator ; trn:sourceNodeId "g" ; trn:targetNodeId "s" .`)
    expect(partial).toMatchObject({ sourceNodeId: 'g', targetNodeId: 's' })
    expect(partial.clipId).toBeUndefined()
  })

  it('throws ParseError on missing subject and node ids', async () => {
    const other = `@prefix trn: <${TRN}> . [] a trn:Other .`
    await expect(parseFreezeGenerator(other)).rejects.toThrow(ParseError)
    const noSource = `@prefix trn: <${TRN}> . [] a trn:FreezeGenerator ; trn:targetNodeId "s" .`
    await expect(parseFreezeGenerator(noSource)).rejects.toThrow(ParseError)
    const noTarget = `@prefix trn: <${TRN}> . [] a trn:FreezeGenerator ; trn:sourceNodeId "g" .`
    await expect(parseFreezeGenerator(noTarget)).rejects.toThrow(ParseError)
  })
})

describe('parseServerConfig', () => {
  it('parses port and bindAddress', async () => {
    const turtle = `
@prefix trn: <${TRN}> .
@prefix xsd: <http://www.w3.org/2001/XMLSchema#> .
[] a trn:ServerConfig ;
   trn:port 7878 ;
   trn:bindAddress "127.0.0.1" ;
   trn:allowedRoots ( "." ) .
`
    const result = await parseServerConfig(turtle)
    expect(result.port).toBe(7878)
    expect(result.bindAddress).toBe('127.0.0.1')
    expect(result.allowedRoots).toContain('.')
  })
})

describe('serializeStatus', () => {
  it('round-trips key fields to Turtle', () => {
    const status = {
      revision: 7,
      dirty: true,
      projectOpen: true,
      engineAvailable: false,
      engineState: 'stopped',
      projectId: 'http://example.org/test',
      filePath: '/tmp/test.ttl',
      transport: { running: false, tempoMap: [{ beat: 0, bpm: 120 }], positionBeats: 4 }
    }
    const turtle = serializeStatus(status)
    expect(turtle).toContain('trn:revision 7')
    expect(turtle).toContain('trn:dirty true')
    expect(turtle).toContain('trn:bpm 120')
    expect(turtle).toContain('trn:positionBeats 4')
  })
})

describe('serializeError', () => {
  it('produces a Turtle error body', () => {
    const turtle = serializeError('Something went wrong', 'ParseError')
    expect(turtle).toContain('trn:ParseError')
    expect(turtle).toContain('Something went wrong')
  })
})
