#include "transmission/RoutedAudioGraph.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>

namespace transmission {

RoutedAudioGraph::~RoutedAudioGraph() { stopProcessingThreads(); }

bool RoutedAudioGraph::addNode(std::string id, std::unique_ptr<AudioProcessor> processor,
                               std::size_t audioInputs, std::size_t audioOutputs) {
    if (id.empty() || !processor || preparedChannels_ != 0 ||
        std::any_of(nodes_.begin(), nodes_.end(), [&id](const auto& node) { return node.id == id; }))
        return false;
    Node node;
    node.id = std::move(id);
    node.processor = std::move(processor);
    node.audioInputs = audioInputs;
    node.audioOutputs = audioOutputs;
    node.inheritDeviceChannels = audioInputs == 0 && audioOutputs == 0;
    nodes_.push_back(std::move(node));
    return true;
}

bool RoutedAudioGraph::connect(const std::string& from, const std::string& to) {
    if (preparedChannels_ != 0 || from == to) return false;
    auto source = std::find_if(nodes_.begin(), nodes_.end(), [&from](const auto& node) { return node.id == from; });
    auto destination = std::find_if(nodes_.begin(), nodes_.end(), [&to](const auto& node) { return node.id == to; });
    if (source == nodes_.end() || destination == nodes_.end()) return false;
    const auto sourceIndex = static_cast<std::size_t>(source - nodes_.begin());
    const auto destinationIndex = static_cast<std::size_t>(destination - nodes_.begin());
    if (std::any_of(source->outgoing.begin(), source->outgoing.end(),
                    [destinationIndex](const auto& route) {
                        return route.node == destinationIndex && route.allChannels;
                    })) return false;
    source->outgoing.push_back({destinationIndex, 0, 0, true});
    destination->incoming.push_back({sourceIndex, 0, 0, true});
    return true;
}

bool RoutedAudioGraph::connect(const std::string& from, std::size_t fromPort,
                               const std::string& to, std::size_t toPort) {
    if (preparedChannels_ != 0 || from == to) return false;
    auto source = std::find_if(nodes_.begin(), nodes_.end(),
                               [&from](const auto& node) { return node.id == from; });
    auto destination = std::find_if(nodes_.begin(), nodes_.end(),
                                    [&to](const auto& node) { return node.id == to; });
    if (source == nodes_.end() || destination == nodes_.end() ||
        (!source->inheritDeviceChannels && fromPort >= source->audioOutputs) ||
        (!destination->inheritDeviceChannels && toPort >= destination->audioInputs))
        return false;
    const auto sourceIndex = static_cast<std::size_t>(source - nodes_.begin());
    const auto destinationIndex = static_cast<std::size_t>(destination - nodes_.begin());
    if (std::any_of(source->outgoing.begin(), source->outgoing.end(),
                    [&](const auto& route) {
                        return route.node == destinationIndex && !route.allChannels &&
                               route.fromPort == fromPort && route.toPort == toPort;
                    })) return false;
    source->outgoing.push_back({destinationIndex, fromPort, toPort, false});
    destination->incoming.push_back({sourceIndex, fromPort, toPort, false});
    return true;
}

bool RoutedAudioGraph::setExternalAudioInput(const std::string& nodeId) {
    if (preparedChannels_ != 0) return false;
    auto node = std::find_if(nodes_.begin(), nodes_.end(),
                             [&nodeId](const auto& candidate) { return candidate.id == nodeId; });
    if (node == nodes_.end()) return false;
    node->externalAudioInput = true;
    return true;
}

bool RoutedAudioGraph::setExternalAudioOutput(const std::string& nodeId) {
    if (preparedChannels_ != 0) return false;
    auto node = std::find_if(nodes_.begin(), nodes_.end(),
                             [&nodeId](const auto& candidate) { return candidate.id == nodeId; });
    if (node == nodes_.end()) return false;
    node->externalAudioOutput = true;
    return true;
}

bool RoutedAudioGraph::connectMidi(const std::string& from, const std::string& to) {
    if (preparedChannels_ != 0 || from == to) return false;
    auto source = std::find_if(nodes_.begin(), nodes_.end(),
                               [&from](const auto& node) { return node.id == from; });
    auto destination = std::find_if(nodes_.begin(), nodes_.end(),
                                    [&to](const auto& node) { return node.id == to; });
    if (source == nodes_.end() || destination == nodes_.end()) return false;
    const auto sourceIndex = static_cast<std::size_t>(source - nodes_.begin());
    const auto destinationIndex = static_cast<std::size_t>(destination - nodes_.begin());
    if (std::find(source->midiOutgoing.begin(), source->midiOutgoing.end(),
                  destinationIndex) != source->midiOutgoing.end()) return false;
    source->midiOutgoing.push_back(destinationIndex);
    destination->midiIncoming.push_back(sourceIndex);
    return true;
}

bool RoutedAudioGraph::setExternalMidiInput(const std::string& nodeId,
                                            std::size_t port) {
    if (preparedChannels_ != 0) return false;
    auto node = std::find_if(nodes_.begin(), nodes_.end(),
                             [&nodeId](const auto& candidate) { return candidate.id == nodeId; });
    if (node == nodes_.end()) return false;
    node->externalMidiInputPort = port;
    return true;
}

bool RoutedAudioGraph::setExternalMidiOutput(const std::string& nodeId,
                                             std::size_t port) {
    if (preparedChannels_ != 0) return false;
    auto node = std::find_if(nodes_.begin(), nodes_.end(),
                             [&nodeId](const auto& candidate) { return candidate.id == nodeId; });
    if (node == nodes_.end()) return false;
    node->externalMidiOutputPort = port;
    return true;
}

bool RoutedAudioGraph::setParameter(const std::string& nodeId, std::uint32_t parameterId,
                                    double normalizedValue, std::string& error) {
    auto node = std::find_if(nodes_.begin(), nodes_.end(),
                             [&nodeId](const auto& candidate) { return candidate.id == nodeId; });
    if (node == nodes_.end()) {
        error = "native graph node does not exist";
        return false;
    }
    return node->processor->setParameter(parameterId, normalizedValue, error);
}

bool RoutedAudioGraph::enqueueParameter(const std::string& nodeId, std::uint32_t parameterId,
                                        double normalizedValue) noexcept {
    auto node = std::find_if(nodes_.begin(), nodes_.end(),
                             [&nodeId](const auto& candidate) { return candidate.id == nodeId; });
    return node != nodes_.end() &&
           node->processor->enqueueParameter(parameterId, normalizedValue);
}

bool RoutedAudioGraph::prepare(std::size_t channels, std::size_t frames) noexcept {
    if (channels == 0 || frames == 0 || nodes_.empty()) return false;
    try {
        executionOrder_.clear();
        executionLevels_.clear();
        std::vector<std::size_t> incoming(nodes_.size());
        std::vector<std::vector<std::size_t>> dependencies(nodes_.size());
        std::vector<std::size_t> ready;
        for (std::size_t index = 0; index < nodes_.size(); ++index) {
            for (const auto& route : nodes_[index].outgoing)
                if (std::find(dependencies[index].begin(), dependencies[index].end(),
                              route.node) == dependencies[index].end())
                    dependencies[index].push_back(route.node);
            for (const auto destination : nodes_[index].midiOutgoing)
                if (std::find(dependencies[index].begin(), dependencies[index].end(),
                              destination) == dependencies[index].end())
                    dependencies[index].push_back(destination);
            for (const auto destination : dependencies[index]) ++incoming[destination];
            if (nodes_[index].inheritDeviceChannels) {
                nodes_[index].audioInputs = channels;
                nodes_[index].audioOutputs = channels;
            }
            nodes_[index].input.assign(nodes_[index].audioInputs * frames, 0.0F);
            nodes_[index].output.assign(nodes_[index].audioOutputs * frames, 0.0F);
            nodes_[index].inputPointers.resize(nodes_[index].audioInputs);
            nodes_[index].outputPointers.resize(nodes_[index].audioOutputs);
            for (std::size_t channel = 0; channel < nodes_[index].audioInputs; ++channel)
                nodes_[index].inputPointers[channel] = nodes_[index].input.data() + channel * frames;
            for (std::size_t channel = 0; channel < nodes_[index].audioOutputs; ++channel)
                nodes_[index].outputPointers[channel] = nodes_[index].output.data() + channel * frames;
        }
        for (std::size_t index = 0; index < nodes_.size(); ++index)
            if (incoming[index] == 0) ready.push_back(index);
        while (!ready.empty()) {
            executionLevels_.push_back(ready);
            std::vector<std::size_t> nextLevel;
            for (const auto current : ready) {
                executionOrder_.push_back(current);
                for (const auto next : dependencies[current]) {
                    if (--incoming[next] == 0)
                        nextLevel.push_back(next);
                }
            }
            ready = std::move(nextLevel);
        }
        if (executionOrder_.size() != nodes_.size()) {
            executionOrder_.clear();
            executionLevels_.clear();
            return false;
        }
    } catch (...) {
        executionOrder_.clear();
        executionLevels_.clear();
        for (auto& node : nodes_) {
            node.input.clear();
            node.output.clear();
            node.inputPointers.clear();
            node.outputPointers.clear();
        }
        return false;
    }
    preparedChannels_ = channels;
    preparedFrames_ = frames;
    return true;
}

void RoutedAudioGraph::process(const float* const* inputs, float* const* outputs,
                               std::size_t channels, std::size_t frames) noexcept {
    processWithMidi(inputs, outputs, channels, frames, nullptr, 0);
}

void RoutedAudioGraph::processWithMidi(const float* const* inputs, float* const* outputs,
                                       std::size_t channels, std::size_t frames,
                                       const MidiEvent* events, std::size_t eventCount) noexcept {
    if (!inputs || !outputs || channels != preparedChannels_ || frames != preparedFrames_) return;
    externalMidiOutputCount_ = 0;
    const bool explicitAudioInputs = std::any_of(
        nodes_.begin(), nodes_.end(), [](const auto& node) { return node.externalAudioInput; });
    const bool explicitAudioOutputs = std::any_of(
        nodes_.begin(), nodes_.end(), [](const auto& node) { return node.externalAudioOutput; });
    for (auto& node : nodes_) {
        node.processor->applyPendingParameters();
        std::fill(node.input.begin(), node.input.end(), 0.0F);
        std::fill(node.output.begin(), node.output.end(), 0.0F);
        node.midiInputCount = 0;
        if (node.externalMidiInputPort != static_cast<std::size_t>(-1) && events) {
            for (std::size_t event = 0;
                 event < eventCount && node.midiInputCount < node.midiInput.size();
                 ++event) {
                if (events[event].port == node.externalMidiInputPort)
                    node.midiInput[node.midiInputCount++] = events[event];
            }
        }
    }
    currentInputs_ = inputs;
    currentChannels_ = channels;
    currentFrames_ = frames;
    currentExplicitAudioInputs_ = explicitAudioInputs;
    for (const auto& level : executionLevels_) processLevel(level);
    for (std::size_t channel = 0; channel < channels; ++channel)
        std::fill(outputs[channel], outputs[channel] + frames, 0.0F);
    for (const auto& node : nodes_) {
        if (!node.externalAudioOutput &&
            (explicitAudioOutputs || !node.outgoing.empty())) continue;
        for (std::size_t channel = 0;
             channel < std::min(channels, node.audioOutputs); ++channel)
            for (std::size_t frame = 0; frame < frames; ++frame)
                outputs[channel][frame] += node.output[channel * frames + frame];
    }
}

void RoutedAudioGraph::processNode(std::size_t index) noexcept {
    auto& node = nodes_[index];
    if (node.externalAudioInput ||
        (!currentExplicitAudioInputs_ && node.incoming.empty())) {
        for (std::size_t channel = 0;
             channel < std::min(currentChannels_, node.audioInputs);
             ++channel)
            std::memcpy(
                node.input.data() + channel * currentFrames_,
                currentInputs_[channel],
                currentFrames_ * sizeof(float));
    }
    if (timingEnabled_) {
        const auto started = std::chrono::steady_clock::now();
        node.processor->processWithMidi(
            node.inputPointers.data(), node.audioInputs,
            node.outputPointers.data(), node.audioOutputs, currentFrames_,
            node.midiInput.data(), node.midiInputCount);
        const auto elapsed = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - started)
                .count());
        node.timing->calls.fetch_add(1, std::memory_order_relaxed);
        node.timing->totalNanoseconds.fetch_add(
            elapsed, std::memory_order_relaxed);
        auto maximum = node.timing->maximumNanoseconds.load(
            std::memory_order_relaxed);
        while (maximum < elapsed &&
               !node.timing->maximumNanoseconds.compare_exchange_weak(
                   maximum, elapsed, std::memory_order_relaxed,
                   std::memory_order_relaxed)) {}
    } else {
        node.processor->processWithMidi(
            node.inputPointers.data(), node.audioInputs,
            node.outputPointers.data(), node.audioOutputs, currentFrames_,
            node.midiInput.data(), node.midiInputCount);
    }
    node.midiOutputCount = node.processor->takeOutputMidi(
        node.midiOutput.data(), node.midiOutput.size());
}

