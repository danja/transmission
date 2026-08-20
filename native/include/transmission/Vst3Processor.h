#pragma once

#include "AudioProcessor.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace transmission {

/**
 * A prepared VST3 audio effect exposed through the engine's real-time
 * processor contract. Loading and all SDK allocations happen in initialize;
 * process() only updates preallocated SDK structures and calls the plug-in.
 */
class Vst3Processor final : public AudioProcessor {
public:
    Vst3Processor();
    ~Vst3Processor() override;

    Vst3Processor(const Vst3Processor&) = delete;
    Vst3Processor& operator=(const Vst3Processor&) = delete;

    bool initialize(const std::string& modulePath, std::size_t channels,
                    std::size_t frames, double sampleRate,
                    std::string& error);
    bool initialize(const std::string& modulePath, std::size_t inputChannels,
                    std::size_t outputChannels, std::size_t frames,
                    double sampleRate, std::string& error);
    bool ready() const noexcept override;
    const std::string& pluginName() const noexcept;

    /** Queue a normalized parameter value for the next process block. */
    bool setParameter(std::uint32_t parameterId, double normalizedValue,
                      std::string& error) override;
    bool captureState(ProcessorState& state, std::string& error) override;
    bool restoreState(const ProcessorState& state, std::string& error) override;
    bool enqueueParameter(std::uint32_t parameterId, double normalizedValue) noexcept override;
    void applyPendingParameters() noexcept override;
    void setProcessContext(const AudioProcessContext& context) noexcept override;
    std::size_t takeOutputMidi(MidiEvent* events, std::size_t capacity) noexcept override;
    bool reconfigure(std::size_t frames, std::string& error) override;

    void process(const float* const* inputs, float* const* outputs,
                 std::size_t channels, std::size_t frames) noexcept override;
    void process(const float* const* inputs, std::size_t inputChannels,
                 float* const* outputs, std::size_t outputChannels,
                 std::size_t frames) noexcept override;
    void processWithMidi(const float* const* inputs, float* const* outputs,
                         std::size_t channels, std::size_t frames,
                         const MidiEvent* events, std::size_t eventCount) noexcept override;
    void processWithMidi(const float* const* inputs, std::size_t inputChannels,
                         float* const* outputs, std::size_t outputChannels,
                         std::size_t frames, const MidiEvent* events,
                         std::size_t eventCount) noexcept override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace transmission
