// native/tests/AudioDeviceTest.cpp

#include "transmission/FakeAudioDevice.h"

#include <cassert>

class TestCallback final : public transmission::AudioCallback {
public:
    void process(const float* const* inputs, float* const* outputs,
                 std::size_t channels, std::size_t frames) noexcept override {
        ++calls;
        for (std::size_t channel = 0; channel < channels; ++channel)
            for (std::size_t frame = 0; frame < frames; ++frame)
                outputs[channel][frame] = inputs[channel][frame];
    }
    int calls = 0;
};

int main() {
    transmission::FakeAudioDevice device;
    TestCallback callback;
    assert(!device.start(callback));
    assert(device.configure({1, 4, 48000.0}));
    assert(device.start(callback));
    const float input[] = {1.0F, 2.0F, 3.0F, 4.0F};
    float output[] = {0.0F, 0.0F, 0.0F, 0.0F};
    const float* inputs[] = {input};
    float* outputs[] = {output};
    assert(device.render(inputs, outputs));
    assert(callback.calls == 1 && output[3] == 4.0F);
    device.stop();
    assert(!device.render(inputs, outputs));
    return 0;
}
