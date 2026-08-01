#pragma once

#include "AudioProcessor.h"

#include <atomic>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace transmission {

struct ProcessorTiming {
    std::string nodeId;
    std::uint64_t calls = 0;
    double averageMicroseconds = 0.0;
    double maximumMicroseconds = 0.0;
};

struct NodeProcessorState {
    std::string nodeId;
    ProcessorState state;
};

struct ScheduledMidiEvent {
    std::string targetNodeId;
    double beat = 0.0;
    std::array<std::uint8_t, 3> data{};
};

struct MidiParameterMapping {
    std::string targetNodeId;
    std::uint32_t parameterId = 0;
    int channel = -1;
    std::uint8_t controller = 0;
    bool consume = true;
};

/**
 * Preallocated DAG runtime. Control-plane code creates nodes and connects
 * them; prepare() resolves execution order and allocates all block storage.
 * The callback only mixes buffers and invokes processors.
 */
class RoutedAudioGraph {
public:
    static constexpr double scheduledInstrumentTailBeats = 8.0;

    RoutedAudioGraph() = default;
    ~RoutedAudioGraph();

    RoutedAudioGraph(const RoutedAudioGraph&) = delete;
    RoutedAudioGraph& operator=(const RoutedAudioGraph&) = delete;

    bool addNode(std::string id, std::unique_ptr<AudioProcessor> processor,
                 std::size_t audioInputs = 0, std::size_t audioOutputs = 0);
    bool connect(const std::string& from, const std::string& to);
    bool connect(const std::string& from, std::size_t fromPort,
                 const std::string& to, std::size_t toPort);
    bool connectMidi(const std::string& from, const std::string& to);
    bool setExternalAudioInput(const std::string& nodeId);
    bool setExternalAudioOutput(const std::string& nodeId);
    bool setExternalMidiInput(const std::string& nodeId, std::size_t port);
    bool setExternalMidiOutput(const std::string& nodeId, std::size_t port);
    bool setParameter(const std::string& nodeId, std::uint32_t parameterId,
                      double normalizedValue, std::string& error);
    bool enqueueParameter(const std::string& nodeId, std::uint32_t parameterId,
                          double normalizedValue) noexcept;
    /**
     * Schedule direct MIDI for instrument nodes. Inputless nodes driven only
     * by this schedule sleep before their first event and after the fixed tail
     * following their last event; audio-routed and live-MIDI nodes never sleep.
     */
    bool setScheduledMidiEvents(std::vector<ScheduledMidiEvent> events,
                                double sampleRate, std::string& error);
    bool setMidiParameterMappings(std::vector<MidiParameterMapping> mappings,
                                  std::string& error);
    bool prepare(std::size_t channels, std::size_t frames) noexcept;
    void process(const float* const* inputs, float* const* outputs,
                 std::size_t channels, std::size_t frames) noexcept;
    void processWithMidi(const float* const* inputs, float* const* outputs,
                         std::size_t channels, std::size_t frames,
                         const MidiEvent* events, std::size_t eventCount) noexcept;
    void setProcessContext(const AudioProcessContext& context) noexcept;
    std::size_t takeExternalMidiOutput(MidiEvent* events,
                                       std::size_t capacity) noexcept;
    /**
     * Copy the most recently processed node block for synchronous offline
     * diagnosis. The caller must not race this with process().
     */
    bool copyNodeAudioOutput(const std::string& nodeId, std::size_t channel,
                             float* samples, std::size_t frames) const noexcept;
    std::size_t copyNodeMidiOutput(const std::string& nodeId,
                                   MidiEvent* events,
                                   std::size_t capacity) const noexcept;
    void setTimingEnabled(bool enabled) noexcept { timingEnabled_ = enabled; }
    std::vector<ProcessorTiming> processorTimings() const;
    std::vector<NodeProcessorState> processorStates(std::string& error);
    bool restoreProcessorState(const std::string& nodeId,
                               const ProcessorState& state,
                               std::string& error);
    bool configureProcessingThreads(std::size_t requestedThreads);