void RoutedAudioGraph::routeNode(std::size_t index) noexcept {
    auto& node = nodes_[index];
    if (node.externalMidiOutputPort != static_cast<std::size_t>(-1)) {
        const auto available =
            externalMidiOutput_.size() - externalMidiOutputCount_;
        const auto copied = std::min(available, node.midiOutputCount);
        for (std::size_t event = 0; event < copied; ++event) {
            auto outputEvent = node.midiOutput[event];
            outputEvent.port = node.externalMidiOutputPort;
            externalMidiOutput_[externalMidiOutputCount_++] = outputEvent;
        }
    }
    for (const auto destination : node.midiOutgoing) {
        auto& target = nodes_[destination];
        const auto available =
            target.midiInput.size() - target.midiInputCount;
        const auto copied =
            std::min(available, node.midiOutputCount);
        std::copy_n(
            node.midiOutput.begin(), copied,
            target.midiInput.begin() +
                static_cast<std::ptrdiff_t>(target.midiInputCount));
        target.midiInputCount += copied;
    }
    for (const auto& route : node.outgoing) {
        auto& destination = nodes_[route.node];
        const auto routeCount = route.allChannels
            ? std::min(node.audioOutputs, destination.audioInputs)
            : std::size_t{1};
        for (std::size_t offset = 0; offset < routeCount; ++offset) {
            const auto sourcePort =
                route.allChannels ? offset : route.fromPort;
            const auto targetPort =
                route.allChannels ? offset : route.toPort;
            if (sourcePort >= node.audioOutputs ||
                targetPort >= destination.audioInputs)
                continue;
            const auto* source =
                node.output.data() + sourcePort * currentFrames_;
            auto* target =
                destination.input.data() + targetPort * currentFrames_;
            for (std::size_t frame = 0; frame < currentFrames_; ++frame)
                target[frame] += source[frame];
        }
    }
}

