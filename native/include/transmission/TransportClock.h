// native/include/transmission/TransportClock.h

#pragma once

#include <cstddef>
#include <array>
#include <vector>

namespace transmission {

struct TransportSegment {
    std::size_t startFrame = 0;
    std::size_t frames = 0;
    double startBeat = 0.0;
    double endBeat = 0.0;
    double bpm = 120.0;
};

struct TransportAdvance {
    double startBeat = 0.0;
    double endBeat = 0.0;
    bool wrapped = false;
    static constexpr std::size_t maxSegments = 32;
    std::array<TransportSegment, maxSegments> segments{};
    std::size_t segmentCount = 0;
};

/** Musical clock. Configure it off the callback; advance() performs no allocation. */
class TransportClock {
public:
    explicit TransportClock(double sampleRate = 48000.0);

    bool setSampleRate(double sampleRate) noexcept;
    bool setTempo(double bpm, double atBeat) noexcept;
    bool setLoop(double startBeat, double endBeat, bool enabled) noexcept;
    void clearLoop() noexcept;
    void seek(double beat) noexcept;
    void start() noexcept { running_ = true; }
    void stop(bool reset = false) noexcept;

    TransportAdvance advance(std::size_t frames) noexcept;
    double positionBeats() const noexcept { return positionBeats_; }
    bool running() const noexcept { return running_; }

private:
    struct TempoChange { double beat; double bpm; };
    double tempoAt(double beat) const noexcept;
    double sampleRate_ = 48000.0;
    double positionBeats_ = 0.0;
    bool running_ = false;
    bool loopEnabled_ = false;
    double loopStart_ = 0.0;
    double loopEnd_ = 0.0;
    std::vector<TempoChange> tempoMap_;
};

} // namespace transmission
