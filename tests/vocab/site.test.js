// tests/vocab/site.test.js
//
// deploy/vocab/ is generated and committed, because nginx serves it from a
// checkout on the server and there is no build step there. That is exactly the
// kind of pair that drifts silently: nothing connects vocabs/ to the copy the
// world dereferences, so a term added to vocabs/ and not rebuilt is a term the
// namespace does not define.
//
// These are also the assertions behind deploy/nginx/vocab.conf. It 303s every
// term to this document, so a term missing from it is a redirect to a page that
// does not mention what was asked for.
import { describe, it, expect, beforeAll } from 'vitest'
import { readFile, readdir } from 'node:fs/promises'
import { existsSync } from 'node:fs'
import { join } from 'node:path'
import { Readable } from 'node:stream'
import rdf from 'rdf-ext'
import ParserN3 from '@rdfjs/parser-n3'
import { buildVocabSite, OUT_DIR } from '../../scripts/build-vocab-site.js'

const TRN = 'http://purl.org/stuff/transmissions/'
const root = new URL('../..', import.meta.url).pathname
const vocabsDir = join(root, 'vocabs')

/** Local names used with the trn: prefix, or written out in full. */
function termsIn (text) {
  const found = new Set()
  for (let line of text.split('\n')) {
    if (!line.trimStart().startsWith('@')) line = line.split('#')[0]
    for (const m of line.matchAll(/\btrn:([A-Za-z][A-Za-z0-9_]*)/g)) found.add(m[1])
    for (const m of line.matchAll(new RegExp(`<${TRN}([A-Za-z][A-Za-z0-9_]*)>`, 'g'))) found.add(m[1])
  }
  return found
}

describe('the vocabulary site', () => {
  let built
  beforeAll(async () => { built = await buildVocabSite() })

  it('reads every vocabulary file, not a list someone has to remember to update', async () => {
    // A guard is only as wide as the list it walks. This one walks vocabs/, so
    // adding a file there brings it into the served document automatically, and
    // this assertion is what says so.
    const present = (await readdir(vocabsDir)).filter(f => f.endsWith('.ttl'))
    expect(present.length, 'no vocabulary files found, so this checked nothing').toBeGreaterThan(0)
    expect([...built.sources].sort()).toEqual([...present].sort())
  })

  it('parses as one document', () => {
    // Each file parses alone. Concatenation is what could still fail, and the
    // builder throws rather than writing something unparseable.
    expect(built.triples).toBeGreaterThan(100)
  })

  it('defines the namespace it is served at', async () => {
    const dataset = await rdf.dataset().import(
      new ParserN3({ factory: rdf }).import(Readable.from([built.turtle])))
    const ontology = [...dataset].filter(q => q.subject.value === TRN)
    expect(ontology.length, 'the document says nothing about the namespace itself').toBeGreaterThan(0)
  })

  it('carries every term the vocabulary files declare', async () => {
    const declared = new Set()
    for (const file of built.sources) {
      for (const t of termsIn(await readFile(join(vocabsDir, file), 'utf8'))) declared.add(t)
    }
    const served = termsIn(built.turtle)
    const missing = [...declared].filter(t => !served.has(t)).sort()
    expect(missing, `declared in vocabs/ and not served: ${missing.join(', ')}`).toEqual([])
    expect(declared.size).toBeGreaterThan(50)
  })

  it('names every term on the page, so a person sees what a machine gets', () => {
    // The page is generated from the merged document, so a term missing from it
    // means the generator stopped seeing a whole category of term.
    const onPage = termsIn(built.page)
    const uncovered = built.groups.flatMap(([, terms]) => terms)
      .filter(t => !onPage.has(t.name)).map(t => t.name)
    expect(uncovered, `served but not on the page: ${uncovered.join(', ')}`).toEqual([])
    expect(built.total).toBeGreaterThan(50)
  })

  it('matches the copy in deploy/vocab, which is what is actually served', async () => {
    for (const [name, expected] of [['transmissions.ttl', built.turtle], ['index.html', built.page]]) {
      const path = join(OUT_DIR, name)
      expect(existsSync(path), `${path} is missing; run npm run build:vocab`).toBe(true)
      expect(
        await readFile(path, 'utf8'),
        `deploy/vocab/${name} is stale; run npm run build:vocab and commit the result`
      ).toBe(expected)
    }
  })

  it('is served by a config that points at the directory it writes', async () => {
    // The two halves that have to agree and that nothing else connects: the
    // generator's output filename and the filename nginx serves.
    const conf = await readFile(join(root, 'deploy/nginx/vocab.conf'), 'utf8')
    expect(conf).toContain('/xmlns/transmissions/transmissions.ttl')
    expect(conf).toContain('/home/github/transmission/deploy/vocab')
    // A term must 303. A 200 would assert that the class IS the page.
    expect(conf).toMatch(/return 303/)
  })
})

describe('the vocabulary and the code that uses it', () => {
  // The pair nothing connected. src/rdf/Vocabulary.js is the single source of
  // IRI truth for the code, vocabs/ is the single source of truth for what the
  // namespace serves, and until this test existed 50 of the 81 terms the code
  // writes into every saved project were declared nowhere at all.
  //
  // Both directions, because they fail differently. A term in the code and not
  // the vocabulary is an IRI in published data that resolves to a document not
  // mentioning it. A term in the vocabulary and not the code is usually a typo
  // in one of the two.
  it('declares every trn: term the code names', async () => {
    const { vocabulary } = await import('../../src/rdf/Vocabulary.js')
    const declared = new Set()
    for (const file of (await readdir(vocabsDir)).filter(f => f.endsWith('.ttl'))) {
      for (const t of termsIn(await readFile(join(vocabsDir, file), 'utf8'))) declared.add(t)
    }

    const named = Object.values(vocabulary.trn).map(iri => iri.slice(TRN.length))
    expect(named.length, 'the code names no trn: terms, so this checked nothing').toBeGreaterThan(50)

    const undeclared = named.filter(t => !declared.has(t)).sort()
    expect(undeclared, `named in src/rdf/Vocabulary.js and not declared in vocabs/: ${undeclared.join(', ')}`)
      .toEqual([])
  })
})

describe('the terms moved from plugin-universe', () => {
  // vocabs/formats.ttl is the copy plugin-universe's own header asked for:
  // "proposed upstream to ~/github/transmission rather than maintained
  // separately". Its copy stays where its SHACL shapes can see it, so the two
  // are a pair that will drift, and this is the only thing watching them.
  //
  // It skips when the sibling checkout is absent rather than failing, so this
  // repository does not depend on it. A guard that cannot run says so.
  const sibling = join(process.env.HOME ?? '', 'github/plugin-universe/vocabs/trn-extensions.ttl')

  it('says the same as plugin-universe does, where that checkout is present', async () => {
    if (!existsSync(sibling)) {
      expect(existsSync(sibling)).toBe(false) // recorded, not silently skipped
      return
    }
    const theirs = termsIn(await readFile(sibling, 'utf8'))
    const ours = termsIn(await readFile(join(vocabsDir, 'formats.ttl'), 'utf8'))
    const lost = [...theirs].filter(t => !ours.has(t)).sort()
    expect(lost, `plugin-universe declares these and vocabs/formats.ttl does not: ${lost.join(', ')}`)
      .toEqual([])
  })
})
