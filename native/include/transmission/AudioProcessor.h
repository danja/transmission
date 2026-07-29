// native/include/transmission/AudioProcessor.h

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "AudioDevice.h"

namespace transmission {

struct AudioProcessContext {
    double projectTimeMusic = 0.0;
    double tempo = 120.0;
    bool playing = false;
};

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
    virtual void setProcessContext(const AudioProcessContext& /*context*/) noexcept {}
    virtual std::size_t takeOutputMidi(MidiEvent* /*events*/, std::size_t /*capacity*/) noexcept {
        return 0;
    }
};

class PassThroughProcessor final : public AudioProcessor {
public:
    void process(const float* const* inputs, float* const* outputs,
                 std::size_t channels, std::size_t frames) noexcept override;
    void processWithMidi(const float* const* inputs, float* const* outputs,
                         std::size_t channels, std::size_t frames,
                         const MidiEvent* events, std::size_t eventCount) noexcept override;
    std::size_t takeOutputMidi(MidiEvent* events, std::size_t capacity) noexcept override;

private:
    const MidiEvent* midiInput_ = nullptr;
    std::size_t midiInputCount_ = 0;
};

/** MIDI-only endpoint that forwards bounded events and always emits silent audio. */
class MidiEndpointProcessor final : public AudioProcessor {
public:
    void process(const float* const* inputs, float* const* outputs,
                 std::size_t channels, std::size_t frames) noexcept override;
    void processWithMidi(const float* const* inputs, float* const* outputs,
                         std::size_t channels, std::size_t frames,
                         const MidiEvent* events, std::size_t eventCount) noexcept override;
    std::size_t takeOutputMidi(MidiEvent* events,
                               std::size_t capacity) noexcept override;

private:
    const MidiEvent* midiInput_ = nullptr;
    std::size_t midiInputCount_ = 0;
};

} // namespace transmission
