#pragma once

#include "AudioDevice.h"
#include "RoutedAudioGraph.h"

#include <functional>
#include <cstdint>
#include <memory>
#include <limits>
#include <string>
#include <vector>

namespace transmission {

enum class RuntimeNodeKind {
    SystemInput, SystemOutput, PassThrough, Plugin, MidiInput, MidiOutput, Gain,
    AudioClip, MidiClip
};
enum class RuntimeConnectionKind { Audio, Midi };

struct RuntimeParameterValue {
    std::uint32_t id = 0;
    double normalizedValue = 0.0;
};

struct RuntimeGraphNode {
    std::string id;
    RuntimeNodeKind kind = RuntimeNodeKind::Plugin;
    std::string pluginPath;
    std::size_t externalMidiPort = 0;
    std::size_t audioInputs = 0;
    std::size_t audioOutputs = 0;
    std::vector<RuntimeParameterValue> parameters;
    ProcessorState state;
    double gainDb = 0.0;
    std::vector<GainEnvelopePoint> gainEnvelope;
    double pan = 0.0;
};

struct RuntimeGraphConnection {
    std::string from;
    std::string to;
    RuntimeConnectionKind kind = RuntimeConnectionKind::Audio;
    std::size_t fromPort = std::numeric_limits<std::size_t>::max();
    std::size_t toPort = std::numeric_limits<std::size_t>::max();
};

struct RuntimeGraphSnapshot {
    std::vector<RuntimeGraphNode> nodes;
    std::vector<RuntimeGraphConnection> connections;
    std::vector<ScheduledMidiEvent> scheduledMidiEvents;
    std::vector<MidiParameterMapping> midiParameterMappings;
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
