// native/src/AudioGraph.cpp

#include "transmission/AudioGraph.h"

#include <algorithm>

namespace transmission {

void AudioGraph::addProcessor(std::unique_ptr<AudioProcessor> processor) {
    if (processor) processors_.push_back(std::move(processor));
}

bool AudioGraph::prepare(std::size_t channels, std::size_t frames) noexcept {
    if (channels == 0 || frames == 0) return false;
    try {
        scratch_.assign(processors_.size() > 1 ? processors_.size() - 1 : 0,
                        std::vector<float>(channels * frames, 0.0F));
        inputPointers_.resize(channels);
        outputPointers_.resize(channels);
    } catch (...) {
        scratch_.clear();
        inputPointers_.clear();
        outputPointers_.clear();
        preparedChannels_ = 0;
        preparedFrames_ = 0;
        return false;
    }
    preparedChannels_ = channels;
    preparedFrames_ = frames;
    return true;
}

void AudioGraph::process(const float* const* inputs, float* const* outputs,
                         std::size_t channels, std::size_t frames) noexcept {
    if (!inputs || !outputs || processors_.empty() || channels != preparedChannels_ || frames != preparedFrames_) return;

    const float* const* currentInputs = inputs;
    for (std::size_t index = 0; index < processors_.size(); ++index) {
        const bool last = index + 1 == processors_.size();
        if (last) {
            processors_[index]->process(currentInputs, outputs, channels, frames);
            continue;
        }
        for (std::size_t channel = 0; channel < channels; ++channel) {
            inputPointers_[channel] = currentInputs[channel];
            outputPointers_[channel] = scratch_[index].data() + channel * frames;
        }
        processors_[index]->process(inputPointers_.data(), outputPointers_.data(), channels, frames);
        currentInputs = inputPointers_.data();
        for (std::size_t channel = 0; channel < channels; ++channel) {
            inputPointers_[channel] = scratch_[index].data() + channel * frames;
        }
        currentInputs = inputPointers_.data();
    }
}

} // namespace transmission
