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
    bool ready() const noexcept;
    const std::string& pluginName() const noexcept;

    /** Queue a normalized parameter value for the next process block. */
    bool setParameter(std::uint32_t parameterId, double normalizedValue,
                      std::string& error);

    void process(const float* const* inputs, float* const* outputs,
                 std::size_t channels, std::size_t frames) noexcept override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace transmission
