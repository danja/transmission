#!/usr/bin/env node
// scripts/dj-runner.js
// Executes a trn:DJSet set list against the Transmission live server.
//
// Usage:
//   node scripts/dj-runner.js <setlist.ttl> [--base <projects-dir>] [--server <url>]
//
// Reads a Turtle set list, resolves patch file paths against the projects
// base directory, and drives the live server via HTTP for each cue in order.
// Waits the declared durationBeats (at each patch's own tempo) between cues.
// durationBeats 0 means play indefinitely — press Ctrl-C to stop.

import { readFile } from 'node:fs/promises'
import { resolve, dirname, join } from 'node:path'
import { fileURLToPath } from 'node:url'

const repositoryRoot = resolve(dirname(fileURLToPath(import.meta.url)), '..')
const projectsRoot   = join(repositoryRoot, 'projects')

// ── Argument parsing ──────────────────────────────────────────────────────────

function parseArgs(argv) {
  const args = { setlistPath: null, base: projectsRoot, server: 'http://localhost:7878' }
  for (let i = 0; i < argv.length; i++) {
    if (argv[i] === '--base')   { args.base = resolve(argv[++i]); continue }
    if (argv[i] === '--server') { args.server = argv[++i]; continue }
    if (!args.setlistPath)       args.setlistPath = resolve(argv[i])
  }
  return args
}

const args = parseArgs(process.argv.slice(2))
if (!args.setlistPath) {
  console.error('Usage: dj-runner.js <setlist.ttl> [--base <dir>] [--server <url>]')
  process.exit(1)
}

// ── Minimal N3-based Turtle parser ────────────────────────────────────────────
// Dynamically import N3 (same dep used by the rest of the project).

async function parseSetList(ttlPath) {
  const { default: N3 } = await import('n3')
  const TRN = 'http://purl.org/stuff/transmissions/'
  const RDF = 'http://www.w3.org/1999/02/22-rdf-syntax-ns#'

  const turtle = await readFile(ttlPath, 'utf8')
  const parser = new N3.Parser({ format: 'Turtle', baseIRI: `file://${ttlPath}` })
  const store  = new N3.Store()
  store.addQuads(parser.parse(turtle))

  const namedNode = s => N3.DataFactory.namedNode(s)
  const term = s => namedNode(TRN + s)

  // Find the DJSet subject
  const [setQuad] = store.getQuads(null, namedNode(RDF + 'type'), term('DJSet'), null)
  if (!setQuad) throw new Error('No trn:DJSet found in ' + ttlPath)
  const setNode = setQuad.subject

  const literal = (subj, pred) => store.getObjects(subj, namedNode(pred), null)[0]?.value ?? null
  const obj     = (subj, pred) => store.getObjects(subj, namedNode(TRN + pred), null)[0] ?? null

  // Walk rdf:List
  function listItems(head) {
    const items = []
    let node = head
    while (node && node.value !== RDF + 'nil') {
      const first = store.getObjects(node, namedNode(RDF + 'first'), null)[0]
      if (first) items.push(first)
      node = store.getObjects(node, namedNode(RDF + 'rest'), null)[0]
    }
    return items
  }

  const cuesHead = obj(setNode, 'cues')
  if (!cuesHead) throw new Error('trn:DJSet has no trn:cues list')

  const cues = listItems(cuesHead).map(cueNode => {
    const filePath = literal(cueNode, TRN + 'filePath')
    const duration = parseInt(literal(cueNode, TRN + 'durationBeats') ?? '0', 10)
    const tempo    = parseFloat(literal(cueNode, TRN + 'tempo') ?? '0')
    const label    = literal(cueNode, 'http://www.w3.org/2000/01/rdf-schema#label')
    return { label, filePath, durationBeats: duration, tempoOverride: tempo || null }
  })

  return {
    label:  literal(setNode, 'http://www.w3.org/2000/01/rdf-schema#label') ?? 'DJ Set',
    artist: literal(setNode, TRN + 'artist'),
    cues
  }
}

// ── HTTP helpers ──────────────────────────────────────────────────────────────

const TRN = 'http://purl.org/stuff/transmissions/'

async function postTurtle(server, path, turtle) {
  const url = server + path
  const res = await fetch(url, {
    method: 'POST',
    headers: { 'Content-Type': 'text/turtle' },
    body: turtle ?? ''
  })
  if (!res.ok) {
    const text = await res.text().catch(() => '')
    throw new Error(`POST ${path} → ${res.status}: ${text}`)
  }
  const ct = res.headers.get('content-type') ?? ''
  return ct.includes('json') ? res.json() : res.text()
}

async function getDiagnostics(server) {
  const res = await fetch(server + '/diagnostics')
  if (!res.ok) throw new Error(`GET /diagnostics → ${res.status}`)
  return res.json()
}

function turtleString(s) {
  return `"${s.replace(/\\/g, '\\\\').replace(/"/g, '\\"')}"`
}

async function openPatch(server, filePath) {
  const turtle = `@prefix trn: <${TRN}> .\n[] a trn:OpenProject ; trn:filePath ${turtleString(filePath)} .\n`
  return postTurtle(server, '/projects/open', turtle)
}

async function play(server) {
  return postTurtle(server, '/transport/play', '')
}

async function stop(server) {
  return postTurtle(server, '/transport/stop', '')
}

// ── Sleep ─────────────────────────────────────────────────────────────────────

function sleep(ms) {
  return new Promise(resolve => setTimeout(resolve, ms))
}

// ── Main ─────────────────────────────────────────────────────────────────────

async function main() {
  console.log(`Loading set list: ${args.setlistPath}`)
  const set = await parseSetList(args.setlistPath)
  console.log(`Set: "${set.label}" by ${set.artist ?? 'unknown'}`)
  console.log(`Cues: ${set.cues.length}`)
  console.log(`Server: ${args.server}`)

  // Check server reachable
  try {
    await getDiagnostics(args.server)
  } catch (e) {
    console.error('Cannot reach live server:', e.message)
    process.exit(1)
  }

  for (let i = 0; i < set.cues.length; i++) {
    const cue = set.cues[i]
    const patchPath = join(args.base, cue.filePath)
    console.log(`\n[${i + 1}/${set.cues.length}] ${cue.label ?? cue.filePath}`)
    console.log(`  Patch: ${patchPath}`)

    try {
      await stop(args.server)
    } catch { /* already stopped is fine */ }

    await openPatch(args.server, patchPath)

    const diag = await getDiagnostics(args.server)
    const bpm = cue.tempoOverride
      ?? diag?.transport?.tempoMap?.[0]?.bpm
      ?? 120

    console.log(`  BPM: ${bpm}`)

    await play(args.server)

    if (cue.durationBeats <= 0) {
      console.log('  Playing indefinitely. Press Ctrl-C to stop.')
      await new Promise(() => {}) // block forever
    }

    const durationMs = (cue.durationBeats / bpm) * 60_000
    console.log(`  Duration: ${cue.durationBeats} beats (${(durationMs / 1000).toFixed(1)}s)`)
    await sleep(durationMs)
  }

  console.log('\nSet complete.')
  try { await stop(args.server) } catch { /* ignore */ }
}

main().catch(e => { console.error(e.message); process.exit(1) })
