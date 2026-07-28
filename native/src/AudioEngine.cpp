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
    routedAudioGraph_.reset();
    graphChannels_ = channels;
    graphFrames_ = frames;
    return true;
}

bool AudioEngine::setRoutedAudioGraph(std::unique_ptr<RoutedAudioGraph> graph,
                                      std::size_t channels, std::size_t frames) {
    std::scoped_lock lock(controlMutex_);
    if (diagnostics_.running || !graph || channels == 0 || frames == 0) return false;
    if (device_ && (channels != deviceConfig_.channels || frames != deviceConfig_.blockSize)) return false;
    if (!graph->prepare(channels, frames)) return false;
    routedAudioGraph_ = std::move(graph);
    audioGraph_.reset();
    graphChannels_ = channels;
    graphFrames_ = frames;
    return true;
}

bool AudioEngine::setSampleRate(double sampleRate) {
    std::scoped_lock lock(controlMutex_);
    if (diagnostics_.running) return false;
    return transport_.setSampleRate(sampleRate);
}

bool AudioEngine::setParameter(const std::string& nodeId, std::uint32_t parameterId,
                               double normalizedValue, std::string& error) {
    std::scoped_lock lock(controlMutex_);
    if (diagnostics_.running) {
        if (!routedAudioGraph_ || !routedAudioGraph_->enqueueParameter(nodeId, parameterId, normalizedValue)) {
            error = "real-time parameter queue is full or node does not support parameters";
            return false;
        }
        return true;
    }
    if (!routedAudioGraph_) {
        error = "no routed graph is loaded";
        return false;
    }
    return routedAudioGraph_->setParameter(nodeId, parameterId, normalizedValue, error);
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
    while (midiEventCount_ < maxMidiEventsPerBlock) {
        const auto read = midiQueueRead_.load(std::memory_order_relaxed);
        if (read == midiQueueWrite_.load(std::memory_order_acquire)) break;
        midiEventBuffer_[midiEventCount_++] = midiControlQueue_[read];
        midiQueueRead_.store((read + 1) % maxMidiEventsPerBlock, std::memory_order_release);
    }
    const auto advance = transport_.advance(frames);
    positionBeats_.store(advance.endBeat, std::memory_order_release);
    if (audioGraph_ && inputs && channels == graphChannels_ && frames == graphFrames_) {
        audioGraph_->processWithMidi(inputs, outputs, channels, frames,
                                     midiEventBuffer_.data(), midiEventCount_);
    } else if (routedAudioGraph_ && inputs && channels == graphChannels_ && frames == graphFrames_) {
        routedAudioGraph_->processWithMidi(inputs, outputs, channels, frames,
                                           midiEventBuffer_.data(), midiEventCount_);
    } else {
        for (std::size_t channel = 0; channel < channels; ++channel) {
            if (outputs[channel]) std::fill(outputs[channel], outputs[channel] + frames, 0.0F);
        }
        if (audioGraph_ || routedAudioGraph_) underruns_.fetch_add(1, std::memory_order_relaxed);
    }
    midiEventCount_ = 0;
    processedBlocks_.fetch_add(1, std::memory_order_relaxed);
}

void AudioEngine::handleMidi(const MidiEvent& event) noexcept {
    midiEvents_.fetch_add(1, std::memory_order_relaxed);
    if (midiEventCount_ < maxMidiEventsPerBlock)
        midiEventBuffer_[midiEventCount_++] = event;
}

bool AudioEngine::enqueueMidi(const MidiEvent& event) noexcept {
    const auto write = midiQueueWrite_.load(std::memory_order_relaxed);
    const auto next = (write + 1) % maxMidiEventsPerBlock;
    if (next == midiQueueRead_.load(std::memory_order_acquire)) return false;
    midiControlQueue_[write] = event;
    midiQueueWrite_.store(next, std::memory_order_release);
    midiEvents_.fetch_add(1, std::memory_order_relaxed);
    return true;
}

} // namespace transmission
