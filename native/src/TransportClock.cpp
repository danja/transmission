// native/src/TransportClock.cpp

#include "transmission/TransportClock.h"

#include <algorithm>
#include <cmath>

namespace transmission {

TransportClock::TransportClock(double sampleRate) {
    if (sampleRate > 0.0) sampleRate_ = sampleRate;
    tempoMap_.push_back({0.0, 120.0});
}

bool TransportClock::setSampleRate(double sampleRate) noexcept {
    if (!std::isfinite(sampleRate) || sampleRate <= 0.0) return false;
    sampleRate_ = sampleRate;
    return true;
}

bool TransportClock::setTempo(double bpm, double atBeat) noexcept {
    if (!std::isfinite(bpm) || bpm <= 0.0 || !std::isfinite(atBeat) || atBeat < 0.0) return false;
    for (auto& change : tempoMap_) {
        if (change.beat == atBeat) {
            change.bpm = bpm;
            return true;
        }
    }
    tempoMap_.push_back({atBeat, bpm});
    std::sort(tempoMap_.begin(), tempoMap_.end(), [](auto left, auto right) { return left.beat < right.beat; });
    return true;
}

bool TransportClock::setLoop(double startBeat, double endBeat, bool enabled) noexcept {
    if (!std::isfinite(startBeat) || !std::isfinite(endBeat) || startBeat < 0.0 || endBeat <= startBeat) return false;
    loopStart_ = startBeat;
    loopEnd_ = endBeat;
    loopEnabled_ = enabled;
    if (positionBeats_ >= loopEnd_) positionBeats_ = loopStart_;
    return true;
}

void TransportClock::clearLoop() noexcept { loopEnabled_ = false; }

void TransportClock::seek(double beat) noexcept {
    if (std::isfinite(beat) && beat >= 0.0) positionBeats_ = beat;
}

void TransportClock::stop(bool reset) noexcept {
    running_ = false;
    if (reset) positionBeats_ = 0.0;
}

double TransportClock::tempoAt(double beat) const noexcept {
    double bpm = tempoMap_.front().bpm;
    for (const auto& change : tempoMap_) {
        if (change.beat > beat) break;
        bpm = change.bpm;
    }
    return bpm;
}

TransportAdvance TransportClock::advance(std::size_t frames) noexcept {
    TransportAdvance result;
    result.startBeat = positionBeats_;
    result.endBeat = positionBeats_;
    if (!running_ || frames == 0) return result;

    std::size_t remaining = frames;
    while (remaining > 0) {
        const double bpm = tempoAt(positionBeats_);
        const double beatsPerFrame = bpm / (60.0 * sampleRate_);
        double nextBoundary = loopEnabled_ ? loopEnd_ : INFINITY;
        for (const auto& change : tempoMap_) {
            if (change.beat > positionBeats_) {
                nextBoundary = std::min(nextBoundary, change.beat);
                break;
            }
        }
        const std::size_t boundaryFrames = std::isfinite(nextBoundary)
            ? std::max<std::size_t>(1, static_cast<std::size_t>(std::ceil((nextBoundary - positionBeats_) / beatsPerFrame)))
            : remaining;
        const std::size_t segmentFrames = std::min(remaining, boundaryFrames);
        const double segmentStart = positionBeats_;
        positionBeats_ += static_cast<double>(segmentFrames) * beatsPerFrame;
        if (result.segmentCount < TransportAdvance::maxSegments) {
            result.segments[result.segmentCount++] = {frames - remaining, segmentFrames, segmentStart, positionBeats_, bpm};
        }
        remaining -= segmentFrames;
        if (loopEnabled_ && positionBeats_ >= loopEnd_) {
            positionBeats_ = loopStart_;
            result.wrapped = true;
        }
    }
    result.endBeat = positionBeats_;
    return result;
}

} // namespace transmission
