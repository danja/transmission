#include "transmission/Vst3MidiEventConversion.h"

#include "pluginterfaces/vst/ivstmidicontrollers.h"

#include <algorithm>
#include <cmath>

namespace transmission {
namespace {

bool validDataByte(std::uint8_t value) noexcept {
    return value <= 127;
}

bool normalizedByte(float value, std::uint8_t& result) noexcept {
    if (!std::isfinite(value) || value < 0.0F || value > 1.0F)
        return false;
    result = static_cast<std::uint8_t>(
        std::clamp(static_cast<int>(std::lround(value * 127.0F)), 0, 127));
    return true;
}

bool validChannel(Steinberg::int16 channel) noexcept {
    return channel >= 0 && channel < 16;
}

Steinberg::int32 sampleOffset(const MidiEvent& midi,
                              std::size_t frames) noexcept {
    return static_cast<Steinberg::int32>(
        std::min<std::size_t>(midi.frameOffset, frames - 1));
}

} // namespace

bool midiToVst3Event(const MidiEvent& midi, std::size_t frames,
                     Steinberg::Vst::Event& event) noexcept {
    if (frames == 0 || midi.size != 3 || !validDataByte(midi.data[1]) ||
        !validDataByte(midi.data[2]))
        return false;
    const auto status = static_cast<std::uint8_t>(midi.data[0] & 0xf0);
    const auto channel = static_cast<Steinberg::int16>(midi.data[0] & 0x0f);
    event = {};
    event.busIndex = 0;
    event.sampleOffset = sampleOffset(midi, frames);
    event.flags = Steinberg::Vst::Event::kIsLive;
    if (status == 0x90 && midi.data[2] != 0) {
        event.type = Steinberg::Vst::Event::kNoteOnEvent;
        event.noteOn.channel = channel;
        event.noteOn.pitch = midi.data[1];
        event.noteOn.velocity = static_cast<float>(midi.data[2]) / 127.0F;
        event.noteOn.noteId = -1;
        return true;
    }
    if (status == 0x80 || status == 0x90) {
        event.type = Steinberg::Vst::Event::kNoteOffEvent;
        event.noteOff.channel = channel;
        event.noteOff.pitch = midi.data[1];
        event.noteOff.velocity = static_cast<float>(midi.data[2]) / 127.0F;
        event.noteOff.noteId = -1;
        return true;
    }
    if (status == 0xa0) {
        event.type = Steinberg::Vst::Event::kPolyPressureEvent;
        event.polyPressure.channel = channel;
        event.polyPressure.pitch = midi.data[1];
        event.polyPressure.pressure =
            static_cast<float>(midi.data[2]) / 127.0F;
        event.polyPressure.noteId = -1;
        return true;
    }
    return false;
}

bool midiToVst3ControllerChange(
    const MidiEvent& midi, std::size_t frames,
    Vst3MidiControllerChange& change) noexcept {
    if (frames == 0) return false;
    const auto status = static_cast<std::uint8_t>(midi.data[0] & 0xf0);
    const auto channel = static_cast<Steinberg::int16>(midi.data[0] & 0x0f);
    change = {};
    change.channel = channel;
    change.sampleOffset = sampleOffset(midi, frames);
    if (status == 0xb0 && midi.size == 3 &&
        validDataByte(midi.data[1]) && validDataByte(midi.data[2])) {
        change.controllerNumber = midi.data[1];
        change.normalizedValue =
            static_cast<double>(midi.data[2]) / 127.0;
        return true;
    }
    if (status == 0xd0 && midi.size == 2 && validDataByte(midi.data[1])) {
        change.controllerNumber = Steinberg::Vst::kAfterTouch;
        change.normalizedValue =
            static_cast<double>(midi.data[1]) / 127.0;
        return true;
    }
    if (status == 0xe0 && midi.size == 3 &&
        validDataByte(midi.data[1]) && validDataByte(midi.data[2])) {
        change.controllerNumber = Steinberg::Vst::kPitchBend;
        const auto value = static_cast<unsigned>(midi.data[1]) |
                           (static_cast<unsigned>(midi.data[2]) << 7U);
        change.normalizedValue = static_cast<double>(value) / 16384.0;
        return true;
    }
    return false;
}

bool vst3ToMidiEvent(const Steinberg::Vst::Event& event,
                     MidiEvent& midi) noexcept {
    if (event.sampleOffset < 0) return false;
    midi = {};
    midi.frameOffset = static_cast<std::size_t>(event.sampleOffset);
    if (event.type == Steinberg::Vst::Event::kNoteOnEvent) {
        std::uint8_t velocity = 0;
        if (!validChannel(event.noteOn.channel) ||
            event.noteOn.pitch < 0 || event.noteOn.pitch > 127 ||
            !normalizedByte(event.noteOn.velocity, velocity))
            return false;
        midi.size = 3;
        midi.data = {
            static_cast<std::uint8_t>(0x90 | event.noteOn.channel),
            static_cast<std::uint8_t>(event.noteOn.pitch), velocity};
        return true;
    }
    if (event.type == Steinberg::Vst::Event::kNoteOffEvent) {
        std::uint8_t velocity = 0;
        if (!validChannel(event.noteOff.channel) ||
            event.noteOff.pitch < 0 || event.noteOff.pitch > 127 ||
            !normalizedByte(event.noteOff.velocity, velocity))
            return false;
        midi.size = 3;
        midi.data = {
            static_cast<std::uint8_t>(0x80 | event.noteOff.channel),
            static_cast<std::uint8_t>(event.noteOff.pitch), velocity};
        return true;
    }
    if (event.type == Steinberg::Vst::Event::kPolyPressureEvent) {
        std::uint8_t pressure = 0;
        if (!validChannel(event.polyPressure.channel) ||
            event.polyPressure.pitch < 0 || event.polyPressure.pitch > 127 ||
            !normalizedByte(event.polyPressure.pressure, pressure))
            return false;
        midi.size = 3;
        midi.data = {
            static_cast<std::uint8_t>(0xa0 | event.polyPressure.channel),
            static_cast<std::uint8_t>(event.polyPressure.pitch), pressure};
        return true;
    }
    if (event.type != Steinberg::Vst::Event::kLegacyMIDICCOutEvent ||
        !validChannel(event.midiCCOut.channel) ||
        event.midiCCOut.value < 0 || event.midiCCOut.value > 127)
        return false;
    const auto channel = static_cast<std::uint8_t>(event.midiCCOut.channel);
    const auto value = static_cast<std::uint8_t>(event.midiCCOut.value);
    if (event.midiCCOut.controlNumber <= 127) {
        midi.size = 3;
        midi.data = {
            static_cast<std::uint8_t>(0xb0 | channel),
            event.midiCCOut.controlNumber, value};
        return true;
    }
    if (event.midiCCOut.controlNumber == Steinberg::Vst::kAfterTouch) {
        midi.size = 2;
        midi.data = {static_cast<std::uint8_t>(0xd0 | channel), value, 0};
        return true;
    }
    if (event.midiCCOut.controlNumber == Steinberg::Vst::kPitchBend) {
        if (event.midiCCOut.value2 < 0 ||
            event.midiCCOut.value2 > 127)
            return false;
        midi.size = 3;
        midi.data = {
            static_cast<std::uint8_t>(0xe0 | channel), value,
            static_cast<std::uint8_t>(event.midiCCOut.value2)};
        return true;
    }
    if (event.midiCCOut.controlNumber ==
        Steinberg::Vst::kCtrlProgramChange) {
        midi.size = 2;
        midi.data = {static_cast<std::uint8_t>(0xc0 | channel), value, 0};
        return true;
    }
    if (event.midiCCOut.controlNumber ==
        Steinberg::Vst::kCtrlPolyPressure) {
        if (event.midiCCOut.value2 < 0 ||
            event.midiCCOut.value2 > 127)
            return false;
        midi.size = 3;
        midi.data = {
            static_cast<std::uint8_t>(0xa0 | channel), value,
            static_cast<std::uint8_t>(event.midiCCOut.value2)};
        return true;
    }
    return false;
}

std::size_t convertVst3OutputEvents(
    const Steinberg::Vst::Event* input, std::size_t inputCount,
    MidiEvent* output, std::size_t capacity) noexcept {
    if (!input || !output || capacity == 0) return 0;
    std::size_t count = 0;
    for (std::size_t index = 0;
         index < inputCount && count < capacity; ++index) {
        MidiEvent midi;
        if (vst3ToMidiEvent(input[index], midi))
            output[count++] = midi;
    }
    return count;
}

} // namespace transmission
