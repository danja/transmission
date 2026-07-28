#include "transmission/AudioGraph.h"
#include "transmission/Vst3Processor.h"

#include <cmath>
#include <iostream>
#include <memory>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: transmission_vst3_graph_probe <plugin.vst3>\n";
        return 2;
    }
    constexpr std::size_t channels = 2;
    constexpr std::size_t frames = 512;
    constexpr double sampleRate = 48000.0;
    std::vector<std::vector<float>> input(channels, std::vector<float>(frames));
    std::vector<std::vector<float>> output(channels, std::vector<float>(frames, 0.0F));
    std::vector<const float*> inputPointers(channels);
    std::vector<float*> outputPointers(channels);
    for (std::size_t channel = 0; channel < channels; ++channel) {
        inputPointers[channel] = input[channel].data();
        outputPointers[channel] = output[channel].data();
        for (std::size_t frame = 0; frame < frames; ++frame) {
            input[channel][frame] = static_cast<float>(0.25 * std::sin(
                2.0 * 3.141592653589793 * 220.0 * frame / sampleRate));
        }
    }

    auto vst = std::make_unique<transmission::Vst3Processor>();
    std::string error;
    if (!vst->initialize(argv[1], channels, frames, sampleRate, error)) {
        std::cerr << "VST3 graph setup failed: " << error << "\n";
        return 1;
    }
    // AGain and many simple effects expose gain as parameter ID 1. Failure is
    // non-fatal so this remains useful with effects that do not expose it.
    vst->setParameter(1, 1.0, error);
    const auto pluginName = vst->pluginName();

    transmission::AudioGraph graph;
    graph.addProcessor(std::move(vst));
    if (!graph.prepare(channels, frames)) {
        std::cerr << "VST3 graph preparation failed\n";
        return 1;
    }
    graph.process(inputPointers.data(), outputPointers.data(), channels, frames);

    double energy = 0.0;
    for (const auto& channel : output)
        for (float sample : channel) energy += sample * sample;
    std::cout << "plugin=" << pluginName << "\n"
              << "graphProcessors=" << graph.processorCount() << "\n"
              << "frames=" << frames << "\n"
              << "outputRms=" << std::sqrt(energy / (channels * frames)) << "\n";
    return 0;
}
