// src/types/public.d.ts

export interface GraphNode {
  id: string
  type: string
  label?: string
  ports?: {
    audioInputs?: number
    audioOutputs?: number
    midiInputs?: number
    midiOutputs?: number
  }
  settings?: Record<string, unknown>
  parameters?: Array<{ id: number, normalizedValue: number }>
  state?: { component?: string, controller?: string }
  metadata?: Record<string, unknown>
}

export interface GraphConnection {
  from: string
  to: string
  kind: 'audio' | 'midi'
  fromPort?: number
  toPort?: number
}

export interface GraphDefinition {
  id: string
  label?: string
  nodes: GraphNode[]
  connections: GraphConnection[]
  metadata?: Record<string, unknown>
}

export interface MidiNote {
  startBeat: number
  durationBeats: number
  pitch: number
  velocity: number
  channel: number
}

export interface MidiClip {
  id: string
  targetNodeId: string
  startBeat: number
  lengthBeats: number
  notes: MidiNote[]
}

export interface GainLane {
  targetNodeId: string
  points: Array<{ beat: number, valueDb: number, shape: 'step' | 'linear' }>
}

export interface ArrangementDefinition {
  lengthBeats: number
  midiClips: MidiClip[]
  gainLanes: GainLane[]
}

export interface CompiledGraph extends GraphDefinition {
  version: 1
  executionOrder: string[]
}

export interface NativeBridgeApi {
  createEngine(options?: Record<string, unknown>): unknown
  scanPlugins(paths: string[]): unknown
  loadProject(graph: CompiledGraph): unknown
  configureTransport(state: Record<string, unknown>): unknown
  startAudio(): unknown
  stopAudio(): unknown
  setParameter(nodeId: string, parameterId: string, value: number, sampleOffset?: number): unknown
  sendMidi(nodeId: string, event: Record<string, unknown>): unknown
  getDiagnostics(): unknown
  dispose(): void
}

export type EngineSessionState = 'idle' | 'loaded' | 'running' | 'disposed'

export interface EngineSessionOptions {
  device?: 'jack'
  channels?: number
  blockSize?: number
  sampleRate?: number
  autoConnect?: boolean
}

export type GraphOperation =
  | { type: 'addNode', node: GraphNode }
  | { type: 'updateNode', nodeId: string, changes: Partial<Omit<GraphNode, 'id'>> }
  | { type: 'removeNode', nodeId: string }
  | { type: 'addConnection', connection: GraphConnection }
  | { type: 'removeConnection', connection: GraphConnection }
  | { type: 'setProjectMetadata', metadata: Record<string, unknown> }

export interface TransmissionStatus {
  projectOpen: boolean
  projectId: string | null
  filePath: string | null
  revision: number
  dirty: boolean
  engineAvailable: boolean
  engineState: EngineSessionState | 'unavailable'
  transport: Record<string, unknown>
}

export interface TransmissionControlApi {
  status(): TransmissionStatus
  describeProject(): TransmissionStatus & { graph: GraphDefinition, arrangement: ArrangementDefinition, executionOrder: string[] }
  projectTurtle(): string
  newProject(definition: GraphDefinition & { arrangement?: ArrangementDefinition }): unknown
  openProject(filePath: string): Promise<unknown>
  saveProject(filePath?: string | null): Promise<unknown>
  applyGraphChanges(input: { expectedRevision: number, operations: GraphOperation[], dryRun?: boolean }): unknown
  configureTransport(input: Record<string, unknown> & { expectedRevision: number }): unknown
  startTransport(): TransmissionStatus
  stopTransport(): TransmissionStatus
  setParameter(input: { expectedRevision: number, nodeId: string, parameterId: number, value: number, sampleOffset?: number }): unknown
  diagnostics(): unknown
  plugins(options?: { installedOnly?: boolean }): unknown
  searchPlugins(query?: Record<string, unknown>): unknown
  describePlugin(identifier: string): unknown
  validatePluginChain(identifiers: string[]): unknown
  scanPlugins(): Promise<unknown>
  pluginProfilesTurtle(): string
  discoveredPluginsTurtle(): string
  dispose(): void
}

export interface PluginParameterDescriptor {
  id: number
  title: string
  shortTitle?: string
  units?: string
  defaultNormalized: number
  stepCount: number
  flags: number
}

export interface PluginCatalogueEntry {
  id: string
  profileId: string | null
  classId: string
  name: string
  vendor: string
  bundleName: string
  modulePath: string | null
  installed: boolean
  audioInputs: number | null
  audioOutputs: number | null
  midiInputs: number | null
  midiOutputs: number | null
  roles: string[]
  produces: string[]
  accepts: string[]
  requirements: string[]
  genres: string[]
  cautions: string[]
  parameters?: PluginParameterDescriptor[]
}
