// tests/registry/PluginRegistryValidation.test.js

import { describe, expect, it } from 'vitest'
import { PluginRegistry } from '../../src/registry/PluginRegistry.js'

describe('PluginRegistry validation', () => {
  it('rejects descriptors with invalid bus counts', () => {
    expect(() => new PluginRegistry().register({
      id: 'bad', name: 'Bad', audioInputs: -1, audioOutputs: 1
    })).toThrow('Invalid audioInputs')
  })
})
