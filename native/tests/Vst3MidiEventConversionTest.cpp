#include "transmission/Vst3MidiEventConversion.h"

#include "pluginterfaces/vst/ivstmidicontrollers.h"

#include <array>
#include <cassert>
#include <cmath>
#include <limits>

namespace {

Steinberg::Vst::Event legacyCc(
    Steinberg::int8 channel, std::uint8_t controller,
    Steinberg::int8 value, Steinberg::int32 offset) {
    Steinberg::Vst::Event event{};
    event.type = Steinberg::Vst::Event::kLegacyMIDICCOutEvent;
    event.sampleOffset = offset;
    event.midiCCOut.channel = channel;
    event.midiCCOut.controlNumber = controller;
    event.midiCCOut.value = value;
    return event;
}

void testInputControllerChanges() {
    transmission::Vst3MidiControllerChange change;
    transmission::MidiEvent cc;
    cc.frameOffset = 73;
    cc.size = 3;
    cc.data = {0xb5, 74, 96};
    assert(transmission::midiToVst3ControllerChange(cc, 64, change));
    assert(change.channel == 5);
    assert(change.controllerNumber == 74);
    assert(change.sampleOffset == 63);
    assert(std::abs(change.normalizedValue - 96.0 / 127.0) < 1e-12);

    transmission::MidiEvent pressure;
    pressure.frameOffset = 11;
    pressure.size = 2;
    pressure.data = {0xd9, 65, 0};
    assert(transmission::midiToVst3ControllerChange(
        pressure, 64, change));
    assert(change.channel == 9);
    assert(change.controllerNumber == Steinberg::Vst::kAfterTouch);
    assert(change.sampleOffset == 11);

    transmission::MidiEvent bend;
    bend.frameOffset = 17;
    bend.size = 3;
    bend.data = {0xe2, 0, 64};
    assert(transmission::midiToVst3ControllerChange(bend, 64, change));
    assert(change.channel == 2);
    assert(change.controllerNumber == Steinberg::Vst::kPitchBend);
    assert(change.normalizedValue == 0.5);
}

void testInputEventsAndMalformedMessages() {
    Steinberg::Vst::Event event{};
    transmission::MidiEvent pressure;
    pressure.frameOffset = 9;
    pressure.size = 3;
    pressure.data = {0xa3, 61, 88};
    assert(transmission::midiToVst3Event(pressure, 32, event));
    assert(event.type == Steinberg::Vst::Event::kPolyPressureEvent);
    assert(event.polyPressure.channel == 3);
    assert(event.polyPressure.pitch == 61);
    assert(event.sampleOffset == 9);

    transmission::MidiEvent shortCc;
    shortCc.size = 2;
    shortCc.data = {0xb0, 1, 0};
    transmission::Vst3MidiControllerChange change;
    assert(!transmission::midiToVst3ControllerChange(
        shortCc, 32, change));
    shortCc.size = 3;
    shortCc.data[1] = 128;
    assert(!transmission::midiToVst3ControllerChange(
        shortCc, 32, change));
    assert(!transmission::midiToVst3ControllerChange(
        shortCc, 0, change));
}

void testLegacyCcOutput() {
    const auto event = legacyCc(12, 23, 101, 37);
    transmission::MidiEvent midi;
    assert(transmission::vst3ToMidiEvent(event, midi));
    assert(midi.frameOffset == 37);
    assert(midi.size == 3);
    assert((midi.data == std::array<std::uint8_t, 3>{0xbc, 23, 101}));

    auto bend = legacyCc(
        7, Steinberg::Vst::kPitchBend, 12, 19);
    bend.midiCCOut.value2 = 91;
    assert(transmission::vst3ToMidiEvent(bend, midi));
    assert(midi.size == 3);
    assert((midi.data == std::array<std::uint8_t, 3>{0xe7, 12, 91}));

    auto program = legacyCc(
        4, Steinberg::Vst::kCtrlProgramChange, 42, 6);
    assert(transmission::vst3ToMidiEvent(program, midi));
    assert(midi.size == 2);
    assert((midi.data == std::array<std::uint8_t, 3>{0xc4, 42, 0}));
}

void testMalformedOutputAndCapacity() {
    std::array<Steinberg::Vst::Event, 5> input{
        legacyCc(-1, 1, 2, 0),
        legacyCc(1, 10, 20, 4),
        legacyCc(2, 11, 21, 5),
        legacyCc(3, 12, 22, 6),
        legacyCc(4, 13, 23, 7)};
    input[0].sampleOffset = -1;
    std::array<transmission::MidiEvent, 2> output{};
    const auto count = transmission::convertVst3OutputEvents(
        input.data(), input.size(), output.data(), output.size());
    assert(count == 2);
    assert(output[0].data[0] == 0xb1);
    assert(output[0].frameOffset == 4);
    assert(output[1].data[0] == 0xb2);

    auto invalidValue = legacyCc(0, 1, -1, 0);
    transmission::MidiEvent midi;
    assert(!transmission::vst3ToMidiEvent(invalidValue, midi));

    Steinberg::Vst::Event invalidVelocity{};
    invalidVelocity.type = Steinberg::Vst::Event::kNoteOnEvent;
    invalidVelocity.noteOn.channel = 0;
    invalidVelocity.noteOn.pitch = 60;
    invalidVelocity.noteOn.velocity =
        std::numeric_limits<float>::quiet_NaN();
    assert(!transmission::vst3ToMidiEvent(invalidVelocity, midi));
}

} // namespace

int main() {
    testInputControllerChanges();
    testInputEventsAndMalformedMessages();
    testLegacyCcOutput();
    testMalformedOutputAndCapacity();
    return 0;
}
