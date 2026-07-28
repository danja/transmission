// native/include/transmission/AudioProcessor.h

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "AudioDevice.h"

namespace transmission {

/** Processing contract for the real-time thread. Implementations must not allocate. */
class AudioProcessor {
public:
    virtual ~AudioProcessor() = default;
    virtual void process(const float* const* inputs, float* const* outputs,
                         std::size_t channels, std::size_t frames) noexcept = 0;
    virtual void processWithMidi(const float* const* inputs, float* const* outputs,
                                 std::size_t channels, std::size_t frames,
                                 const MidiEvent* events, std::size_t eventCount) noexcept {
        (void)events;
        (void)eventCount;
        process(inputs, outputs, channels, frames);
    }
    virtual bool setParameter(std::uint32_t /*parameterId*/, double /*normalizedValue*/,
                              std::string& error) {
        error = "processor does not expose parameters";
        return false;
    }
    /** Submit a parameter update without touching control-plane state. */
    virtual bool enqueueParameter(std::uint32_t /*parameterId*/, double /*normalizedValue*/) noexcept {
        return false;
    }
    /** Apply queued parameter updates on the audio thread before processing. */
    virtual void applyPendingParameters() noexcept {}
};

class PassThroughProcessor final : public AudioProcessor {
public:
    void process(const float* const* inputs, float* const* outputs,
                 std::size_t channels, std::size_t frames) noexcept override;
};

} // namespace transmission
