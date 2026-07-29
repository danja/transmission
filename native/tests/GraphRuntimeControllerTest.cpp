#include "transmission/FakeAudioDevice.h"
#include "transmission/GraphRuntimeController.h"

#include <cassert>
#include <memory>

int main() {
    transmission::GraphRuntimeController runtime(
        [](const transmission::RuntimeGraphNode&,
           const transmission::AudioDeviceConfig&,
           std::string&) -> std::unique_ptr<transmission::AudioProcessor> {
            return std::make_unique<transmission::PassThroughProcessor>();
        });
    transmission::FakeAudioDevice device;
    const transmission::AudioDeviceConfig config{1, 4, 48000.0, false, 1};
    const transmission::RuntimeGraphSnapshot snapshot{
        {{"input", transmission::RuntimeNodeKind::SystemInput, ""},
         {"output", transmission::RuntimeNodeKind::SystemOutput, ""}},
        {{"input", "output", transmission::RuntimeConnectionKind::Audio},
         {"input", "output", transmission::RuntimeConnectionKind::Midi}}};
    std::string error;
    assert(runtime.start(snapshot, device, config, {}, error));
    const float input[] = {0.25F, 0.5F, 0.75F, 1.0F};
    float output[] = {};
    const float* inputs[] = {input};
    float* outputs[] = {output};
    assert(device.render(inputs, outputs));
    assert(output[3] == input[3]);
    assert(runtime.diagnostics().processedBlocks == 1);
    runtime.stop();
    assert(!runtime.running());

    transmission::RuntimeGraphSnapshot invalid;
    assert(!runtime.start(invalid, device, config, {}, error));
    return 0;
}
