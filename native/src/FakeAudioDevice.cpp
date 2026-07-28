// native/src/FakeAudioDevice.cpp

#include "transmission/FakeAudioDevice.h"

namespace transmission {

bool FakeAudioDevice::configure(const AudioDeviceConfig& config) {
    if (config.channels == 0 || config.blockSize == 0 || config.sampleRate <= 0.0) return false;
    try {
        config_ = config;
        inputStorage_.assign(config.channels * config.blockSize, 0.0F);
        outputStorage_.assign(config.channels * config.blockSize, 0.0F);
    } catch (...) {
        inputStorage_.clear();
        outputStorage_.clear();
        configured_ = false;
        return false;
    }
    configured_ = true;
    return true;
}

bool FakeAudioDevice::start(AudioCallback& callback) {
    if (!configured_ || running_) return false;
    callback_ = &callback;
    running_ = true;
    return true;
}

void FakeAudioDevice::stop() noexcept {
    running_ = false;
    callback_ = nullptr;
}

bool FakeAudioDevice::render(const float* const* inputs, float* const* outputs) noexcept {
    if (!running_ || !callback_ || !inputs || !outputs) return false;
    callback_->process(inputs, outputs, config_.channels, config_.blockSize);
    return true;
}

} // namespace transmission
