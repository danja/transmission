// native/src/jigdaw_inspect_main.cpp
//
// The JigDAW counterpart of transmission_vst3_inspect: dereference a plugin
// IRI and report the port shape and parameters a project must agree with.
// The key names match the VST3 tool's so that the same greps work on both.

#include "transmission/JigdawProcessor.h"

#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: transmission-jigdaw-inspect <plugin-iri>\n"
                     "  https://example.org/plugins/pulse/  or  file:///path/to/pulse/\n";
        return 2;
    }

    transmission::JigdawPluginTopology topology;
    std::string error;
    if (!transmission::JigdawInspector().inspectTopology(argv[1], topology, error)) {
        std::cerr << "Unable to inspect " << argv[1] << ": " << error << "\n";
        return 1;
    }

    std::cout << "iri=" << topology.iri << "\n"
              << "name=" << topology.name << "\n"
              << "vendor=" << topology.vendor << "\n"
              << "abi=" << topology.abi << "\n"
              << "audioInputs=" << topology.audioInputs << "\n"
              << "audioOutputs=" << topology.audioOutputs << "\n"
              << "midiInputs=" << topology.midiInputs << "\n"
              << "midiOutputs=" << topology.midiOutputs << "\n"
              << "requiresTransport=" << (topology.requiresTransport ? 1 : 0) << "\n"
              << "latencyFrames=" << topology.latencyFrames << "\n"
              << "parameterCount=" << topology.parameters.size() << "\n";
    for (std::size_t index = 0; index < topology.parameters.size(); ++index) {
        const auto& parameter = topology.parameters[index];
        std::cout << "parameter." << index << ".id=" << parameter.id << "\n"
                  << "parameter." << index << ".symbol=" << parameter.symbol << "\n"
                  << "parameter." << index << ".title=" << parameter.name << "\n"
                  << "parameter." << index << ".units=" << parameter.unit << "\n"
                  << "parameter." << index << ".minimum=" << parameter.minimum << "\n"
                  << "parameter." << index << ".maximum=" << parameter.maximum << "\n"
                  << "parameter." << index << ".default=" << parameter.defaultValue << "\n"
                  << "parameter." << index << ".toggled=" << (parameter.toggled ? 1 : 0) << "\n"
                  << "parameter." << index << ".enumeration=" << (parameter.enumeration ? 1 : 0) << "\n"
                  << "parameter." << index << ".scalePoints=" << parameter.scalePoints.size() << "\n";
        for (std::size_t point = 0; point < parameter.scalePoints.size(); ++point)
            std::cout << "parameter." << index << ".scalePoint." << point << "="
                      << parameter.scalePoints[point].value << " "
                      << parameter.scalePoints[point].label << "\n";
    }
    return 0;
}
