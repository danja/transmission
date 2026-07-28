// src/rdf/Vocabulary.js

const RDF = 'http://www.w3.org/1999/02/22-rdf-syntax-ns#'
const RDFS = 'http://www.w3.org/2000/01/rdf-schema#'
const TRN = 'http://purl.org/stuff/transmissions/'

export const vocabulary = Object.freeze({
  rdf: Object.freeze({ type: `${RDF}type`, first: `${RDF}first`, rest: `${RDF}rest`, nil: `${RDF}nil` }),
  rdfs: Object.freeze({ label: `${RDFS}label`, comment: `${RDFS}comment` }),
  trn: Object.freeze({
    transmission: `${TRN}Transmission`, entryTransmission: `${TRN}EntryTransmission`, pipe: `${TRN}pipe`, settings: `${TRN}settings`,
    audioInputs: `${TRN}audioInputs`, audioOutputs: `${TRN}audioOutputs`, midiInputs: `${TRN}midiInputs`, midiOutputs: `${TRN}midiOutputs`,
    tempoMap: `${TRN}tempoMap`, atBeat: `${TRN}atBeat`, bpm: `${TRN}bpm`, loopStart: `${TRN}loopStart`, loopEnd: `${TRN}loopEnd`, loopEnabled: `${TRN}loopEnabled`
  })
})
