#include "transmission/FakeAudioDevice.h"
#include "transmission/GraphRuntimeController.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>

namespace {

class ParameterProcessor final : public transmission::AudioProcessor {
public:
    void process(const float* const* inputs, float* const* outputs,
                 std::size_t channels, std::size_t frames) noexcept override {
        for (std::size_t channel = 0; channel < channels; ++channel)
            std::copy_n(inputs[channel], frames, outputs[channel]);
    }

    bool enqueueParameter(std::uint32_t parameterId,
                          double normalizedValue) noexcept override {
        lastId = parameterId;
        lastValue = normalizedValue;
        return true;
    }

    bool captureState(transmission::ProcessorState& state,
                      std::string&) override {
        state = {{1, 3, 5}, {2, 4}};
        return true;
    }

    std::uint32_t lastId = 0;
    double lastValue = 0.0;
};

} // namespace

int main() {
    ParameterProcessor* parameterProcessor = nullptr;
    transmission::GraphRuntimeController runtime(
        [&](const transmission::RuntimeGraphNode& node,
           const transmission::AudioDeviceConfig&,
           std::string&) -> std::unique_ptr<transmission::AudioProcessor> {
            if (node.id == "parameter") {
                auto processor = std::make_unique<ParameterProcessor>();
                parameterProcessor = processor.get();
                return processor;
            }
            return std::make_unique<transmission::PassThroughProcessor>();
        });
    transmission::FakeAudioDevice device;
    const transmission::AudioDeviceConfig config{1, 4, 48000.0, false, 1};
    const transmission::RuntimeGraphSnapshot snapshot{
        {{"input", transmission::RuntimeNodeKind::SystemInput, ""},
         {"parameter", transmission::RuntimeNodeKind::Plugin, "/parameter.vst3"},
         {"output", transmission::RuntimeNodeKind::SystemOutput, ""}},
        {{"input", "parameter", transmission::RuntimeConnectionKind::Audio},
         {"parameter", "output", transmission::RuntimeConnectionKind::Audio},
         {"input", "output", transmission::RuntimeConnectionKind::Midi}}};
    std::string error;
    assert(runtime.setRenderAheadBlocks(0));
    assert(runtime.setProcessingThreadCount(0));
    assert(runtime.start(snapshot, device, config, {}, error));
    assert(!runtime.setRenderAheadBlocks(1));
    assert(!runtime.setProcessingThreadCount(1));
    assert(runtime.setParameter("parameter", 17, 0.625, error));
    assert(parameterProcessor && parameterProcessor->lastId == 17);
    assert(parameterProcessor->lastValue == 0.625);
    const float input[] = {0.25F, 0.5F, 0.75F, 1.0F};
    float output[4] = {};
    const float* inputs[] = {input};
    float* outputs[] = {output};
    assert(device.render(inputs, outputs));
    assert(output[3] == input[3]);
    assert(runtime.diagnostics().processedBlocks == 1);
    std::string runningStateError;
    assert(runtime.processorStates(runningStateError).empty());
    assert(runningStateError ==
           "stop audio before capturing processor state");
    runtime.stop();
    assert(!runtime.running());
    const auto states = runtime.processorStates(error);
    assert(error.empty());
    assert(states.size() == 1);
    assert(states[0].nodeId == "parameter");
    assert(states[0].state.component ==
           std::vector<std::uint8_t>({1, 3, 5}));

    transmission::RuntimeGraphSnapshot invalid;
    assert(!runtime.start(invalid, device, config, {}, error));
    return 0;
}
