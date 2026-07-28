// src/registry/PluginRegistry.js

export class PluginRegistry {
  #entries = new Map()

  register(descriptor, factory = null) {
    validateDescriptor(descriptor)
    if (this.#entries.has(descriptor.id)) throw new Error(`Plugin already registered: ${descriptor.id}`)
    this.#entries.set(descriptor.id, Object.freeze({ descriptor: Object.freeze({ ...descriptor }), factory }))
  }

  upsert(descriptor, factory = null) {
    validateDescriptor(descriptor)
    this.#entries.set(descriptor.id, Object.freeze({ descriptor: Object.freeze({ ...descriptor }), factory }))
  }

  get(id) {
    return this.#entries.get(id)
  }

  list() {
    return [...this.#entries.values()].map(entry => entry.descriptor)
  }

  create(id, options = {}) {
    const entry = this.#entries.get(id)
    if (!entry) throw new Error(`Unknown plugin: ${id}`)
    if (typeof entry.factory !== 'function') throw new Error(`Plugin has no factory: ${id}`)
    return entry.factory(options)
  }
}

function validateDescriptor(descriptor) {
  if (!descriptor?.id || typeof descriptor.id !== 'string') throw new TypeError('Plugin descriptor id is required')
  if (!descriptor.name || typeof descriptor.name !== 'string') throw new TypeError(`Plugin name is required: ${descriptor.id}`)
  if (!Number.isInteger(descriptor.audioInputs) || descriptor.audioInputs < 0) throw new TypeError(`Invalid audioInputs: ${descriptor.id}`)
  if (!Number.isInteger(descriptor.audioOutputs) || descriptor.audioOutputs < 0) throw new TypeError(`Invalid audioOutputs: ${descriptor.id}`)
}
