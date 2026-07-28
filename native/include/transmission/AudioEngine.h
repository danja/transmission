// native/include/transmission/AudioEngine.h

#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

#include "TransportClock.h"

namespace transmission {

struct Diagnostics {
    std::uint64_t underruns = 0;
    std::uint64_t processedBlocks = 0;
    std::uint64_t controlEventsDropped = 0;
    bool running = false;
    bool graphLoaded = false;
    double positionBeats = 0.0;
};

/** Control-plane lifecycle contract for the future VST3/JACK engine. */
class AudioEngine {
public:
    AudioEngine() = default;
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    bool loadRuntimeGraph(std::string serializedGraph);
    std::string runtimeGraph() const;
    bool start();
    void stop();
    bool setTempo(double bpm, double atBeat);
    bool setLoop(double startBeat, double endBeat, bool enabled);
    void seek(double beat);
    TransportAdvance advanceTransport(std::size_t frames) noexcept;
    Diagnostics diagnostics() const;

private:
    mutable std::mutex controlMutex_;
    std::string serializedGraph_;
    mutable Diagnostics diagnostics_;
    TransportClock transport_;
};

} // namespace transmission
