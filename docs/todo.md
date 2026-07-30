# TODO

- Support non-note VST3 channel messages in `Vst3Processor` input and output
  conversion, especially `kLegacyMIDICCOutEvent`, so Drift modulation and the
  CC portions of Conductor and Oracle can traverse Transmission MIDI edges.
  Cover valid CC values, channel preservation, sample offsets, malformed
  events, and bounded output capacity.
