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
    return 0;
}
