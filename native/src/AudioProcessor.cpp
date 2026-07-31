// native/src/AudioProcessor.cpp

#include "transmission/AudioProcessor.h"

#include <algorithm>
#include <cmath>

namespace transmission {

void AudioProcessor::process(const float* const* inputs, std::size_t inputChannels,
                             float* const* outputs, std::size_t outputChannels,
                             std::size_t frames) noexcept {
    if (inputChannels == outputChannels) {
        process(inputs, outputs, inputChannels, frames);
        return;
    }
    if (!outputs) return;
    for (std::size_t channel = 0; channel < outputChannels; ++channel)
        if (outputs[channel]) std::fill_n(outputs[channel], frames, 0.0F);
}

void AudioProcessor::processWithMidi(
    const float* const* inputs, std::size_t inputChannels,
    float* const* outputs, std::size_t outputChannels, std::size_t frames,
    const MidiEvent* events, std::size_t eventCount) noexcept {
    if (inputChannels == outputChannels) {
        processWithMidi(inputs, outputs, inputChannels, frames, events, eventCount);
        return;
    }
    process(inputs, inputChannels, outputs, outputChannels, frames);
}

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

GainProcessor::GainProcessor(double sampleRate, double gainDb,
                             std::vector<GainEnvelopePoint> points)
    : sampleRate_(sampleRate), gainDb_(gainDb), points_(std::move(points)) {}

void GainProcessor::setProcessContext(const AudioProcessContext& context) noexcept {
    context_ = context;
}

double GainProcessor::gainAt(double beat) const noexcept {
    if (points_.empty()) return std::pow(10.0, gainDb_ / 20.0);
    if (beat <= points_.front().beat)
        return std::pow(10.0, (gainDb_ + points_.front().valueDb) / 20.0);
    const auto next = std::upper_bound(
        points_.begin(), points_.end(), beat,
        [](double value, const auto& point) { return value < point.beat; });
    if (next == points_.end())
        return std::pow(10.0, (gainDb_ + points_.back().valueDb) / 20.0);
    const auto& previous = *(next - 1);
    auto valueDb = previous.valueDb;
    if (previous.linear) {
        const auto amount = (beat - previous.beat) / (next->beat - previous.beat);
        valueDb += (next->valueDb - previous.valueDb) * amount;
    }
    return std::pow(10.0, (gainDb_ + valueDb) / 20.0);
}

void GainProcessor::process(const float* const* inputs, float* const* outputs,
                            std::size_t channels, std::size_t frames) noexcept {
    if (!inputs || !outputs) return;
    const auto beatsPerSample = context_.playing && sampleRate_ > 0.0
        ? context_.tempo / (60.0 * sampleRate_) : 0.0;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const auto gain = static_cast<float>(gainAt(
            context_.projectTimeMusic + beatsPerSample * static_cast<double>(frame)));
        for (std::size_t channel = 0; channel < channels; ++channel)
            if (inputs[channel] && outputs[channel])
                outputs[channel][frame] = inputs[channel][frame] * gain;
    }
}

} // namespace transmission
