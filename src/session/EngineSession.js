// src/session/EngineSession.js

import { ProjectSession } from './ProjectSession.js'

export class EngineSession {
  constructor({ bridge, project = new ProjectSession() } = {}) {
    if (!bridge) throw new TypeError('EngineSession requires a native bridge')
    this.bridge = bridge
    this.project = project
    this.state = 'idle'
  }

  open(definition, filePath = null) {
    if (this.state === 'running') throw new Error('Stop audio before opening a project')
    const compiled = this.project.open(definition, filePath)
    this.bridge.loadProject(compiled)
    this.state = 'loaded'
    return compiled
  }

  update(mutator) {
    if (this.state === 'running') throw new Error('Stop audio before editing the runtime graph')
    const compiled = this.project.update(mutator)
    this.bridge.loadProject(compiled)
    this.state = 'loaded'
    return compiled
  }

  start() {
    if (this.state !== 'loaded') throw new Error('A compiled project must be loaded before starting audio')
    this.bridge.startAudio()
    this.state = 'running'
  }

  stop() {
    if (this.state !== 'running') return
    this.bridge.stopAudio()
    this.state = 'loaded'
  }

  dispose() {
    this.stop()
    this.bridge.dispose()
    this.state = 'disposed'
  }
}
