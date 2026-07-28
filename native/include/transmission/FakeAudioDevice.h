// native/include/transmission/FakeAudioDevice.h

#pragma once

#include "AudioDevice.h"

#include <vector>

namespace transmission {

/** Deterministic offline device used by tests and headless development. */
class FakeAudioDevice final : public AudioDevice {
public:
    bool configure(const AudioDeviceConfig& config) override;
    bool start(AudioCallback& callback) override;
    void stop() noexcept override;

    bool render(const float* const* inputs, float* const* outputs) noexcept;
    bool running() const noexcept { return running_; }

private:
    AudioDeviceConfig config_;
    AudioCallback* callback_ = nullptr;
    bool configured_ = false;
    bool running_ = false;
    std::vector<float> inputStorage_;
    std::vector<float> outputStorage_;
};

} // namespace transmission