    std::size_t nodeCount() const noexcept { return nodes_.size(); }
    std::size_t maximumParallelWidth() const noexcept;
    std::size_t processingThreadCount() const noexcept {
        return processingThreadCount_;
    }

private:
    struct AudioRoute {
        std::size_t node = 0;
        std::size_t fromPort = 0;
        std::size_t toPort = 0;
        bool allChannels = false;
    };

    struct Node {
        struct Timing {
            std::atomic<std::uint64_t> calls{0};
            std::atomic<std::uint64_t> totalNanoseconds{0};
            std::atomic<std::uint64_t> maximumNanoseconds{0};
        };

        std::string id;
        std::unique_ptr<AudioProcessor> processor;
        std::unique_ptr<Timing> timing = std::make_unique<Timing>();
        std::vector<AudioRoute> incoming;
        std::vector<AudioRoute> outgoing;
        std::vector<std::size_t> midiIncoming;
        std::vector<std::size_t> midiOutgoing;
        std::vector<float> input;
        std::vector<float> output;
        std::vector<const float*> inputPointers;
        std::vector<float*> outputPointers;
        std::array<MidiEvent, maxMidiEventsPerBlock> midiInput{};
        std::array<MidiEvent, maxMidiEventsPerBlock> midiOutput{};
        std::size_t midiInputCount = 0;
        std::size_t midiOutputCount = 0;
        std::size_t externalMidiInputPort = static_cast<std::size_t>(-1);
        std::size_t externalMidiOutputPort = static_cast<std::size_t>(-1);
        std::size_t audioInputs = 0;
        std::size_t audioOutputs = 0;
        std::size_t processingLane = 0;
        bool inheritDeviceChannels = false;
        bool externalAudioInput = false;
        bool externalAudioOutput = false;
        double scheduledMidiFirstBeat = 0.0;
        double scheduledMidiLastBeat = 0.0;
        bool hasScheduledMidi = false;
        bool sleepOutsideScheduledMidi = false;
        struct MidiMapping {
            std::uint32_t parameterId = 0;
            int channel = -1;
            std::uint8_t controller = 0;
            bool consume = true;
        };
        std::vector<MidiMapping> midiParameterMappings;
    };

    struct CompiledMidiEvent {
        std::size_t node = 0;
        double beat = 0.0;
        std::array<std::uint8_t, 3> data{};
    };

    void stopProcessingThreads() noexcept;
    void processingWorkerLoop(std::size_t lane) noexcept;
    void processNode(std::size_t index) noexcept;
    void routeNode(std::size_t index) noexcept;
    void processLevel(const std::vector<std::size_t>& level) noexcept;

    std::vector<Node> nodes_;
    std::vector<std::size_t> executionOrder_;
    std::vector<std::vector<std::size_t>> executionLevels_;
    std::vector<std::thread> processingWorkers_;
    std::atomic<bool> stopProcessingWorkers_{false};
    std::atomic<std::uint64_t> processingGeneration_{0};
    std::atomic<std::size_t> completedProcessingWorkers_{0};
    const std::vector<std::size_t>* activeProcessingLevel_ = nullptr;
    const float* const* currentInputs_ = nullptr;
    std::size_t currentChannels_ = 0;
    std::size_t currentFrames_ = 0;
    bool currentExplicitAudioInputs_ = false;
    std::size_t preparedChannels_ = 0;
    std::size_t preparedFrames_ = 0;
    std::size_t processingThreadCount_ = 1;
    std::array<MidiEvent, maxMidiEventsPerBlock> externalMidiOutput_{};
    std::size_t externalMidiOutputCount_ = 0;
    bool timingEnabled_ = false;
    std::vector<CompiledMidiEvent> scheduledMidiEvents_;
    double sampleRate_ = 48000.0;
    AudioProcessContext processContext_;
};

} // namespace transmission
