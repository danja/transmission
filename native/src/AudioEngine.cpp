// native/src/AudioEngine.cpp

#include "transmission/AudioEngine.h"

#include <algorithm>
#include <utility>

namespace transmission {

bool AudioEngine::loadRuntimeGraph(std::string serializedGraph) {
    std::scoped_lock lock(controlMutex_);
    if (diagnostics_.running || serializedGraph.empty()) return false;
    serializedGraph_ = std::move(serializedGraph);
    diagnostics_.graphLoaded = true;
    return true;
}

std::string AudioEngine::runtimeGraph() const {
    std::scoped_lock lock(controlMutex_);
    return serializedGraph_;
}

bool AudioEngine::configureDevice(AudioDevice& device, const AudioDeviceConfig& config) {
    std::scoped_lock lock(controlMutex_);
    if (diagnostics_.running || !device.configure(config)) return false;
    device_ = &device;
    deviceConfig_ = config;
    transport_.setSampleRate(config.sampleRate);
    positionBeats_.store(transport_.positionBeats(), std::memory_order_release);
    return true;
}

bool AudioEngine::setAudioGraph(std::unique_ptr<AudioGraph> graph,
                                std::size_t channels, std::size_t frames) {
    std::scoped_lock lock(controlMutex_);
    if (diagnostics_.running || !graph || channels == 0 || frames == 0) return false;
    if (device_ && (channels != deviceConfig_.channels || frames != deviceConfig_.blockSize)) return false;
    if (!graph->prepare(channels, frames)) return false;
    audioGraph_ = std::move(graph);
    graphChannels_ = channels;
    graphFrames_ = frames;
    return true;
}

bool AudioEngine::start() {
    std::scoped_lock lock(controlMutex_);
    if (serializedGraph_.empty() || diagnostics_.running) return false;
    diagnostics_.running = true;
    transport_.start();
    if (device_ && !device_->start(*this)) {
        transport_.stop();
        diagnostics_.running = false;
        return false;
    }
    return true;
}

void AudioEngine::stop() {
    std::scoped_lock lock(controlMutex_);
    if (device_) device_->stop();
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
    if (!diagnostics_.running) {
        transport_.seek(beat);
        positionBeats_.store(transport_.positionBeats(), std::memory_order_release);
    }
}

TransportAdvance AudioEngine::advanceTransport(std::size_t frames) noexcept {
    auto result = transport_.advance(frames);
    positionBeats_.store(result.endBeat, std::memory_order_release);
    return result;
}

Diagnostics AudioEngine::diagnostics() const {
    std::scoped_lock lock(controlMutex_);
    Diagnostics result = diagnostics_;
    result.underruns = underruns_.load(std::memory_order_acquire);
    result.processedBlocks = processedBlocks_.load(std::memory_order_acquire);
    result.midiEvents = midiEvents_.load(std::memory_order_acquire);
    result.positionBeats = positionBeats_.load(std::memory_order_acquire);
    return result;
}

void AudioEngine::process(const float* const* inputs, float* const* outputs,
                          std::size_t channels, std::size_t frames) noexcept {
    if (!outputs || channels == 0 || frames == 0) {
        underruns_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const auto advance = transport_.advance(frames);
    positionBeats_.store(advance.endBeat, std::memory_order_release);
    if (audioGraph_ && inputs && channels == graphChannels_ && frames == graphFrames_) {
        audioGraph_->process(inputs, outputs, channels, frames);
    } else {
        for (std::size_t channel = 0; channel < channels; ++channel) {
            if (outputs[channel]) std::fill(outputs[channel], outputs[channel] + frames, 0.0F);
        }
        if (audioGraph_) underruns_.fetch_add(1, std::memory_order_relaxed);
    }
    processedBlocks_.fetch_add(1, std::memory_order_relaxed);
}

void AudioEngine::handleMidi(const MidiEvent& /*event*/) noexcept {
    midiEvents_.fetch_add(1, std::memory_order_relaxed);
}

} // namespace transmission
