// native/src/AudioEngine.cpp

#include "transmission/AudioEngine.h"

#include <utility>

namespace transmission {

bool AudioEngine::loadRuntimeGraph(std::string serializedGraph) {
    std::scoped_lock lock(controlMutex_);
    if (diagnostics_.running || serializedGraph.empty()) return false;
    serializedGraph_ = std::move(serializedGraph);
    diagnostics_.graphLoaded = true;
    return true;
}

bool AudioEngine::start() {
    std::scoped_lock lock(controlMutex_);
    if (serializedGraph_.empty() || diagnostics_.running) return false;
    diagnostics_.running = true;
    return true;
}

std::string AudioEngine::runtimeGraph() const {
    std::scoped_lock lock(controlMutex_);
    return serializedGraph_;
}

void AudioEngine::stop() {
    std::scoped_lock lock(controlMutex_);
    diagnostics_.running = false;
}

Diagnostics AudioEngine::diagnostics() const {
    std::scoped_lock lock(controlMutex_);
    return diagnostics_;
}

} // namespace transmission
