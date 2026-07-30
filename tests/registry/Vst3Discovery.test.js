import { describe, expect, it } from 'vitest'
import { parseInspectorOutput } from '../../src/registry/Vst3Discovery.js'

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
})
