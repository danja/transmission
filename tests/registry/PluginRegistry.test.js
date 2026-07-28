// tests/registry/PluginRegistry.test.js

import { describe, expect, it } from 'vitest'
import { PluginRegistry } from '../../src/registry/PluginRegistry.js'

const descriptor = { id: 'vendor:test', name: 'Test Plugin', audioInputs: 1, audioOutputs: 1 }

describe('PluginRegistry', () => {
  it('registers descriptors and creates instances through factories', () => {
    const registry = new PluginRegistry()
    registry.register(descriptor, options => ({ options }))
    expect(registry.list()).toEqual([descriptor])
    expect(registry.create(descriptor.id, { sampleRate: 48000 })).toEqual({ options: { sampleRate: 48000 } })
  })

  it('rejects duplicate identifiers', () => {
    const registry = new PluginRegistry()
    registry.register(descriptor)
    expect(() => registry.register(descriptor)).toThrow('already registered')
  })
})
