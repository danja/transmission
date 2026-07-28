// native/src/vst3_offline_probe_main.cpp

#include "transmission/Vst3OfflineProbe.h"

#include <cstdlib>
#include <iostream>

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        std::cerr << "Usage: transmission-vst3-offline <plugin.vst3> [frames]\n";
        return 2;
    }
    std::size_t frames = 512;
    if (argc == 3) {
        char* end = nullptr;
        const auto value = std::strtoull(argv[2], &end, 10);
        if (!end || *end != '\0' || value == 0) {
            std::cerr << "frames must be a positive integer\n";
            return 2;
        }
        frames = static_cast<std::size_t>(value);
    }

    transmission::Vst3ProbeResult result;
    std::string error;
    if (!transmission::Vst3OfflineProbe().process(argv[1], result, error, frames)) {
        std::cerr << "Offline VST3 processing failed: " << error << "\n";
        return 1;
    }
    std::cout << "plugin=" << result.pluginName << "\n"
              << "classId=" << result.classId << "\n"
              << "inputChannels=" << result.inputChannels << "\n"
              << "outputChannels=" << result.outputChannels << "\n"
              << "frames=" << result.frames << "\n"
              << "inputRms=" << result.inputRms << "\n"
              << "outputRms=" << result.outputRms << "\n";
    return 0;
}
