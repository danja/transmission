#include "transmission/Vst3Inspector.h"
#include "transmission/Vst3Processor.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: transmission_vst3_graph_probe <plugin.vst3>\n";
        return 2;
    }
    constexpr std::size_t frames = 512;
    constexpr double sampleRate = 48000.0;
    transmission::Vst3PluginTopology topology;
    std::string error;
    if (!transmission::Vst3Inspector().inspectTopology(argv[1], topology, error)) {
        std::cerr << "VST3 topology inspection failed: " << error << "\n";
        return 1;
    }
    const auto inputChannels = topology.audioInputs.size();
    const auto outputChannels = topology.audioOutputs.size();
    std::vector<std::vector<float>> input(inputChannels,
                                          std::vector<float>(frames));
    std::vector<std::vector<float>> output(
        outputChannels, std::vector<float>(frames, 0.0F));
    std::vector<const float*> inputPointers(inputChannels);
    std::vector<float*> outputPointers(outputChannels);
    for (std::size_t channel = 0; channel < inputChannels; ++channel) {
        inputPointers[channel] = input[channel].data();
        for (std::size_t frame = 0; frame < frames; ++frame) {
            input[channel][frame] = static_cast<float>(0.25 * std::sin(
                2.0 * 3.141592653589793 * 220.0 * frame / sampleRate));
        }
    }
    for (std::size_t channel = 0; channel < outputChannels; ++channel)
        outputPointers[channel] = output[channel].data();

    auto vst = std::make_unique<transmission::Vst3Processor>();
    if (!vst->initialize(argv[1], inputChannels, outputChannels,
                         frames, sampleRate, error)) {
        std::cerr << "VST3 graph setup failed: " << error << "\n";
        return 1;
    }
    vst->setParameter(1, 1.0, error);
    const auto pluginName = vst->pluginName();
    vst->process(inputPointers.data(), inputChannels, outputPointers.data(),
                 outputChannels, frames);

    double energy = 0.0;
    for (const auto& channel : output)
        for (float sample : channel) energy += sample * sample;
    std::cout << "plugin=" << pluginName << "\n"
              << "graphProcessors=1\n"
              << "inputChannels=" << inputChannels << "\n"
              << "outputChannels=" << outputChannels << "\n"
              << "frames=" << frames << "\n"
              << "outputRms=" << std::sqrt(
                     energy / std::max<std::size_t>(1, outputChannels * frames))
              << "\n";
    return 0;
}
