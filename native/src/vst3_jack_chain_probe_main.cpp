#include "transmission/GraphRuntimeController.h"
#include "transmission/JackAudioDevice.h"
#include "transmission/JackConnectionManager.h"
#include "transmission/Vst3Processor.h"

#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: transmission_vst3_jack_chain_probe "
                     "<MIDI-generator.vst3> <instrument.vst3>\n";
        return 2;
    }
    const transmission::AudioDeviceConfig deviceConfig{2, 1024, 48000.0, false, 1};
    transmission::JackAudioDevice device;
    transmission::GraphRuntimeController runtime(
        [](const transmission::RuntimeGraphNode& node,
           const transmission::AudioDeviceConfig& config,
           std::string& error) -> std::unique_ptr<transmission::AudioProcessor> {
            if (node.kind != transmission::RuntimeNodeKind::Plugin)
                return std::make_unique<transmission::PassThroughProcessor>();
            auto processor = std::make_unique<transmission::Vst3Processor>();
            if (!processor->initialize(node.pluginPath, config.channels,
                                       config.blockSize, config.sampleRate, error))
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
    if (!runtime.start(snapshot, device, deviceConfig, {}, error)) {
        if (!device.lastError().empty()) error = device.lastError();
        std::cerr << error << '\n';
        return 1;
    }
    transmission::JackConnectionManager connections;
    if (!connections.connectOutput(0, "system:playback_1", error) ||
        !connections.connectOutput(1, "system:playback_2", error)) {
        std::cerr << error << '\n';
        return 1;
    }
    std::this_thread::sleep_for(std::chrono::seconds(2));
    const auto diagnostics = runtime.diagnostics();
    runtime.stop();
    std::cout << "processedBlocks=" << diagnostics.processedBlocks
              << "\nunderruns=" << diagnostics.underruns << '\n';
    return diagnostics.processedBlocks > 0 && diagnostics.underruns == 0 ? 0 : 1;
}
