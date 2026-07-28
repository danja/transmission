// native/include/transmission/AudioEngine.h

#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

namespace transmission {

struct Diagnostics {
    std::uint64_t underruns = 0;
    std::uint64_t processedBlocks = 0;
    std::uint64_t controlEventsDropped = 0;
    bool running = false;
    bool graphLoaded = false;
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
    Diagnostics diagnostics() const;

private:
    mutable std::mutex controlMutex_;
    std::string serializedGraph_;
    Diagnostics diagnostics_;
};

} // namespace transmission