void RoutedAudioGraph::processLevel(
    const std::vector<std::size_t>& level) noexcept {
    if (processingWorkers_.empty() || level.size() == 1) {
        for (const auto index : level) processNode(index);
    } else {
        activeProcessingLevel_ = &level;
        completedProcessingWorkers_.store(0, std::memory_order_relaxed);
        processingGeneration_.fetch_add(1, std::memory_order_release);
        for (const auto index : level)
            if (nodes_[index].processingLane == 0)
                processNode(index);
        while (completedProcessingWorkers_.load(
                   std::memory_order_acquire) <
               processingWorkers_.size())
            std::this_thread::yield();
    }
    for (const auto index : level) routeNode(index);
}

std::size_t RoutedAudioGraph::maximumParallelWidth() const noexcept {
    std::size_t result = 1;
    for (const auto& level : executionLevels_)
        result = std::max(result, level.size());
    return result;
}

bool RoutedAudioGraph::configureProcessingThreads(
    std::size_t requestedThreads) {
    stopProcessingThreads();
    const auto effective = std::max<std::size_t>(
        1, std::min(requestedThreads, maximumParallelWidth()));
    processingThreadCount_ = effective;
    processingGeneration_.store(0, std::memory_order_relaxed);
    completedProcessingWorkers_.store(0, std::memory_order_relaxed);
    activeProcessingLevel_ = nullptr;
    for (const auto& level : executionLevels_)
        for (std::size_t position = 0; position < level.size(); ++position)
            nodes_[level[position]].processingLane =
                position % effective;
    stopProcessingWorkers_.store(false, std::memory_order_release);
    try {
        processingWorkers_.reserve(effective - 1);
        for (std::size_t index = 1; index < effective; ++index)
            processingWorkers_.emplace_back(
                &RoutedAudioGraph::processingWorkerLoop, this, index);
    } catch (...) {
        stopProcessingThreads();
        processingThreadCount_ = 1;
        return false;
    }
    return true;
}

