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
  metadata?: Record<string, unknown>
}

export interface GraphConnection {
  from: string
  to: string
  kind: 'audio' | 'midi'
}

export interface GraphDefinition {
  id: string
  label?: string
  nodes: GraphNode[]
  connections: GraphConnection[]
  metadata?: Record<string, unknown>
}

export interface CompiledGraph extends GraphDefinition {
  version: 1
  executionOrder: string[]
}

export interface NativeBridgeApi {
  createEngine(options?: Record<string, unknown>): unknown
  scanPlugins(paths: string[]): unknown
  loadProject(graph: CompiledGraph): unknown
  startAudio(): unknown
  stopAudio(): unknown
  setParameter(nodeId: string, parameterId: string, value: number, sampleOffset?: number): unknown
  sendMidi(nodeId: string, event: Record<string, unknown>): unknown
  getDiagnostics(): unknown
  dispose(): void
}
