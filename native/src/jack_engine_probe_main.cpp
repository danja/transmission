#include "transmission/AudioEngine.h"
#include "transmission/JackAudioDevice.h"

#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

int main(int argc, char** argv) {
    const bool autoConnect = argc == 2 && std::string(argv[1]) == "--auto-connect";
    if (argc > 2 || (argc == 2 && !autoConnect)) {
        std::cerr << "Usage: transmission_jack_engine_probe [--auto-connect]\n";
        return 2;
    }

    transmission::JackAudioDevice device;
    transmission::AudioEngine engine;
    auto graph = std::make_unique<transmission::AudioGraph>();
    graph->addProcessor(std::make_unique<transmission::PassThroughProcessor>());
    if (!engine.loadRuntimeGraph("{\"version\":1,\"nodes\":[\"passthrough\"]}") ||
        !engine.configureDevice(device, {2, 1024, 48000.0, autoConnect}) ||
        !engine.setAudioGraph(std::move(graph), 2, 1024) || !engine.start()) {
        std::cerr << "JACK engine setup failed: " << device.lastError() << "\n";
        return 1;
    }

    std::cout << "JACK engine running for 3 seconds; inspect ports with jack_lsp\n";
    std::this_thread::sleep_for(std::chrono::seconds(3));
    const auto diagnostics = engine.diagnostics();
    engine.stop();
    std::cout << "processedBlocks=" << diagnostics.processedBlocks << "\n"
              << "underruns=" << diagnostics.underruns << "\n"
              << "midiEvents=" << diagnostics.midiEvents << "\n";
    return 0;
}