void RoutedAudioGraph::stopProcessingThreads() noexcept {
    stopProcessingWorkers_.store(true, std::memory_order_release);
    for (auto& worker : processingWorkers_)
        if (worker.joinable()) worker.join();
    processingWorkers_.clear();
    processingThreadCount_ = 1;
}

void RoutedAudioGraph::processingWorkerLoop(std::size_t lane) noexcept {
    std::uint64_t observedGeneration = 0;
    while (!stopProcessingWorkers_.load(std::memory_order_acquire)) {
        const auto generation =
            processingGeneration_.load(std::memory_order_acquire);
        if (generation == observedGeneration) {
            std::this_thread::sleep_for(std::chrono::microseconds(50));
            continue;
        }
        observedGeneration = generation;
        if (stopProcessingWorkers_.load(std::memory_order_acquire)) break;
        const auto* level = activeProcessingLevel_;
        if (level)
            for (const auto index : *level)
                if (nodes_[index].processingLane == lane)
                    processNode(index);
        completedProcessingWorkers_.fetch_add(
            1, std::memory_order_release);
    }
}

std::size_t RoutedAudioGraph::takeExternalMidiOutput(
    MidiEvent* events, std::size_t capacity) noexcept {
    const auto count = std::min(capacity, externalMidiOutputCount_);
    if (events) std::copy_n(externalMidiOutput_.begin(), count, events);
    externalMidiOutputCount_ = 0;
    return count;
}

