export interface GenOmegaNote {
  pitch: number
  start: number
  length: number
  velocity: number
  channel: number
}

export interface GenOmegaClip {
  name: string
  section: string
  start_position: number
  length: number
  notes: GenOmegaNote[]
}

export interface GenOmegaArrangement {
  title: string
  tempo: number
  timeSignature: [number, number]
  bars: number
  length: number
  tonalCenter: string
  sections: Array<{
    name: string
    startBar: number
    bars: number
    start: number
    length: number
    intent: string
  }>
  tracks: Array<{
    name: string
    role: string
    instrument: string
    source: string
    fx: string[]
    clips: GenOmegaClip[]
  }>
  productionCues: Array<{
    bar: number
    track: string
    parameter: string
    value: number
    intent: string
  }>
  limitations: string[]
}

export function createGenOmegaArrangement(options?: { tempo?: number }): GenOmegaArrangement
