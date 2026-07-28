// src/bridge/NativeBridge.js

/**
 * Control-rate boundary for the native engine. Audio buffers never cross this API.
 * The native implementation will be supplied by the N-API addon in a later slice.
 */
export class NativeBridge {
  constructor(nativeModule = null) {
    this.nativeModule = nativeModule
    this.disposed = false
  }

  #call(method, ...args) {
    if (this.disposed) throw new Error('Native bridge has been disposed')
    if (!this.nativeModule || typeof this.nativeModule[method] !== 'function') {
      throw new Error(`Native engine capability is unavailable: ${method}`)
    }
    return this.nativeModule[method](...args)
  }

  createEngine(options = {}) { return this.#call('createEngine', options) }
  scanPlugins(paths) { return this.#call('scanPlugins', paths) }
  loadProject(graph) { return this.#call('loadProject', graph) }
  startAudio() { return this.#call('startAudio') }
  stopAudio() { return this.#call('stopAudio') }
  setParameter(nodeId, parameterId, value, sampleOffset = 0) {
    return this.#call('setParameter', nodeId, parameterId, value, sampleOffset)
  }
  sendMidi(nodeId, event) { return this.#call('sendMidi', nodeId, event) }
  getDiagnostics() { return this.#call('getDiagnostics') }
  savePluginState(nodeId) { return this.#call('savePluginState', nodeId) }
  restorePluginState(nodeId, state) { return this.#call('restorePluginState', nodeId, state) }
  dispose() {
    if (!this.disposed && this.nativeModule?.disposeEngine) this.nativeModule.disposeEngine()
    this.disposed = true
  }
}
