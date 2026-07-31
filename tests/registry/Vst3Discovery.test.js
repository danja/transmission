import { chmod, mkdir, mkdtemp, rm, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { describe, expect, it } from 'vitest'
import { parseInspectorOutput, Vst3Discovery } from '../../src/registry/Vst3Discovery.js'

describe('Vst3Discovery inspector output', () => {
  it('keeps the processor class, topology, and parameter metadata', () => {
    const plugins = parseInspectorOutput(`
id=processor-id
name=Example
vendor=Vendor
category=Audio Module Class
module=/plugins/example.vst3

id=controller-id
name=Example
vendor=Vendor
category=Component Controller Class
module=/plugins/example.vst3

audioInputs=2
audioOutputs=2
midiInputs=1
midiOutputs=0
parameterCount=1
parameter.0.id=42
parameter.0.title=Colour
parameter.0.shortTitle=Clr
parameter.0.units=%
parameter.0.defaultNormalized=0.5
parameter.0.stepCount=0
parameter.0.flags=1
`, '/plugins/example.vst3')
    expect(plugins).toEqual([expect.objectContaining({
      classId: 'processor-id',
      bundleName: 'example.vst3',
      roles: ['AudioEffect', 'Instrument'],
      produces: ['Audio'],
      accepts: ['Midi', 'Audio'],
      parameters: [{
        id: 42,
        title: 'Colour',
        shortTitle: 'Clr',
        units: '%',
        defaultNormalized: 0.5,
        stepCount: 0,
        flags: 1
      }]
    })])
  })

  it('rejects malformed descriptors so the scan can isolate the plugin', () => {
    expect(() => parseInspectorOutput(`
id=
name=Broken
category=Audio Module Class
`, '/plugins/broken.vst3')).toThrow('non-empty id and name')

    expect(() => parseInspectorOutput(`
id=processor-id
category=Audio Module Class
`, '/plugins/broken.vst3')).toThrow('non-empty id and name')
  })

  it('reports malformed inspector output as a per-plugin scan failure', async () => {
    const directory = await mkdtemp(join(tmpdir(), 'transmission-discovery-'))
    try {
      const pluginRoot = join(directory, 'plugins')
      const bundle = join(pluginRoot, 'broken.vst3')
      const inspector = join(directory, 'inspector')
      await mkdir(bundle, { recursive: true })
      await writeFile(inspector, '#!/bin/sh\nprintf "id=\\nname=Broken\\n"\n')
      await chmod(inspector, 0o755)

      const discovery = new Vst3Discovery({
        inspectorPath: inspector,
        concurrency: 1
      })
      const result = await discovery.scan([pluginRoot])

      expect(result.plugins).toEqual([])
      expect(result.failures).toEqual([{
        modulePath: bundle,
        error: 'inspector output requires non-empty id and name fields'
      }])
    } finally {
      await rm(directory, { recursive: true, force: true })
    }
  })
})
