// native/src/vst3_inspect_main.cpp

#include "transmission/Vst3Inspector.h"

#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: transmission-vst3-inspect <plugin.vst3>\n";
        return 2;
    }

    std::string error;
    const auto descriptors = transmission::Vst3Inspector().inspect(argv[1], error);
    if (descriptors.empty()) {
        std::cerr << "No VST3 classes found in " << argv[1] << "\n";
        if (!error.empty()) std::cerr << error << "\n";
        return 1;
    }

    for (const auto& descriptor : descriptors) {
        std::cout << "id=" << descriptor.classId << "\n"
                  << "name=" << descriptor.name << "\n"
                  << "vendor=" << descriptor.vendor << "\n"
                  << "category=" << descriptor.category << "\n"
                  << "module=" << descriptor.modulePath << "\n\n";
    }
    transmission::Vst3PluginTopology topology;
    if (transmission::Vst3Inspector().inspectTopology(argv[1], topology, error)) {
        std::cout << "audioInputs=" << topology.audioInputs.size() << "\n"
                  << "audioOutputs=" << topology.audioOutputs.size() << "\n"
                  << "midiInputs=" << topology.midiInputs << "\n"
                  << "midiOutputs=" << topology.midiOutputs << "\n";
        for (std::size_t index = 0; index < topology.audioInputs.size(); ++index)
            std::cout << "input." << index << "=" << topology.audioInputs[index].name << "\n";
        for (std::size_t index = 0; index < topology.audioOutputs.size(); ++index)
            std::cout << "output." << index << "=" << topology.audioOutputs[index].name << "\n";
    }
    return 0;
}
