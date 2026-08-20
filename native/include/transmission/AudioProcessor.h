// native/include/transmission/AudioProcessor.h

#pragma once

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <string>
#include <vector>

#include "AudioDevice.h"

namespace transmission {

struct AudioProcessContext {
    double projectTimeMusic = 0.0;
    double tempo = 120.0;
    bool playing = false;
};

struct ProcessorState {
    std::vector<std::uint8_t> component;
    std::vector<std::uint8_t> controller;
};

/** Processing contract for the real-time thread. Implementations must not allocate. */
class AudioProcessor {
public:
    virtual ~AudioProcessor() = default;
    virtual void process(const float* const* inputs, float* const* outputs,
                         std::size_t channels, std::size_t frames) noexcept = 0;
    virtual void process(const float* const* inputs, std::size_t inputChannels,
                         float* const* outputs, std::size_t outputChannels,
                         std::size_t frames) noexcept;
    virtual void processWithMidi(const float* const* inputs, float* const* outputs,
                                 std::size_t channels, std::size_t frames,
                                 const MidiEvent* events, std::size_t eventCount) noexcept {
        (void)events;
        (void)eventCount;
        process(inputs, outputs, channels, frames);
    }
    virtual void processWithMidi(const float* const* inputs, std::size_t inputChannels,
                                 float* const* outputs, std::size_t outputChannels,
                                 std::size_t frames, const MidiEvent* events,
                                 std::size_t eventCount) noexcept;
    virtual bool setParameter(std::uint32_t /*parameterId*/, double /*normalizedValue*/,
                              std::string& error) {
        error = "processor does not expose parameters";
        return false;
    }
    /** Control-thread project persistence; never call while processing audio. */
    virtual bool captureState(ProcessorState& state, std::string& /*error*/) {
        state = {};
        return true;
    }
    /** Control-thread project restoration; never call while processing audio. */
    virtual bool restoreState(const ProcessorState& state, std::string& error) {
        if (state.component.empty() && state.controller.empty()) return true;
        error = "processor does not expose opaque state";
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
    /** Called on the control thread when the device block size changes post-activation. */
    virtual bool reconfigure(std::size_t /*frames*/, std::string& /*error*/) { return true; }
    /** Returns true when the processor is fully initialised and ready to process audio. */
    virtual bool ready() const noexcept { return true; }
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

struct GainEnvelopePoint {
    double beat = 0.0;
    double valueDb = 0.0;
    bool linear = true;
};

/** Built-in sample-accurate gain with an immutable beat-domain envelope. */
class GainProcessor final : public AudioProcessor {
public:
    static constexpr std::uint32_t gainParameterId = 0;
    static constexpr std::uint32_t panParameterId = 1;
    static constexpr double minimumGainDb = -120.0;
    static constexpr double maximumGainDb = 12.0;

    GainProcessor(double sampleRate, double gainDb,
                  std::vector<GainEnvelopePoint> points = {},
                  double pan = 0.0);
    bool setParameter(std::uint32_t parameterId, double normalizedValue,
                      std::string& error) override;
    bool enqueueParameter(std::uint32_t parameterId,
                          double normalizedValue) noexcept override;
    void process(const float* const* inputs, float* const* outputs,
                 std::size_t channels, std::size_t frames) noexcept override;
    void setProcessContext(const AudioProcessContext& context) noexcept override;

private:
    double gainAt(double beat, double gainDb) const noexcept;
    bool applyNormalizedParameter(std::uint32_t parameterId,
                                  double normalizedValue) noexcept;
    double sampleRate_ = 48000.0;
    std::atomic<double> gainDb_{0.0};
    std::atomic<double> pan_{0.0};
    std::vector<GainEnvelopePoint> points_;
    AudioProcessContext context_;
};

} // namespace transmission
