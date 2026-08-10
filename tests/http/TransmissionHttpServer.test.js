import { describe, it, expect, beforeEach, afterEach } from 'vitest'
import { TransmissionHttpServer } from '../../src/http/TransmissionHttpServer.js'
import { request as httpRequest } from 'node:http'

const TRN = 'http://purl.org/stuff/transmissions/'

// ── Minimal mock control service ──────────────────────────────────────────────

function mockControl(overrides = {}) {
  return {
    status: () => ({
      revision: 1, dirty: false, projectOpen: true, engineAvailable: false,
      engineState: 'stopped', projectId: `${TRN}test`, filePath: null,
      transport: { running: false, tempoMap: [{ beat: 0, bpm: 120 }], positionBeats: 0 }
    }),
    projectTurtle: () => `@prefix : <${TRN}> .\n:test a :Transmission .\n`,
    diagnostics: () => ({ native: null }),
    plugins: () => ({ entries: [] }),
    waitForPluginScan: async () => {},
    pluginProfilesTurtle: () => `@prefix trn: <${TRN}> .\n`,
    discoveredPluginsTurtle: () => `@prefix trn: <${TRN}> .\n`,
    describePlugin: id => { throw new Error(`Unknown plugin: ${id}`) },
    applyGraphChanges: input => ({ dryRun: input.dryRun, revision: 2, graph: {}, executionOrder: [] }),
    startTransport: () => ({ revision: 1, projectOpen: true, dirty: false, engineAvailable: false, engineState: 'running', transport: { running: true, tempoMap: [], positionBeats: 0 } }),
    stopTransport: () => ({ revision: 1, projectOpen: true, dirty: false, engineAvailable: false, engineState: 'stopped', transport: { running: false, tempoMap: [], positionBeats: 0 } }),
    configureTransport: () => ({ revision: 2, transport: { running: false, tempoMap: [{ beat: 0, bpm: 140 }], positionBeats: 0 } }),
    setParameter: input => ({ revision: 1, nodeId: input.nodeId, parameterId: input.parameterId, value: input.value, appliedToRuntime: false }),
    newProject: def => ({ revision: 0, graph: def }),
    openProject: async () => ({ revision: 0, graph: {} }),
    saveProject: async () => ({ filePath: '/tmp/test.ttl', revision: 1 }),
    scanPlugins: async () => ({ discovered: 0, profiled: 0, failures: [] }),
    ...overrides
  }
}

// ── HTTP helpers ──────────────────────────────────────────────────────────────

function fetch_(url, { method = 'GET', headers = {}, body = '' } = {}) {
  return new Promise((resolve, reject) => {
    const parsed = new URL(url)
    const bodyBuffer = Buffer.from(body, 'utf8')
    const req = httpRequest({
      hostname: parsed.hostname,
      port: parsed.port,
      path: parsed.pathname,
      method,
      headers: { ...headers, 'Content-Length': bodyBuffer.length }
    }, res => {
      const chunks = []
      res.on('data', c => chunks.push(c))
      res.on('end', () => resolve({ status: res.statusCode, body: Buffer.concat(chunks).toString('utf8'), headers: res.headers }))
    })
    req.on('error', reject)
    if (bodyBuffer.length) req.write(bodyBuffer)
    req.end()
  })
}

// ── Tests ─────────────────────────────────────────────────────────────────────

describe('TransmissionHttpServer', () => {
  let server
  let base

  beforeEach(async () => {
    server = new TransmissionHttpServer(mockControl(), { port: 0, bindAddress: '127.0.0.1' })
    await server.listen()
    base = `http://127.0.0.1:${server._server.address().port}`
  })

  afterEach(async () => {
    await server.close()
  })

  it('GET /status returns Turtle', async () => {
    const res = await fetch_(`${base}/status`)
    expect(res.status).toBe(200)
    expect(res.headers['content-type']).toContain('text/turtle')
    expect(res.body).toContain('trn:revision')
  })

  it('GET /graph returns Turtle', async () => {
    const res = await fetch_(`${base}/graph`)
    expect(res.status).toBe(200)
    expect(res.headers['content-type']).toContain('text/turtle')
    expect(res.body).toContain('Transmission')
  })

  it('GET /diagnostics returns JSON', async () => {
    const res = await fetch_(`${base}/diagnostics`)
    expect(res.status).toBe(200)
    expect(res.headers['content-type']).toContain('application/json')
    const data = JSON.parse(res.body)
    expect(data).toHaveProperty('native')
  })

  it('GET /plugins returns JSON', async () => {
    const res = await fetch_(`${base}/plugins`)
    expect(res.status).toBe(200)
    const data = JSON.parse(res.body)
    expect(data).toHaveProperty('entries')
  })

  it('POST /transport/play returns Turtle status', async () => {
    const res = await fetch_(`${base}/transport/play`, { method: 'POST' })
    expect(res.status).toBe(200)
    expect(res.headers['content-type']).toContain('text/turtle')
    expect(res.body).toContain('trn:engineState')
  })

  it('POST /transport/stop returns Turtle status', async () => {
    const res = await fetch_(`${base}/transport/stop`, { method: 'POST' })
    expect(res.status).toBe(200)
    expect(res.body).toContain('trn:engineState')
  })

  it('POST /graph/changes with valid ChangeSet returns 200 JSON', async () => {
    const turtle = `
@prefix trn: <${TRN}> .
[] a trn:ChangeSet ;
   trn:expectedRevision 1 ;
   trn:dryRun true ;
   trn:operations (
     [ a trn:RemoveNode ; trn:nodeId "x" ]
   ) .
`
    const res = await fetch_(`${base}/graph/changes`, {
      method: 'POST',
      headers: { 'Content-Type': 'text/turtle' },
      body: turtle
    })
    expect(res.status).toBe(200)
    const data = JSON.parse(res.body)
    expect(data.dryRun).toBe(true)
  })

  it('POST /graph/changes with bad Turtle returns 422', async () => {
    const res = await fetch_(`${base}/graph/changes`, {
      method: 'POST',
      headers: { 'Content-Type': 'text/turtle' },
      body: '@prefix trn: <x> . [] a trn:Other .'
    })
    expect(res.status).toBe(422)
    expect(res.body).toContain('ParseError')
  })

  it('POST /projects/open returns JSON', async () => {
    const turtle = `@prefix trn: <${TRN}> . [] a trn:OpenProject ; trn:filePath "test.ttl" .`
    const res = await fetch_(`${base}/projects/open`, {
      method: 'POST',
      headers: { 'Content-Type': 'text/turtle' },
      body: turtle
    })
    expect(res.status).toBe(200)
  })

  it('GET /nonexistent returns 404', async () => {
    const res = await fetch_(`${base}/nonexistent`)
    expect(res.status).toBe(404)
  })
})
