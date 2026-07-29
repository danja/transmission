#include "transmission/GraphRuntimeCompiler.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <limits>

namespace transmission {

GraphRuntimeCompiler::GraphRuntimeCompiler(RuntimeProcessorFactory processorFactory)
    : processorFactory_(std::move(processorFactory)) {}

std::unique_ptr<RoutedAudioGraph> GraphRuntimeCompiler::compile(
    const RuntimeGraphSnapshot& snapshot, const AudioDeviceConfig& config,
    std::string& error) const {
    if (!processorFactory_ || snapshot.nodes.empty() || config.channels == 0 ||
        config.blockSize == 0 || config.sampleRate <= 0.0) {
        error = "runtime graph and audio device configuration must be valid";
        return nullptr;
    }

    std::unordered_map<std::string, std::size_t> nodeIndices;
    for (std::size_t index = 0; index < snapshot.nodes.size(); ++index) {
        const auto& node = snapshot.nodes[index];
        if (node.id.empty() || !nodeIndices.emplace(node.id, index).second) {
            error = "runtime graph node IDs must be non-empty and unique";
            return nullptr;
        }
        if (node.kind == RuntimeNodeKind::Plugin && node.pluginPath.empty()) {
            error = "runtime plugin node is missing its bundle path: " + node.id;
            return nullptr;
        }
    }

    std::vector<std::vector<std::size_t>> outgoing(snapshot.nodes.size());
    std::vector<std::size_t> incoming(snapshot.nodes.size(), 0);
    std::unordered_set<std::string> typedEdges;
    for (const auto& connection : snapshot.connections) {
        const auto from = nodeIndices.find(connection.from);
        const auto to = nodeIndices.find(connection.to);
        if (from == nodeIndices.end() || to == nodeIndices.end() ||
            from->second == to->second) {
            error = "runtime graph connection has an invalid endpoint";
            return nullptr;
        }
        const auto key = std::to_string(static_cast<int>(connection.kind)) + ":" +
                         connection.from + ":" + connection.to + ":" +
                         std::to_string(connection.fromPort) + ":" +
                         std::to_string(connection.toPort);
        if (!typedEdges.emplace(key).second) continue;
        auto& dependencies = outgoing[from->second];
        if (std::find(dependencies.begin(), dependencies.end(), to->second) ==
            dependencies.end()) {
            dependencies.push_back(to->second);
            ++incoming[to->second];
        }
    }
    auto remaining = incoming;
    std::vector<std::size_t> ready;
    for (std::size_t index = 0; index < remaining.size(); ++index)
        if (remaining[index] == 0) ready.push_back(index);
    std::size_t visited = 0;
    while (!ready.empty()) {
        const auto current = ready.back();
        ready.pop_back();
        ++visited;
        for (const auto next : outgoing[current])
            if (--remaining[next] == 0) ready.push_back(next);
    }
    if (visited != snapshot.nodes.size()) {
        error = "runtime graph contains an audio or MIDI cycle";
        return nullptr;
    }

    auto graph = std::make_unique<RoutedAudioGraph>();
    for (const auto& node : snapshot.nodes) {
        auto processor = processorFactory_(node, config, error);
        if (!processor) {
            if (error.empty()) error = "unable to create processor for node: " + node.id;
            return nullptr;
        }
        std::size_t audioInputs = node.audioInputs;
        std::size_t audioOutputs = node.audioOutputs;
        if (node.kind == RuntimeNodeKind::SystemInput ||
            node.kind == RuntimeNodeKind::SystemOutput) {
            audioInputs = config.channels;
            audioOutputs = config.channels;
        }
        if (!graph->addNode(node.id, std::move(processor),
                            audioInputs, audioOutputs)) {
            error = "unable to add runtime graph node: " + node.id;
            return nullptr;
        }
        for (const auto& parameter : node.parameters) {
            if (!graph->setParameter(node.id, parameter.id,
                                     parameter.normalizedValue, error)) {
                if (error.empty())
                    error = "unable to restore parameter for node: " + node.id;
                return nullptr;
            }
        }
        if (node.kind == RuntimeNodeKind::SystemInput &&
            !graph->setExternalAudioInput(node.id)) {
            error = "unable to configure the runtime audio input endpoint";
            return nullptr;
        }
        if (node.kind == RuntimeNodeKind::SystemOutput &&
            !graph->setExternalAudioOutput(node.id)) {
            error = "unable to configure the runtime audio output endpoint";
            return nullptr;
        }
        if ((node.kind == RuntimeNodeKind::SystemInput ||
             node.kind == RuntimeNodeKind::MidiInput) &&
            !graph->setExternalMidiInput(node.id, node.externalMidiPort)) {
            error = "unable to configure the runtime MIDI input endpoint";
            return nullptr;
        }
        if (node.kind == RuntimeNodeKind::MidiOutput &&
            !graph->setExternalMidiOutput(node.id, node.externalMidiPort)) {
            error = "unable to configure the runtime MIDI output endpoint";
            return nullptr;
        }
    }

    typedEdges.clear();
    for (const auto& connection : snapshot.connections) {
        const auto key = std::to_string(static_cast<int>(connection.kind)) + ":" +
                         connection.from + ":" + connection.to + ":" +
                         std::to_string(connection.fromPort) + ":" +
                         std::to_string(connection.toPort);
        if (!typedEdges.emplace(key).second) continue;
        const auto noPort = std::numeric_limits<std::size_t>::max();
        const bool connected = connection.kind == RuntimeConnectionKind::Audio
            ? (connection.fromPort == noPort || connection.toPort == noPort
                   ? graph->connect(connection.from, connection.to)
                   : graph->connect(connection.from, connection.fromPort,
                                    connection.to, connection.toPort))
            : graph->connectMidi(connection.from, connection.to);
        if (!connected) {
            error = "unable to compile runtime connection: " + connection.from +
                    " -> " + connection.to;
            return nullptr;
        }
    }
    return graph;
}

} // namespace transmission
