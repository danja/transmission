// native/src/AudioProcessor.cpp

#include "transmission/AudioProcessor.h"

#include <algorithm>
#include <cmath>
#include <numbers>

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
                             std::vector<GainEnvelopePoint> points,
                             double pan)
    : sampleRate_(sampleRate),
      gainDb_(std::clamp(gainDb, minimumGainDb, maximumGainDb)),
      pan_(std::clamp(pan, -1.0, 1.0)), points_(std::move(points)) {}

bool GainProcessor::applyNormalizedParameter(
    std::uint32_t parameterId, double normalizedValue) noexcept {
    if (!std::isfinite(normalizedValue) || normalizedValue < 0.0 ||
        normalizedValue > 1.0)
        return false;
    if (parameterId == gainParameterId) {
        gainDb_.store(
            minimumGainDb +
                normalizedValue * (maximumGainDb - minimumGainDb),
            std::memory_order_release);
        return true;
    }
    if (parameterId == panParameterId) {
        pan_.store(normalizedValue * 2.0 - 1.0,
                   std::memory_order_release);
        return true;
    }
    return false;
}

bool GainProcessor::setParameter(std::uint32_t parameterId,
                                 double normalizedValue,
                                 std::string& error) {
    if (applyNormalizedParameter(parameterId, normalizedValue)) return true;
    error = "gain/pan parameter id or normalized value is invalid";
    return false;
}

bool GainProcessor::enqueueParameter(
    std::uint32_t parameterId, double normalizedValue) noexcept {
    return applyNormalizedParameter(parameterId, normalizedValue);
}

void GainProcessor::setProcessContext(const AudioProcessContext& context) noexcept {
    context_ = context;
}

double GainProcessor::gainAt(double beat, double gainDb) const noexcept {
    if (points_.empty()) return std::pow(10.0, gainDb / 20.0);
    if (beat <= points_.front().beat)
        return std::pow(10.0, (gainDb + points_.front().valueDb) / 20.0);
    const auto next = std::upper_bound(
        points_.begin(), points_.end(), beat,
        [](double value, const auto& point) { return value < point.beat; });
    if (next == points_.end())
        return std::pow(10.0, (gainDb + points_.back().valueDb) / 20.0);
    const auto& previous = *(next - 1);
    auto valueDb = previous.valueDb;
    if (previous.linear) {
        const auto amount = (beat - previous.beat) / (next->beat - previous.beat);
        valueDb += (next->valueDb - previous.valueDb) * amount;
    }
    return std::pow(10.0, (gainDb + valueDb) / 20.0);
}

void GainProcessor::process(const float* const* inputs, float* const* outputs,
                            std::size_t channels, std::size_t frames) noexcept {
    if (!inputs || !outputs) return;
    const auto gainDb = gainDb_.load(std::memory_order_acquire);
    const auto pan = pan_.load(std::memory_order_acquire);
    const auto leftBalance = pan <= 0.0 ? 1.0 : std::cos(pan * std::numbers::pi / 2.0);
    const auto rightBalance = pan >= 0.0 ? 1.0 : std::cos(-pan * std::numbers::pi / 2.0);
    const auto beatsPerSample = context_.playing && sampleRate_ > 0.0
        ? context_.tempo / (60.0 * sampleRate_) : 0.0;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const auto gain = static_cast<float>(gainAt(
            context_.projectTimeMusic + beatsPerSample * static_cast<double>(frame),
            gainDb));
        for (std::size_t channel = 0; channel < channels; ++channel)
            if (inputs[channel] && outputs[channel])
                outputs[channel][frame] = inputs[channel][frame] * gain *
                    static_cast<float>(
                        channel == 0 ? leftBalance
                        : channel == 1 ? rightBalance : 1.0);
    }
}

} // namespace transmission
