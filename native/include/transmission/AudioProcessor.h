// native/include/transmission/AudioProcessor.h

#pragma once

#include <cstddef>

namespace transmission {

/** Processing contract for the real-time thread. Implementations must not allocate. */
class AudioProcessor {
public:
    virtual ~AudioProcessor() = default;
    virtual void process(const float* const* inputs, float* const* outputs,
                         std::size_t channels, std::size_t frames) noexcept = 0;
};

class PassThroughProcessor final : public AudioProcessor {
public:
    void process(const float* const* inputs, float* const* outputs,
                 std::size_t channels, std::size_t frames) noexcept override;
};

} // namespace transmission
