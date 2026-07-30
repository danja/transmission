// native/src/AudioEngine.cpp

#include "transmission/AudioEngine.h"

#include <algorithm>
#include <chrono>
#include <thread>
#include <utility>

namespace transmission {

AudioEngine::~AudioEngine() { stopRenderWorker(); }

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

bool AudioEngine::setRenderAheadBlocks(std::size_t blocks) {
    std::scoped_lock lock(controlMutex_);
    if (diagnostics_.running || blocks > maximumRenderAheadBlocks)
        return false;
    renderAheadBlocks_ = blocks;
    return true;
}

bool AudioEngine::setProcessingThreadCount(std::size_t threads) {
    std::scoped_lock lock(controlMutex_);
    if (diagnostics_.running) return false;
    requestedProcessingThreads_ = threads;
    return true;
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
    if (renderAheadBlocks_ != 0 && !prepareRenderBuffers()) return false;
    if (routedAudioGraph_) {
        routedAudioGraph_->setTimingEnabled(renderAheadBlocks_ != 0);
        auto requestedThreads = requestedProcessingThreads_;
        if (requestedThreads == 0)
            requestedThreads = std::max(
                1U, std::thread::hardware_concurrency());
        if (renderAheadBlocks_ == 0) requestedThreads = 1;
        if (!routedAudioGraph_->configureProcessingThreads(
                requestedThreads))
            return false;
    }
    renderInputRead_.store(0, std::memory_order_relaxed);
    renderInputWrite_.store(0, std::memory_order_relaxed);
    renderOutputRead_.store(0, std::memory_order_relaxed);
    renderOutputWrite_.store(0, std::memory_order_relaxed);
    callbackSequence_ = 0;
    stopRenderWorker_.store(false, std::memory_order_release);
    if (renderAheadBlocks_ != 0) {
        try {
            renderWorker_ = std::thread(&AudioEngine::renderWorkerLoop, this);
        } catch (...) {
            return false;
        }
    }
    diagnostics_.running = true;
    transport_.start();
    if (device_ && !device_->start(*this)) {
        transport_.stop();
        diagnostics_.running = false;
        stopRenderWorker();
        if (routedAudioGraph_)
            routedAudioGraph_->configureProcessingThreads(1);
        return false;
    }
    return true;
}

void AudioEngine::stop() {
    std::scoped_lock lock(controlMutex_);
    if (diagnostics_.running && device_) device_->stop();
    stopRenderWorker();
    if (routedAudioGraph_)
        routedAudioGraph_->configureProcessingThreads(1);
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
    result.renderLateBlocks =
        renderLateBlocks_.load(std::memory_order_acquire);
    result.renderQueueDrops =
        renderQueueDrops_.load(std::memory_order_acquire);
    result.renderAheadBlocks = renderAheadBlocks_;
    result.processingThreads = routedAudioGraph_
        ? routedAudioGraph_->processingThreadCount()
        : std::size_t{1};
    const auto rendered =
        timedRenderBlocks_.load(std::memory_order_acquire);
    const auto totalNanoseconds =
        renderTimeNanoseconds_.load(std::memory_order_acquire);
    result.averageRenderMicroseconds =
        rendered == 0 ? 0.0
                      : static_cast<double>(totalNanoseconds) /
                            static_cast<double>(rendered) / 1000.0;
    result.maximumRenderMicroseconds =
        static_cast<double>(
            maximumRenderNanoseconds_.load(std::memory_order_acquire)) /
        1000.0;
    result.positionBeats = positionBeats_.load(std::memory_order_acquire);
    return result;
}

std::vector<ProcessorTiming> AudioEngine::processorTimings() const {
    std::scoped_lock lock(controlMutex_);
    return routedAudioGraph_
        ? routedAudioGraph_->processorTimings()
        : std::vector<ProcessorTiming>{};
}

std::vector<NodeProcessorState> AudioEngine::processorStates(
    std::string& error) {
    std::scoped_lock lock(controlMutex_);
    if (diagnostics_.running) {
        error = "stop audio before capturing processor state";
        return {};
    }
    if (!routedAudioGraph_) return {};
    return routedAudioGraph_->processorStates(error);
}

void AudioEngine::process(const float* const* inputs, float* const* outputs,
                          std::size_t channels, std::size_t frames) noexcept {
    if (renderAheadBlocks_ == 0)
        processDirect(inputs, outputs, channels, frames);
    else
        processBuffered(inputs, outputs, channels, frames);
}

void AudioEngine::drainControlMidi() noexcept {
    while (midiEventCount_ < maxMidiEventsPerBlock) {
        const auto read = midiQueueRead_.load(std::memory_order_relaxed);
        if (read == midiQueueWrite_.load(std::memory_order_acquire)) break;
        midiEventBuffer_[midiEventCount_++] = midiControlQueue_[read];
        midiQueueRead_.store((read + 1) % maxMidiEventsPerBlock,
                             std::memory_order_release);
    }
}

bool AudioEngine::processGraphBlock(
    const float* const* inputs, float* const* outputs,
    std::size_t channels, std::size_t frames,
    const MidiEvent* midi, std::size_t midiCount,
    MidiEvent* outputMidi, std::size_t& outputMidiCount) noexcept {
    outputMidiCount = 0;
    if (audioGraph_ && inputs && channels == graphChannels_ &&
        frames == graphFrames_) {
        audioGraph_->processWithMidi(inputs, outputs, channels, frames,
                                     midi, midiCount);
        return true;
    }
    if (routedAudioGraph_ && inputs && channels == graphChannels_ &&
        frames == graphFrames_) {
        routedAudioGraph_->processWithMidi(inputs, outputs, channels, frames,
                                           midi, midiCount);
        outputMidiCount = routedAudioGraph_->takeExternalMidiOutput(
            outputMidi, maxMidiEventsPerBlock);
        return true;
    }
    return false;
}

void AudioEngine::processDirect(const float* const* inputs, float* const* outputs,
                                std::size_t channels,
                                std::size_t frames) noexcept {
    midiOutputCount_ = 0;
    if (!outputs || channels == 0 || frames == 0) {
        underruns_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    drainControlMidi();
    const auto advance = transport_.advance(frames);
    positionBeats_.store(advance.endBeat, std::memory_order_release);
    const auto tempo = advance.segmentCount > 0 ? advance.segments[0].bpm : 120.0;
    if (routedAudioGraph_)
        routedAudioGraph_->setProcessContext({advance.startBeat, tempo, true});
    if (!processGraphBlock(
            inputs, outputs, channels, frames, midiEventBuffer_.data(),
            midiEventCount_, midiOutputBuffer_.data(), midiOutputCount_)) {
        for (std::size_t channel = 0; channel < channels; ++channel) {
            if (outputs[channel]) std::fill(outputs[channel], outputs[channel] + frames, 0.0F);
        }
        if (audioGraph_ || routedAudioGraph_) underruns_.fetch_add(1, std::memory_order_relaxed);
    }
    midiEventCount_ = 0;
    processedBlocks_.fetch_add(1, std::memory_order_relaxed);
}

bool AudioEngine::prepareRenderBuffers() {
    if (graphChannels_ == 0 || graphFrames_ == 0) return false;
    try {
        for (auto& block : renderInputQueue_) {
            block.audio.assign(graphChannels_ * graphFrames_, 0.0F);
            block.channels.resize(graphChannels_);
            for (std::size_t channel = 0; channel < graphChannels_; ++channel)
                block.channels[channel] =
                    block.audio.data() + channel * graphFrames_;
        }
        for (auto& block : renderOutputQueue_) {
            block.audio.assign(graphChannels_ * graphFrames_, 0.0F);
            block.channels.resize(graphChannels_);
            for (std::size_t channel = 0; channel < graphChannels_; ++channel)
                block.channels[channel] =
                    block.audio.data() + channel * graphFrames_;
        }
    } catch (...) {
        return false;
    }
    return true;
}

void AudioEngine::processBuffered(const float* const* inputs,
                                  float* const* outputs,
                                  std::size_t channels,
                                  std::size_t frames) noexcept {
    midiOutputCount_ = 0;
    if (!inputs || !outputs || channels != graphChannels_ ||
        frames != graphFrames_) {
        if (outputs)
            for (std::size_t channel = 0; channel < channels; ++channel)
                if (outputs[channel])
                    std::fill_n(outputs[channel], frames, 0.0F);
        underruns_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    for (std::size_t channel = 0; channel < channels; ++channel)
        if (outputs[channel])
            std::fill_n(outputs[channel], frames, 0.0F);

    const auto sequence = callbackSequence_++;
    if (sequence >= renderAheadBlocks_) {
        const auto expected = sequence - renderAheadBlocks_;
        auto read = renderOutputRead_.load(std::memory_order_relaxed);
        const auto write =
            renderOutputWrite_.load(std::memory_order_acquire);
        while (read != write &&
               renderOutputQueue_[read].sequence < expected) {
            read = (read + 1) % renderQueueCapacity;
            renderOutputRead_.store(read, std::memory_order_release);
        }
        if (read != write &&
            renderOutputQueue_[read].sequence == expected) {
            const auto& block = renderOutputQueue_[read];
            for (std::size_t channel = 0; channel < channels; ++channel)
                std::copy_n(block.channels[channel], frames, outputs[channel]);
            midiOutputCount_ = std::min(
                block.midiCount, midiOutputBuffer_.size());
            std::copy_n(block.midi.begin(), midiOutputCount_,
                        midiOutputBuffer_.begin());
            positionBeats_.store(block.endBeat, std::memory_order_release);
            renderOutputRead_.store(
                (read + 1) % renderQueueCapacity,
                std::memory_order_release);
        } else {
            renderLateBlocks_.fetch_add(1, std::memory_order_relaxed);
            underruns_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    drainControlMidi();
    const auto advance = transport_.advance(frames);
    const auto tempo =
        advance.segmentCount > 0 ? advance.segments[0].bpm : 120.0;
    const auto write =
        renderInputWrite_.load(std::memory_order_relaxed);
    const auto next = (write + 1) % renderQueueCapacity;
    if (next == renderInputRead_.load(std::memory_order_acquire)) {
        renderQueueDrops_.fetch_add(1, std::memory_order_relaxed);
        midiEventCount_ = 0;
        return;
    }
    auto& block = renderInputQueue_[write];
    for (std::size_t channel = 0; channel < channels; ++channel)
        std::copy_n(inputs[channel], frames,
                    block.audio.data() + channel * frames);
    block.midiCount = std::min(midiEventCount_, block.midi.size());
    std::copy_n(midiEventBuffer_.begin(), block.midiCount,
                block.midi.begin());
    block.context = {advance.startBeat, tempo, true};
    block.endBeat = advance.endBeat;
    block.sequence = sequence;
    midiEventCount_ = 0;
    renderInputWrite_.store(next, std::memory_order_release);
}

void AudioEngine::renderWorkerLoop() noexcept {
    while (!stopRenderWorker_.load(std::memory_order_acquire)) {
        const auto inputRead =
            renderInputRead_.load(std::memory_order_relaxed);
        if (inputRead ==
            renderInputWrite_.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::microseconds(50));
            continue;
        }
        const auto outputWrite =
            renderOutputWrite_.load(std::memory_order_relaxed);
        const auto outputNext =
            (outputWrite + 1) % renderQueueCapacity;
        if (outputNext ==
            renderOutputRead_.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::microseconds(50));
            continue;
        }

        const auto& input = renderInputQueue_[inputRead];
        auto& output = renderOutputQueue_[outputWrite];
        std::fill(output.audio.begin(), output.audio.end(), 0.0F);
        if (routedAudioGraph_)
            routedAudioGraph_->setProcessContext(input.context);
        const auto started = std::chrono::steady_clock::now();
        std::size_t outputMidiCount = 0;
        const bool processed = processGraphBlock(
            input.channels.data(), output.channels.data(),
            graphChannels_, graphFrames_, input.midi.data(),
            input.midiCount, output.midi.data(), outputMidiCount);
        const auto elapsed = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - started)
                .count());
        renderTimeNanoseconds_.fetch_add(elapsed,
                                         std::memory_order_relaxed);
        timedRenderBlocks_.fetch_add(1, std::memory_order_relaxed);
        auto maximum =
            maximumRenderNanoseconds_.load(std::memory_order_relaxed);
        while (maximum < elapsed &&
               !maximumRenderNanoseconds_.compare_exchange_weak(
                   maximum, elapsed, std::memory_order_relaxed,
                   std::memory_order_relaxed)) {}
        if (!processed) {
            std::fill(output.audio.begin(), output.audio.end(), 0.0F);
            outputMidiCount = 0;
        }
        output.midiCount = outputMidiCount;
        output.endBeat = input.endBeat;
        output.sequence = input.sequence;
        renderInputRead_.store(
            (inputRead + 1) % renderQueueCapacity,
            std::memory_order_release);
        renderOutputWrite_.store(outputNext, std::memory_order_release);
        processedBlocks_.fetch_add(1, std::memory_order_relaxed);
    }
}

void AudioEngine::stopRenderWorker() noexcept {
    stopRenderWorker_.store(true, std::memory_order_release);
    if (renderWorker_.joinable()) renderWorker_.join();
}

void AudioEngine::handleMidi(const MidiEvent& event) noexcept {
    midiEvents_.fetch_add(1, std::memory_order_relaxed);
    if (midiEventCount_ < maxMidiEventsPerBlock)
        midiEventBuffer_[midiEventCount_++] = event;
}

void AudioEngine::handleXrun() noexcept {
    underruns_.fetch_add(1, std::memory_order_relaxed);
}

std::size_t AudioEngine::takeOutputMidi(MidiEvent* events,
                                        std::size_t capacity) noexcept {
    const auto count = std::min(capacity, midiOutputCount_);
    if (events) std::copy_n(midiOutputBuffer_.begin(), count, events);
    midiOutputCount_ = 0;
    return count;
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
