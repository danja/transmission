// src/session/EngineSession.js

import { ProjectSession } from './ProjectSession.js'

export class EngineSession {
  constructor({ bridge, project = new ProjectSession(), engineOptions = {} } = {}) {
    if (!bridge) throw new TypeError('EngineSession requires a native bridge')
    this.bridge = bridge
    this.project = project
    this.engineOptions = { ...engineOptions }
    this.state = 'idle'
    this.engineCreated = false
  }

  open(definition, filePath = null) {
    if (this.state === 'running') throw new Error('Stop audio before opening a project')
    const compiled = this.project.open(definition, filePath)
    this.#ensureEngine()
    this.bridge.loadProject(compiled)
    this.#configureTransport()
    this.state = 'loaded'
    return compiled
  }

  update(mutator) {
    if (this.state === 'running') throw new Error('Stop audio before editing the runtime graph')
    const compiled = this.project.update(mutator)
    this.#ensureEngine()
    this.bridge.loadProject(compiled)
    this.#configureTransport()
    this.state = 'loaded'
    return compiled
  }

  start() {
    if (this.state !== 'loaded') throw new Error('A compiled project must be loaded before starting audio')
    this.bridge.startAudio()
    this.project.transport.start()
    this.state = 'running'
  }

  stop() {
    if (this.state !== 'running') return
    this.bridge.stopAudio()
    this.project.transport.stop()
    this.state = 'loaded'
  }

  setTempo(bpm, atBeat = this.project.transport.positionBeats) {
    if (this.state === 'running') throw new Error('Stop audio before changing the tempo map')
    this.project.transport.setTempo(bpm, atBeat)
    this.#configureTransport()
  }

  setLoop(startBeat, endBeat, enabled = true) {
    if (this.state === 'running') throw new Error('Stop audio before changing the loop')
    this.project.transport.setLoop(startBeat, endBeat, enabled)
    this.#configureTransport()
  }

  clearLoop() {
    if (this.state === 'running') throw new Error('Stop audio before changing the loop')
    this.project.transport.clearLoop()
    this.#configureTransport()
  }

  seek(beat) {
    if (this.state === 'running') throw new Error('Stop audio before seeking')
    this.project.transport.seek(beat)
    this.#configureTransport()
  }

  setParameter(nodeId, parameterId, value, sampleOffset = 0) {
    if (!['loaded', 'running'].includes(this.state)) throw new Error('A compiled project must be loaded before setting a parameter')
    return this.bridge.setParameter(nodeId, parameterId, value, sampleOffset)
  }

  diagnostics() {
    if (!this.engineCreated) return null
    return this.bridge.getDiagnostics()
  }

  synchronizeTransport() {
    if (!this.engineCreated) throw new Error('A native engine must be created before configuring transport')
    this.#configureTransport()
  }

  #configureTransport() {
    if (typeof this.bridge.configureTransport === 'function') {
      this.bridge.configureTransport(this.project.transport.toJSON())
    }
  }

  #ensureEngine() {
    if (!this.engineCreated && typeof this.bridge.createEngine === 'function') {
      this.bridge.createEngine(this.engineOptions)
      this.engineCreated = true
    }
  }

  dispose() {
    this.stop()
    this.bridge.dispose()
    this.engineCreated = false
    this.state = 'disposed'
  }
}
