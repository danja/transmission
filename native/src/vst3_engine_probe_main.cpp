#include "transmission/AudioEngine.h"
#include "transmission/FakeAudioDevice.h"
#include "transmission/Vst3Processor.h"

#include <cmath>
#include <iostream>
#include <memory>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: transmission_vst3_engine_probe <plugin.vst3>\n";
        return 2;
    }
    constexpr std::size_t channels = 2;
    constexpr std::size_t frames = 512;
    constexpr double sampleRate = 48000.0;

    auto processor = std::make_unique<transmission::Vst3Processor>();
    std::string error;
    if (!processor->initialize(argv[1], channels, frames, sampleRate, error)) {
        std::cerr << "VST3 engine setup failed: " << error << "\n";
        return 1;
    }
    processor->setParameter(1, 1.0, error);
    const auto pluginName = processor->pluginName();

    auto graph = std::make_unique<transmission::AudioGraph>();
    graph->addProcessor(std::move(processor));
    transmission::FakeAudioDevice device;
    transmission::AudioEngine engine;
    if (!engine.loadRuntimeGraph("{\"version\":1,\"nodes\":[\"vst3\"]}")) return 1;
    if (!engine.configureDevice(device, {channels, frames, sampleRate}) ||
        !engine.setAudioGraph(std::move(graph), channels, frames) || !engine.start()) {
        std::cerr << "engine/device start failed\n";
        return 1;
    }

    std::vector<std::vector<float>> input(channels, std::vector<float>(frames));
    std::vector<std::vector<float>> output(channels, std::vector<float>(frames, 0.0F));
    std::vector<const float*> inputs(channels);
    std::vector<float*> outputs(channels);
    for (std::size_t channel = 0; channel < channels; ++channel) {
        inputs[channel] = input[channel].data();
        outputs[channel] = output[channel].data();
        for (std::size_t frame = 0; frame < frames; ++frame)
            input[channel][frame] = static_cast<float>(0.25 * std::sin(
                2.0 * 3.141592653589793 * 220.0 * frame / sampleRate));
    }
    if (!device.render(inputs.data(), outputs.data())) {
        engine.stop();
        std::cerr << "device render failed\n";
        return 1;
    }
    double energy = 0.0;
    for (const auto& channel : output)
        for (float sample : channel) energy += sample * sample;
    const auto diagnostics = engine.diagnostics();
    engine.stop();
    std::cout << "plugin=" << pluginName << "\n"
              << "processedBlocks=" << diagnostics.processedBlocks << "\n"
              << "underruns=" << diagnostics.underruns << "\n"
              << "outputRms=" << std::sqrt(energy / (channels * frames)) << "\n";
    return 0;
}
