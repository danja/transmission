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
    transport_.start();
    return true;
}

std::string AudioEngine::runtimeGraph() const {
    std::scoped_lock lock(controlMutex_);
    return serializedGraph_;
}

void AudioEngine::stop() {
    std::scoped_lock lock(controlMutex_);
    diagnostics_.running = false;
    transport_.stop();
}

bool AudioEngine::setTempo(double bpm, double atBeat) {
    std::scoped_lock lock(controlMutex_);
    if (diagnostics_.running) return false;
    return transport_.setTempo(bpm, atBeat);
}

bool AudioEngine::setLoop(double startBeat, double endBeat, bool enabled) {
    std::scoped_lock lock(controlMutex_);
    if (diagnostics_.running) return false;
    return transport_.setLoop(startBeat, endBeat, enabled);
}

void AudioEngine::seek(double beat) {
    std::scoped_lock lock(controlMutex_);
    if (!diagnostics_.running) transport_.seek(beat);
}

TransportAdvance AudioEngine::advanceTransport(std::size_t frames) noexcept {
    return transport_.advance(frames);
}

Diagnostics AudioEngine::diagnostics() const {
    std::scoped_lock lock(controlMutex_);
    diagnostics_.positionBeats = transport_.positionBeats();
    return diagnostics_;
}

} // namespace transmission
