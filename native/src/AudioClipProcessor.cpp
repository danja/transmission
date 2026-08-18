// native/src/AudioClipProcessor.cpp

#include "transmission/AudioClipProcessor.h"
#include "transmission/WavReader.h"

#include <algorithm>
#include <cmath>

namespace transmission {

bool AudioClipProcessor::load(const std::string& path, double deviceSampleRate,
                              std::string& error) {
    (void)deviceSampleRate;
    auto wav = loadWav(path);
    if (!wav.error.empty()) { error = wav.error; return false; }
    if (wav.frameCount == 0) { error = "WAV file is empty"; return false; }
    samples_    = std::move(wav.samples);
    frameCount_ = wav.frameCount;
    playHead_   = 0;
    return true;
}

void AudioClipProcessor::process(const float* const* /*inputs*/,
                                 float* const* outputs,
                                 std::size_t channels,
                                 std::size_t frames) noexcept {
    const std::size_t outChannels = std::min(channels, std::size_t{2});
    if (!playing_ || frameCount_ == 0) {
        for (std::size_t ch = 0; ch < outChannels; ++ch)
            if (outputs && outputs[ch]) std::fill_n(outputs[ch], frames, 0.0f);
        return;
    }
    for (std::size_t f = 0; f < frames; ++f) {
        const std::size_t pos = static_cast<std::size_t>(playHead_ % frameCount_);
        for (std::size_t ch = 0; ch < outChannels; ++ch)
            if (outputs && outputs[ch])
                outputs[ch][f] = samples_[pos * 2 + ch];
        ++playHead_;
    }
}

void AudioClipProcessor::process(const float* const* inputs, std::size_t /*inputChannels*/,
                                 float* const* outputs, std::size_t outputChannels,
                                 std::size_t frames) noexcept {
    process(inputs, outputs, outputChannels, frames);
}

void AudioClipProcessor::setProcessContext(const AudioProcessContext& ctx) noexcept {
    if (!ctx.playing && playing_) {
        // Transport stopped: reset playhead so next play starts from beginning
        playHead_ = 0;
    }
    playing_ = ctx.playing;
}

} // namespace transmission
