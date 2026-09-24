// native/tests/JigdawProcessorTest.cpp
//
// Offline and deterministic: the plugins are read from the jigdaw checkout over
// file:// and rendered without a device. Nothing here needs JACK, a display or
// the network, which is what makes it the first check to run after a change.

#include "transmission/JigdawProcessor.h"

#include <array>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#ifndef TRANSMISSION_JIGDAW_PLUGIN_ROOT
#define TRANSMISSION_JIGDAW_PLUGIN_ROOT ""
#endif

namespace {

constexpr int skipExitCode = 77;

std::string iriFor(const std::string& name) {
    return "file://" + std::string(TRANSMISSION_JIGDAW_PLUGIN_ROOT) + "/" + name + "/";
}

struct Render {
    double rms = 0.0;
    double peak = 0.0;
    std::size_t midiOut = 0;
    std::vector<transmission::MidiEvent> events;
};

/// Render `blocks` blocks and report what came out. `events` is delivered on
/// the first block only, which is how a held note is expressed.
Render render(transmission::JigdawProcessor& processor, std::size_t channels,
              std::size_t frames, std::size_t blocks, double tempo, bool playing,
              const std::vector<transmission::MidiEvent>& events) {
    std::vector<std::vector<float>> inputStorage(channels, std::vector<float>(frames, 0.0F));
    std::vector<std::vector<float>> outputStorage(channels, std::vector<float>(frames, 0.0F));
    std::vector<const float*> inputs(channels);
    std::vector<float*> outputs(channels);
    for (std::size_t channel = 0; channel < channels; ++channel) {
        inputs[channel] = inputStorage[channel].data();
        outputs[channel] = outputStorage[channel].data();
    }
    std::array<transmission::MidiEvent, transmission::maxMidiEventsPerBlock> taken{};
    const double beatsPerBlock = tempo / 60.0 * static_cast<double>(frames) / 48000.0;

    Render result;
    double sum = 0.0;
    std::size_t samples = 0;
    for (std::size_t block = 0; block < blocks; ++block) {
        transmission::AudioProcessContext context;
        context.playing = playing;
        context.tempo = tempo;
        context.projectTimeMusic = beatsPerBlock * static_cast<double>(block);
        processor.setProcessContext(context);
        processor.applyPendingParameters();
        processor.processWithMidi(
            inputs.data(), channels, outputs.data(), channels, frames,
            events.empty() ? nullptr : events.data(), block == 0 ? events.size() : 0);
        const auto count = processor.takeOutputMidi(taken.data(), taken.size());
        for (std::size_t index = 0; index < count; ++index)
            result.events.push_back(taken[index]);
        result.midiOut += count;
        for (std::size_t channel = 0; channel < channels; ++channel)
            for (std::size_t frame = 0; frame < frames; ++frame) {
                const double value = outputStorage[channel][frame];
                sum += value * value;
                result.peak = std::max(result.peak, std::abs(value));
                ++samples;
            }
    }
    result.rms = samples > 0 ? std::sqrt(sum / static_cast<double>(samples)) : 0.0;
    return result;
}

transmission::MidiEvent noteOn(std::size_t frame, std::uint8_t pitch,
                               std::uint8_t velocity) {
    transmission::MidiEvent event;
    event.frameOffset = frame;
    event.size = 3;
    event.data = {0x90, pitch, velocity};
    return event;
}

} // namespace