std::vector<ProcessorTiming> RoutedAudioGraph::processorTimings() const {
    std::vector<ProcessorTiming> result;
    result.reserve(nodes_.size());
    for (const auto& node : nodes_) {
        const auto calls =
            node.timing->calls.load(std::memory_order_acquire);
        const auto total =
            node.timing->totalNanoseconds.load(std::memory_order_acquire);
        const auto maximum =
            node.timing->maximumNanoseconds.load(std::memory_order_acquire);
        result.push_back({
            node.id,
            calls,
            calls == 0
                ? 0.0
                : static_cast<double>(total) /
                      static_cast<double>(calls) / 1000.0,
            static_cast<double>(maximum) / 1000.0});
    }
    return result;
}

std::vector<NodeProcessorState> RoutedAudioGraph::processorStates(
    std::string& error) {
    std::vector<NodeProcessorState> result;
    result.reserve(nodes_.size());
    for (auto& node : nodes_) {
        ProcessorState state;
        if (!node.processor->captureState(state, error)) {
            if (error.empty())
                error = "unable to capture processor state: " + node.id;
            return {};
        }
        if (!state.component.empty() || !state.controller.empty())
            result.push_back({node.id, std::move(state)});
    }
    return result;
}

bool RoutedAudioGraph::restoreProcessorState(
    const std::string& nodeId, const ProcessorState& state,
    std::string& error) {
    auto node = std::find_if(
        nodes_.begin(), nodes_.end(),
        [&nodeId](const auto& candidate) { return candidate.id == nodeId; });
    if (node == nodes_.end()) {
        error = "native graph node does not exist";
        return false;
    }
    return node->processor->restoreState(state, error);
}

void RoutedAudioGraph::setProcessContext(const AudioProcessContext& context) noexcept {
    for (auto& node : nodes_) node.processor->setProcessContext(context);
}

} // namespace transmission
