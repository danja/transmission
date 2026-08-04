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
    connections: `${TRN}connections`, from: `${TRN}from`, to: `${TRN}to`, kind: `${TRN}kind`,
    fromPort: `${TRN}fromPort`, toPort: `${TRN}toPort`, editorX: `${TRN}editorX`, editorY: `${TRN}editorY`,
    parameters: `${TRN}parameters`, parameterId: `${TRN}parameterId`, normalizedValue: `${TRN}normalizedValue`,
    componentState: `${TRN}componentState`, controllerState: `${TRN}controllerState`,
    systemInputConnections: `${TRN}systemInputConnections`, systemOutputConnections: `${TRN}systemOutputConnections`,
    tempoMap: `${TRN}tempoMap`, atBeat: `${TRN}atBeat`, bpm: `${TRN}bpm`, loopStart: `${TRN}loopStart`, loopEnd: `${TRN}loopEnd`, loopEnabled: `${TRN}loopEnabled`,
    arrangementLength: `${TRN}arrangementLength`, midiClips: `${TRN}midiClips`, gainLanes: `${TRN}gainLanes`, midiMappings: `${TRN}midiMappings`,
    clipId: `${TRN}clipId`, targetNode: `${TRN}targetNode`, startBeat: `${TRN}startBeat`, lengthBeats: `${TRN}lengthBeats`, notes: `${TRN}notes`,
    durationBeats: `${TRN}durationBeats`, pitch: `${TRN}pitch`, velocity: `${TRN}velocity`, channel: `${TRN}channel`, controller: `${TRN}controller`, consume: `${TRN}consume`,
    points: `${TRN}points`, valueDb: `${TRN}valueDb`, shape: `${TRN}shape`,
    pluginProfile: `${TRN}PluginProfile`, discoveredPlugin: `${TRN}DiscoveredPlugin`,
    bundleName: `${TRN}bundleName`, vstClassId: `${TRN}vstClassId`, vendor: `${TRN}vendor`,
    role: `${TRN}role`, produces: `${TRN}produces`, accepts: `${TRN}accepts`, requires: `${TRN}requires`,
    recommendedBefore: `${TRN}recommendedBefore`, recommendedAfter: `${TRN}recommendedAfter`,
    companion: `${TRN}companion`, genre: `${TRN}genre`, caution: `${TRN}caution`, homepage: `${TRN}homepage`,
    modulePath: `${TRN}modulePath`, category: `${TRN}category`, discoverySource: `${TRN}discoverySource`,
    parameter: `${TRN}parameter`, parameterTitle: `${TRN}parameterTitle`, shortTitle: `${TRN}shortTitle`,
    units: `${TRN}units`, defaultNormalized: `${TRN}defaultNormalized`,
    stepCount: `${TRN}stepCount`, parameterFlags: `${TRN}parameterFlags`,
    audioSettings: `${TRN}audioSettings`, renderAheadMs: `${TRN}renderAheadMs`,
    requestedBufferSize: `${TRN}requestedBufferSize`, processingThreads: `${TRN}processingThreads`
  })
})