int main() {
    const std::string root = TRANSMISSION_JIGDAW_PLUGIN_ROOT;
    if (root.empty() || !std::filesystem::exists(root + "/pulse/profile.ttl") ||
        !std::filesystem::exists(root + "/bassgen/profile.ttl")) {
        std::cout << "Skipping: no JigDAW worked plugins under " << root << "\n";
        return skipExitCode;
    }

    // An IRI that resolves to nothing is refused with a message, not a crash.
    {
        transmission::JigdawProcessor processor;
        std::string error;
        assert(!processor.initialize("file:///nonexistent/plugin/", 256, 48000.0, error));
        assert(!error.empty());
        assert(!processor.ready());
    }

    // Pulse: jig:Abi1, an instrument with no audio input and a stereo output.
    {
        transmission::JigdawProcessor processor;
        std::string error;
        assert(processor.initialize(iriFor("pulse"), 256, 48000.0, error));
        assert(error.empty());
        assert(processor.ready());
        const auto& topology = processor.topology();
        assert(topology.name == "Pulse");
        assert(topology.audioInputs == 0);
        assert(topology.audioOutputs == 2);
        assert(topology.midiInputs == 1);
        assert(topology.midiOutputs == 0);
        assert(topology.latencyFrames == 0);
        assert(topology.parameters.size() == 5);
        assert(topology.parameters.front().id == 0);
        assert(topology.parameters.front().symbol == "waveform");
        assert(topology.parameters.front().enumeration);

        // Silence with no note, sound with one. The block is deliberately
        // larger than the module's 128 frame maximum: a host that processes
        // only the first sub-block leaves the rest of every buffer stale, which
        // is what this measures.
        const auto quiet = render(processor, 2, 512, 8, 120.0, true, {});
        assert(quiet.rms == 0.0);

        const auto sounding =
            render(processor, 2, 512, 8, 120.0, true, {noteOn(0, 60, 100)});
        assert(sounding.rms > 0.0001);
        assert(sounding.peak > 0.001);

        // Gain is jig:paramIndex 4, declared 0..1. Normalised zero is silence,
        // and it takes effect without the graph being rebuilt.
        assert(processor.enqueueParameter(4, 0.0));
        const auto muted =
            render(processor, 2, 512, 8, 120.0, true, {noteOn(0, 64, 100)});
        assert(muted.peak < sounding.peak);

        // A parameter the profile does not declare is refused rather than
        // written to whatever index the module happens to have there.
        std::string parameterError;
        assert(!processor.setParameter(99, 0.5, parameterError));
        assert(!parameterError.empty());
    }

    // BassGen: jig:Abi2, no audio at all, MIDI out, and silent until the
    // transport is rolling with a tempo and a beat. The transport is the whole
    // point of version 2, so a host that fills the block in wrongly gets
    // nothing and a host that does not fill it in at all gets the same nothing.
    {
        transmission::JigdawProcessor processor;
        std::string error;
        assert(processor.initialize(iriFor("bassgen"), 256, 48000.0, error));
        assert(error.empty());
        const auto& topology = processor.topology();
        assert(topology.name == "BassGen");
        assert(topology.audioInputs == 0);
        assert(topology.audioOutputs == 0);
        assert(topology.midiOutputs == 1);
        assert(topology.requiresTransport);

        const auto stopped = render(processor, 2, 256, 32, 120.0, false, {});
        assert(stopped.midiOut == 0);
        // No audio outputs means the buffers passing through are left silent
        // rather than filled with whatever the module's memory held.
        assert(stopped.rms == 0.0);

        const auto rolling = render(processor, 2, 256, 32, 128.0, true, {});
        assert(rolling.midiOut > 0);
        assert(rolling.rms == 0.0);
        for (const auto& event : rolling.events) {
            assert(event.size >= 1 && event.size <= 3);
            // Rebased onto the whole block, not left as a sub-block offset.
            assert(event.frameOffset < 256);
            const auto status = static_cast<std::uint8_t>(event.data[0] & 0xF0);
            assert(status == 0x80 || status == 0x90);
        }
        // Taking the output twice does not replay it: the count describes the
        // block that just ran and nothing else.
        std::array<transmission::MidiEvent, 8> again{};
        assert(processor.takeOutputMidi(again.data(), again.size()) == 0);
    }

    std::cout << "JigdawProcessorTest passed\n";
    return 0;
}
