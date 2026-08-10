import { describe, it, expect } from 'vitest'
import {
  parseChangeSet,
  parseTransportConfigure,
  parseSetParameter,
  parseProjectOpen,
  parseProjectSave,
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
