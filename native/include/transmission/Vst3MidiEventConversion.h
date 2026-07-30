#pragma once

#include "AudioDevice.h"

#include "pluginterfaces/vst/ivstevents.h"

#include <cstddef>

namespace transmission {

struct Vst3MidiControllerChange {
    Steinberg::int16 channel = 0;
    Steinberg::int16 controllerNumber = 0;
    Steinberg::int32 sampleOffset = 0;
    Steinberg::Vst::ParamValue normalizedValue = 0.0;
};

bool midiToVst3Event(const MidiEvent& midi, std::size_t frames,
                     Steinberg::Vst::Event& event) noexcept;
bool midiToVst3ControllerChange(
    const MidiEvent& midi, std::size_t frames,
    Vst3MidiControllerChange& change) noexcept;
bool vst3ToMidiEvent(const Steinberg::Vst::Event& event,
                     MidiEvent& midi) noexcept;
std::size_t convertVst3OutputEvents(
    const Steinberg::Vst::Event* input, std::size_t inputCount,
    MidiEvent* output, std::size_t capacity) noexcept;

} // namespace transmission
