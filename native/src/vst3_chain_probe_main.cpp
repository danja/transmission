#include "transmission/GraphRuntimeCompiler.h"
#include "transmission/Vst3Processor.h"

#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: transmission_vst3_chain_probe <MIDI-generator.vst3> "
                     "<instrument.vst3>\n";
        return 2;
    }
    constexpr std::size_t channels = 2;
    constexpr std::size_t frames = 1024;
    constexpr double sampleRate = 48000.0;
    constexpr double tempo = 120.0;
    const transmission::AudioDeviceConfig config{channels, frames, sampleRate, false, 1};
    transmission::GraphRuntimeCompiler compiler(
        [](const transmission::RuntimeGraphNode& node,
           const transmission::AudioDeviceConfig& device,
           std::string& error) -> std::unique_ptr<transmission::AudioProcessor> {
            if (node.kind != transmission::RuntimeNodeKind::Plugin)
                return std::make_unique<transmission::PassThroughProcessor>();
            auto processor = std::make_unique<transmission::Vst3Processor>();
            if (!processor->initialize(node.pluginPath, device.channels, device.blockSize,
                                       device.sampleRate, error))
                return nullptr;
            return processor;
        });
    using NodeKind = transmission::RuntimeNodeKind;
    using EdgeKind = transmission::RuntimeConnectionKind;
    const transmission::RuntimeGraphSnapshot snapshot{
        {{"input", NodeKind::SystemInput, ""},
         {"generator", NodeKind::Plugin, argv[1]},
         {"instrument", NodeKind::Plugin, argv[2]},
         {"output", NodeKind::SystemOutput, ""}},
        {{"input", "generator", EdgeKind::Midi},
         {"generator", "instrument", EdgeKind::Midi},
         {"generator", "instrument", EdgeKind::Audio},
         {"instrument", "output", EdgeKind::Audio}}};
    std::string error;
    auto graph = compiler.compile(snapshot, config, error);
    if (!graph || !graph->prepare(channels, frames)) {
        std::cerr << (error.empty() ? "Unable to prepare VST3 chain" : error) << '\n';
        return 1;
    }
    std::vector<float> input(channels * frames, 0.0F);
    std::vector<float> output(channels * frames, 0.0F);
    const float* inputs[] = {input.data(), input.data() + frames};
    float* outputs[] = {output.data(), output.data() + frames};
    double energy = 0.0;
    double beat = 0.0;
    for (std::size_t block = 0; block < 256; ++block) {
        graph->setProcessContext({beat, tempo, true});
        graph->process(inputs, outputs, channels, frames);
        for (const auto sample : output) energy += sample * sample;
        beat += static_cast<double>(frames) * tempo / (60.0 * sampleRate);
    }
    const auto rms = std::sqrt(energy / static_cast<double>(channels * frames * 256));
    std::cout << "blocks=256\noutputRms=" << rms << '\n';
    return rms > 0.0 ? 0 : 1;
}
