#include "transmission/GraphRuntimeCompiler.h"

#include <cassert>
#include <memory>

namespace {

class GeneratorProcessor final : public transmission::AudioProcessor {
public:
    void process(const float* const* inputs, float* const* outputs,
                 std::size_t channels, std::size_t frames) noexcept override {
        for (std::size_t channel = 0; channel < channels; ++channel)
            for (std::size_t frame = 0; frame < frames; ++frame)
                outputs[channel][frame] = inputs[channel][frame];
    }

    std::size_t takeOutputMidi(transmission::MidiEvent* events,
                               std::size_t capacity) noexcept override {
        if (!events || capacity == 0) return 0;
        events[0].size = 3;
        events[0].data = {0x90, 36, 100};
        return 1;
    }
};

class CaptureProcessor final : public transmission::AudioProcessor {
public:
    void process(const float* const* inputs, float* const* outputs,
                 std::size_t channels, std::size_t frames) noexcept override {
        for (std::size_t channel = 0; channel < channels; ++channel)
            for (std::size_t frame = 0; frame < frames; ++frame)
                outputs[channel][frame] = inputs[channel][frame];
    }

    void processWithMidi(const float* const* inputs, float* const* outputs,
                         std::size_t channels, std::size_t frames,
                         const transmission::MidiEvent*, std::size_t eventCount) noexcept override {
        received = eventCount;
        process(inputs, outputs, channels, frames);
    }

    bool setParameter(std::uint32_t parameterId, double normalizedValue,
                      std::string&) override {
        lastParameterId = parameterId;
        lastParameterValue = normalizedValue;
        return true;
    }

    bool captureState(transmission::ProcessorState& state,
                      std::string&) override {
        state = capturedState;
        return true;
    }

    bool restoreState(const transmission::ProcessorState& state,
                      std::string&) override {
        restoredState = state;
        return true;
    }

    std::size_t received = 0;
    std::uint32_t lastParameterId = 0;
    double lastParameterValue = 0.0;
    transmission::ProcessorState restoredState;
    transmission::ProcessorState capturedState{{9, 8, 7}, {6, 5}};
};

transmission::RuntimeGraphSnapshot validSnapshot() {
    using NodeKind = transmission::RuntimeNodeKind;
    using EdgeKind = transmission::RuntimeConnectionKind;
    transmission::RuntimeGraphSnapshot snapshot{
        {{"input", NodeKind::SystemInput, ""},
         {"generator", NodeKind::Plugin, "/generator.vst3"},
         {"capture", NodeKind::Plugin, "/capture.vst3"},
         {"output", NodeKind::SystemOutput, ""}},
        {{"input", "generator", EdgeKind::Audio},
         {"generator", "capture", EdgeKind::Audio},
         {"capture", "output", EdgeKind::Audio},
         {"input", "generator", EdgeKind::Midi},
         {"generator", "capture", EdgeKind::Midi}}};
    for (auto& node : snapshot.nodes) {
        node.audioInputs = 1;
        node.audioOutputs = 1;
    }
    for (auto& connection : snapshot.connections) {
        if (connection.kind != EdgeKind::Audio) continue;
        connection.fromPort = 0;
        connection.toPort = 0;
    }
    return snapshot;
}

} // namespace

int main() {
    CaptureProcessor* capture = nullptr;
    transmission::GraphRuntimeCompiler compiler(
        [&](const transmission::RuntimeGraphNode& node,
            const transmission::AudioDeviceConfig&, std::string& error)
            -> std::unique_ptr<transmission::AudioProcessor> {
            if (node.id == "generator") return std::make_unique<GeneratorProcessor>();
            if (node.id == "capture") {
                auto processor = std::make_unique<CaptureProcessor>();
                capture = processor.get();
                return processor;
            }
            if (node.id == "factory-failure") {
                error = "factory failure";
                return nullptr;
            }
            return std::make_unique<transmission::PassThroughProcessor>();
        });
    const transmission::AudioDeviceConfig config{1, 4, 48000.0, false, 1};
    std::string error;
    auto parameterized = validSnapshot();
    parameterized.nodes[2].parameters.push_back({42, 0.75});
    parameterized.nodes[2].state = {{1, 2, 3}, {4, 5}};
    auto graph = compiler.compile(parameterized, config, error);
    assert(graph);
    assert(capture && capture->lastParameterId == 42);
    assert(capture->lastParameterValue == 0.75);
    assert(capture->restoredState.component ==
           parameterized.nodes[2].state.component);
    std::string stateError;
    const auto states = graph->processorStates(stateError);
    assert(stateError.empty());
    assert(states.size() == 1);
    assert(states[0].nodeId == "capture");
    assert(states[0].state.controller ==
           capture->capturedState.controller);
    assert(graph->prepare(config.channels, config.blockSize));
    const float input[] = {0.25F, 0.5F, 0.75F, 1.0F};
    float output[4] = {};
    const float* inputs[] = {input};
    float* outputs[] = {output};
    graph->processWithMidi(inputs, outputs, 1, 4, nullptr, 0);
    assert(capture && capture->received == 1);
    assert(output[3] == input[3]);

    auto duplicate = validSnapshot();
    duplicate.nodes.push_back(duplicate.nodes.front());
    assert(!compiler.compile(duplicate, config, error));

    auto missing = validSnapshot();
    missing.connections.push_back({"missing", "capture",
                                   transmission::RuntimeConnectionKind::Audio});
    assert(!compiler.compile(missing, config, error));

    auto missingPort = validSnapshot();
    missingPort.connections.front().fromPort = 2;
    assert(!compiler.compile(missingPort, config, error));

    auto cycle = validSnapshot();
    cycle.connections.push_back({"capture", "generator",
                                 transmission::RuntimeConnectionKind::Midi});
    assert(!compiler.compile(cycle, config, error));

    auto unsupportedState = validSnapshot();
    unsupportedState.nodes[0].state.component = {1};
    assert(!compiler.compile(unsupportedState, config, error));
    assert(error == "processor does not expose opaque state");

    auto failure = validSnapshot();
    failure.nodes.push_back({"factory-failure",
                             transmission::RuntimeNodeKind::Plugin,
                             "/failure.vst3"});
    assert(!compiler.compile(failure, config, error));
    assert(error == "factory failure");
    return 0;
}
