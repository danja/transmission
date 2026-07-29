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

void PassThroughProcessor::processWithMidi(const float* const* inputs, float* const* outputs,
                                           std::size_t channels, std::size_t frames,
                                           const MidiEvent* events,
                                           std::size_t eventCount) noexcept {
    midiInput_ = events;
    midiInputCount_ = eventCount;
    process(inputs, outputs, channels, frames);
}

std::size_t PassThroughProcessor::takeOutputMidi(MidiEvent* events,
                                                 std::size_t capacity) noexcept {
    const auto count = std::min(midiInputCount_, capacity);
    if (events && midiInput_) std::copy_n(midiInput_, count, events);
    midiInput_ = nullptr;
    midiInputCount_ = 0;
    return count;
}

void MidiEndpointProcessor::process(const float* const*, float* const* outputs,
                                    std::size_t channels,
                                    std::size_t frames) noexcept {
    if (!outputs) return;
    for (std::size_t channel = 0; channel < channels; ++channel)
        if (outputs[channel]) std::fill_n(outputs[channel], frames, 0.0F);
}

void MidiEndpointProcessor::processWithMidi(
    const float* const* inputs, float* const* outputs,
    std::size_t channels, std::size_t frames,
    const MidiEvent* events, std::size_t eventCount) noexcept {
    midiInput_ = events;
    midiInputCount_ = eventCount;
    process(inputs, outputs, channels, frames);
}

std::size_t MidiEndpointProcessor::takeOutputMidi(
    MidiEvent* events, std::size_t capacity) noexcept {
    const auto count = std::min(midiInputCount_, capacity);
    if (events && midiInput_) std::copy_n(midiInput_, count, events);
    midiInput_ = nullptr;
    midiInputCount_ = 0;
    return count;
}

} // namespace transmission
