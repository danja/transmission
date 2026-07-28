// tests/rdf/TransmissionRdf.test.js

import { describe, expect, it } from 'vitest'
import { graphFromDataset, parseTurtle } from '../../src/rdf/TransmissionRdf.js'

describe('Transmission RDF adapter', () => {
  it('parses an RDF list into a typed graph', async () => {
    const dataset = await parseTurtle(`
      @prefix : <http://purl.org/stuff/transmissions/> .
      @prefix rdf: <http://www.w3.org/1999/02/22-rdf-syntax-ns#> .
      :main a :Transmission ; :pipe ( :input :output ) .
      :input a :AudioInput .
      :output a :AudioOutput .
    `)
    const graph = graphFromDataset(dataset, 'http://purl.org/stuff/transmissions/main')
    expect(graph.nodes.size).toBe(2)
    expect(graph.connections[0]).toMatchObject({ from: expect.stringContaining('input'), kind: 'audio' })
  })
})
