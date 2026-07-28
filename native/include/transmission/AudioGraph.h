// native/include/transmission/AudioGraph.h

#pragma once

#include "AudioProcessor.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace transmission {

/** A linear, preallocated processor chain for deterministic offline processing. */
class AudioGraph {
public:
    void addProcessor(std::unique_ptr<AudioProcessor> processor);
    bool prepare(std::size_t channels, std::size_t frames) noexcept;
    void process(const float* const* inputs, float* const* outputs,
                 std::size_t channels, std::size_t frames) noexcept;
    void processWithMidi(const float* const* inputs, float* const* outputs,
                         std::size_t channels, std::size_t frames,
                         const MidiEvent* events, std::size_t eventCount) noexcept;

    std::size_t processorCount() const noexcept { return processors_.size(); }

private:
    std::vector<std::unique_ptr<AudioProcessor>> processors_;
    std::vector<std::vector<float>> scratch_;
    std::vector<const float*> inputPointers_;
    std::vector<float*> outputPointers_;
    std::size_t preparedChannels_ = 0;
    std::size_t preparedFrames_ = 0;
};

} // namespace transmission
