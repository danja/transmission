// native/include/transmission/AudioClipProcessor.h
#pragma once

#include "AudioProcessor.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace transmission {

/** Loops a pre-loaded WAV file as stereo audio when the transport is playing. */
class AudioClipProcessor final : public AudioProcessor {
public:
    /** Load from a WAV file. Safe to call on the control thread before audio starts. */
    bool load(const std::string& path, double deviceSampleRate, std::string& error);

    void process(const float* const* inputs, float* const* outputs,
                 std::size_t channels, std::size_t frames) noexcept override;
    void process(const float* const* inputs, std::size_t inputChannels,
                 float* const* outputs, std::size_t outputChannels,
                 std::size_t frames) noexcept override;
    void setProcessContext(const AudioProcessContext& context) noexcept override;

private:
    std::vector<float> samples_; // interleaved stereo, pre-allocated
    std::uint64_t frameCount_ = 0;
    std::uint64_t playHead_   = 0;
    bool          playing_    = false;
};

} // namespace transmission
