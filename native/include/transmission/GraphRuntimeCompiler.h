#pragma once

#include "AudioDevice.h"
#include "RoutedAudioGraph.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace transmission {

enum class RuntimeNodeKind { SystemInput, SystemOutput, PassThrough, Plugin };
enum class RuntimeConnectionKind { Audio, Midi };

struct RuntimeGraphNode {
    std::string id;
    RuntimeNodeKind kind = RuntimeNodeKind::Plugin;
    std::string pluginPath;
};

struct RuntimeGraphConnection {
    std::string from;
    std::string to;
    RuntimeConnectionKind kind = RuntimeConnectionKind::Audio;
};

struct RuntimeGraphSnapshot {
    std::vector<RuntimeGraphNode> nodes;
    std::vector<RuntimeGraphConnection> connections;
};

using RuntimeProcessorFactory = std::function<std::unique_ptr<AudioProcessor>(
    const RuntimeGraphNode&, const AudioDeviceConfig&, std::string&)>;

/** Compiles editor/model execution data into a validated native graph. */
class GraphRuntimeCompiler {
public:
    explicit GraphRuntimeCompiler(RuntimeProcessorFactory processorFactory);

    std::unique_ptr<RoutedAudioGraph> compile(const RuntimeGraphSnapshot& snapshot,
                                              const AudioDeviceConfig& config,
                                              std::string& error) const;

private:
    RuntimeProcessorFactory processorFactory_;
};

} // namespace transmission
