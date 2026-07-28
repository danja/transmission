#include "transmission/RoutedAudioGraph.h"

#include <algorithm>
#include <cstring>

namespace transmission {

bool RoutedAudioGraph::addNode(std::string id, std::unique_ptr<AudioProcessor> processor) {
    if (id.empty() || !processor || preparedChannels_ != 0 ||
        std::any_of(nodes_.begin(), nodes_.end(), [&id](const auto& node) { return node.id == id; }))
        return false;
    Node node;
    node.id = std::move(id);
    node.processor = std::move(processor);
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
    if (std::find(source->outgoing.begin(), source->outgoing.end(), destinationIndex) != source->outgoing.end()) return false;
    source->outgoing.push_back(destinationIndex);
    destination->incoming.push_back(sourceIndex);
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
        std::vector<std::size_t> incoming(nodes_.size());
        std::vector<std::size_t> ready;
        for (std::size_t index = 0; index < nodes_.size(); ++index) {
            incoming[index] = nodes_[index].incoming.size();
            if (incoming[index] == 0) ready.push_back(index);
            nodes_[index].input.assign(channels * frames, 0.0F);
            nodes_[index].output.assign(channels * frames, 0.0F);
            nodes_[index].inputPointers.resize(channels);
            nodes_[index].outputPointers.resize(channels);
            for (std::size_t channel = 0; channel < channels; ++channel) {
                nodes_[index].inputPointers[channel] = nodes_[index].input.data() + channel * frames;
                nodes_[index].outputPointers[channel] = nodes_[index].output.data() + channel * frames;
            }
        }
        while (!ready.empty()) {
            const auto current = ready.front();
            ready.erase(ready.begin());
            executionOrder_.push_back(current);
            for (const auto next : nodes_[current].outgoing) {
                if (--incoming[next] == 0) ready.push_back(next);
            }
        }
        if (executionOrder_.size() != nodes_.size()) {
            executionOrder_.clear();
            return false;
        }
    } catch (...) {
        executionOrder_.clear();
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
    for (auto& node : nodes_) node.processor->applyPendingParameters();
    for (auto& node : nodes_) {
        std::fill(node.input.begin(), node.input.end(), 0.0F);
        std::fill(node.output.begin(), node.output.end(), 0.0F);
    }
    for (const auto index : executionOrder_) {
        auto& node = nodes_[index];
        if (node.incoming.empty()) {
            for (std::size_t channel = 0; channel < channels; ++channel)
                std::memcpy(node.input.data() + channel * frames, inputs[channel], frames * sizeof(float));
        }
        node.processor->processWithMidi(node.inputPointers.data(), node.outputPointers.data(),
                                        channels, frames, events, eventCount);
        for (const auto destination : node.outgoing) {
            for (std::size_t channel = 0; channel < channels; ++channel) {
                const auto* source = node.output.data() + channel * frames;
                auto* target = nodes_[destination].input.data() + channel * frames;
                for (std::size_t frame = 0; frame < frames; ++frame) target[frame] += source[frame];
            }
        }
    }
    for (std::size_t channel = 0; channel < channels; ++channel)
        std::fill(outputs[channel], outputs[channel] + frames, 0.0F);
    for (const auto& node : nodes_) {
        if (!node.outgoing.empty()) continue;
        for (std::size_t channel = 0; channel < channels; ++channel)
            for (std::size_t frame = 0; frame < frames; ++frame)
                outputs[channel][frame] += node.output[channel * frames + frame];
    }
}

} // namespace transmission
