// native/src/AudioProcessor.cpp

#include "transmission/AudioProcessor.h"

#include <algorithm>

namespace transmission {

void PassThroughProcessor::process(const float* const* inputs, float* const* outputs,
                                   std::size_t channels, std::size_t frames) noexcept {
    if (!inputs || !outputs) return;
    for (std::size_t channel = 0; channel < channels; ++channel) {
        if (!inputs[channel] || !outputs[channel]) continue;
        std::copy_n(inputs[channel], frames, outputs[channel]);
    }
}

} // namespace transmission
