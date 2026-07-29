#pragma once

#include "AudioProcessor.h"

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace transmission {

/**
 * Preallocated DAG runtime. Control-plane code creates nodes and connects
 * them; prepare() resolves execution order and allocates all block storage.
 * The callback only mixes buffers and invokes processors.
 */
class RoutedAudioGraph {
public:
    bool addNode(std::string id, std::unique_ptr<AudioProcessor> processor);
    bool connect(const std::string& from, const std::string& to);
    bool connectMidi(const std::string& from, const std::string& to);
    bool setExternalMidiInput(const std::string& nodeId, std::size_t port);
    bool setExternalMidiOutput(const std::string& nodeId, std::size_t port);
    bool setParameter(const std::string& nodeId, std::uint32_t parameterId,
                      double normalizedValue, std::string& error);
    bool enqueueParameter(const std::string& nodeId, std::uint32_t parameterId,
                          double normalizedValue) noexcept;
    bool prepare(std::size_t channels, std::size_t frames) noexcept;
    void process(const float* const* inputs, float* const* outputs,
                 std::size_t channels, std::size_t frames) noexcept;
    void processWithMidi(const float* const* inputs, float* const* outputs,
                         std::size_t channels, std::size_t frames,
                         const MidiEvent* events, std::size_t eventCount) noexcept;
    void setProcessContext(const AudioProcessContext& context) noexcept;
    std::size_t takeExternalMidiOutput(MidiEvent* events,
                                       std::size_t capacity) noexcept;

    std::size_t nodeCount() const noexcept { return nodes_.size(); }

private:
    struct Node {
        std::string id;
        std::unique_ptr<AudioProcessor> processor;
        std::vector<std::size_t> incoming;
        std::vector<std::size_t> outgoing;
        std::vector<std::size_t> midiIncoming;
        std::vector<std::size_t> midiOutgoing;
        std::vector<float> input;
        std::vector<float> output;
        std::vector<const float*> inputPointers;
        std::vector<float*> outputPointers;
        std::array<MidiEvent, maxMidiEventsPerBlock> midiInput{};
        std::array<MidiEvent, maxMidiEventsPerBlock> midiOutput{};
        std::size_t midiInputCount = 0;
        std::size_t externalMidiInputPort = static_cast<std::size_t>(-1);
        std::size_t externalMidiOutputPort = static_cast<std::size_t>(-1);
    };

    std::vector<Node> nodes_;
    std::vector<std::size_t> executionOrder_;
    std::size_t preparedChannels_ = 0;
    std::size_t preparedFrames_ = 0;
    std::array<MidiEvent, maxMidiEventsPerBlock> externalMidiOutput_{};
    std::size_t externalMidiOutputCount_ = 0;
};

} // namespace transmission
