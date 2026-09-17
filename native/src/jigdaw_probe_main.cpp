// native/src/jigdaw_probe_main.cpp
//
// Render a JigDAW plugin offline and say what came out. No device, no JACK and
// no display, so it is the first thing to reach for when a patch is silent.

#include "transmission/AudioDevice.h"
#include "transmission/JigdawProcessor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

double toDouble(const char* text, double fallback) {
    try { return std::stod(text); } catch (...) { return fallback; }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: transmission-jigdaw-probe <plugin-iri> [options]\n"
                     "  --blocks N     blocks to render (default 16)\n"
                     "  --frames N     frames per block (default 256)\n"
                     "  --rate HZ      sample rate (default 48000)\n"
                     "  --bpm BPM      transport tempo (default 120)\n"
                     "  --note PITCH   hold this note for the whole render\n"
                     "  --stopped      leave the transport not rolling\n";
        return 2;
    }

    const std::string iri = argv[1];
    std::size_t blocks = 16;
    std::size_t frames = 256;
    double sampleRate = 48000.0;
    double tempo = 120.0;
    int note = -1;
    bool playing = true;
    for (int index = 2; index < argc; ++index) {
        const std::string option = argv[index];
        const bool hasValue = index + 1 < argc;
        if (option == "--stopped") playing = false;
        else if (option == "--blocks" && hasValue) blocks = static_cast<std::size_t>(toDouble(argv[++index], 16));
        else if (option == "--frames" && hasValue) frames = static_cast<std::size_t>(toDouble(argv[++index], 256));
        else if (option == "--rate" && hasValue) sampleRate = toDouble(argv[++index], 48000.0);
        else if (option == "--bpm" && hasValue) tempo = toDouble(argv[++index], 120.0);
        else if (option == "--note" && hasValue) note = static_cast<int>(toDouble(argv[++index], 60));
        else {
            std::cerr << "Unknown option: " << option << "\n";
            return 2;
        }
    }
    if (blocks == 0 || frames == 0 || sampleRate <= 0.0) {
        std::cerr << "Block count, frame count and sample rate must be positive\n";
        return 2;
    }

    transmission::JigdawProcessor processor;
    std::string error;
    if (!processor.initialize(iri, frames, sampleRate, error)) {
        std::cerr << "Unable to load " << iri << ": " << error << "\n";
        return 1;
    }
    const auto& topology = processor.topology();
    std::cout << "name=" << topology.name << "\n"
              << "abi=" << topology.abi << "\n"
              << "audioInputs=" << topology.audioInputs << "\n"
              << "audioOutputs=" << topology.audioOutputs << "\n"
              << "midiInputs=" << topology.midiInputs << "\n"
              << "midiOutputs=" << topology.midiOutputs << "\n";

    const std::size_t channels = topology.audioOutputs > 0 ? topology.audioOutputs : 2;
    std::vector<std::vector<float>> inputStorage(channels, std::vector<float>(frames, 0.0F));
    std::vector<std::vector<float>> outputStorage(channels, std::vector<float>(frames, 0.0F));
    std::vector<const float*> inputs(channels);
    std::vector<float*> outputs(channels);
    for (std::size_t channel = 0; channel < channels; ++channel) {
        inputs[channel] = inputStorage[channel].data();
        outputs[channel] = outputStorage[channel].data();
    }

    std::vector<transmission::MidiEvent> midiIn;
    if (note >= 0) {
        transmission::MidiEvent on;
        on.frameOffset = 0;
        on.size = 3;
        on.data = {0x90, static_cast<std::uint8_t>(note), 100};
        midiIn.push_back(on);
    }

    double sum = 0.0;
    double peak = 0.0;
    std::size_t samples = 0;
    std::size_t emitted = 0;
    std::array<transmission::MidiEvent, transmission::maxMidiEventsPerBlock> taken{};
    const double beatsPerBlock = tempo / 60.0 * static_cast<double>(frames) / sampleRate;

    for (std::size_t block = 0; block < blocks; ++block) {
        transmission::AudioProcessContext context;
        context.playing = playing;
        context.tempo = tempo;
        context.projectTimeMusic = beatsPerBlock * static_cast<double>(block);
        processor.setProcessContext(context);
        processor.applyPendingParameters();
        processor.processWithMidi(inputs.data(), channels, outputs.data(), channels,
                                  frames, midiIn.empty() ? nullptr : midiIn.data(),
                                  block == 0 ? midiIn.size() : 0);
        const auto count = processor.takeOutputMidi(taken.data(), taken.size());
        for (std::size_t index = 0; index < count; ++index) {
            if (emitted < 8)
                std::cout << "midiOut block=" << block
                          << " frame=" << taken[index].frameOffset
                          << " status=" << static_cast<int>(taken[index].data[0])
                          << " data1=" << static_cast<int>(taken[index].data[1])
                          << " data2=" << static_cast<int>(taken[index].data[2]) << "\n";
            ++emitted;
        }
        for (std::size_t channel = 0; channel < channels; ++channel) {
            for (std::size_t frame = 0; frame < frames; ++frame) {
                const double value = outputStorage[channel][frame];
                sum += value * value;
                peak = std::max(peak, std::abs(value));
                ++samples;
            }
        }
    }

    const double rms = samples > 0 ? std::sqrt(sum / static_cast<double>(samples)) : 0.0;
    std::cout << "blocks=" << blocks << " frames=" << frames << "\n"
              << "midiOutCount=" << emitted << "\n"
              << "AUDIO rms=" << rms << " peak=" << peak << "\n";
    if (rms == 0.0 && emitted == 0)
        std::cout << "(silence and no MIDI — check the transport, the notes and the parameters)\n";
    return 0;
}
