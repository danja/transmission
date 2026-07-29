// native/include/transmission/AudioEngine.h

#pragma once

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <array>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "AudioDevice.h"
#include "AudioGraph.h"
#include "RoutedAudioGraph.h"
#include "TransportClock.h"

namespace transmission {

struct Diagnostics {
    std::uint64_t underruns = 0;
    std::uint64_t processedBlocks = 0;
    std::uint64_t midiEvents = 0;
    std::uint64_t controlEventsDropped = 0;
    std::uint64_t renderLateBlocks = 0;
    std::uint64_t renderQueueDrops = 0;
    std::size_t renderAheadBlocks = 0;
    std::size_t processingThreads = 1;
    double averageRenderMicroseconds = 0.0;
    double maximumRenderMicroseconds = 0.0;
    bool running = false;
    bool graphLoaded = false;
    double positionBeats = 0.0;
};

/** Control-plane lifecycle contract for the future VST3/JACK engine. */
class AudioEngine final : public AudioCallback {
public:
    AudioEngine() = default;
    ~AudioEngine();
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    bool loadRuntimeGraph(std::string serializedGraph);
    std::string runtimeGraph() const;
    bool configureDevice(AudioDevice& device, const AudioDeviceConfig& config);
    bool setAudioGraph(std::unique_ptr<AudioGraph> graph,
                       std::size_t channels, std::size_t frames);
    bool setRoutedAudioGraph(std::unique_ptr<RoutedAudioGraph> graph,
                             std::size_t channels, std::size_t frames);
    bool setSampleRate(double sampleRate);
    bool setRenderAheadBlocks(std::size_t blocks);
    bool setProcessingThreadCount(std::size_t threads);
    bool setParameter(const std::string& nodeId, std::uint32_t parameterId,
                      double normalizedValue, std::string& error);
    bool start();
    void stop();
    bool setTempo(double bpm, double atBeat);
    bool setLoop(double startBeat, double endBeat, bool enabled);
    void seek(double beat);
    TransportAdvance advanceTransport(std::size_t frames) noexcept;
    Diagnostics diagnostics() const;
    std::vector<ProcessorTiming> processorTimings() const;
    void process(const float* const* inputs, float* const* outputs,
                 std::size_t channels, std::size_t frames) noexcept override;
    void handleMidi(const MidiEvent& event) noexcept override;
    void handleXrun() noexcept override;
    std::size_t takeOutputMidi(MidiEvent* events,
                               std::size_t capacity) noexcept override;
    bool enqueueMidi(const MidiEvent& event) noexcept;

private:
    static constexpr std::size_t renderQueueCapacity = 64;
    static constexpr std::size_t maximumRenderAheadBlocks = 48;

    struct RenderInputBlock {
        std::vector<float> audio;
        std::vector<const float*> channels;
        std::array<MidiEvent, maxMidiEventsPerBlock> midi{};
        std::size_t midiCount = 0;
        AudioProcessContext context;
        double endBeat = 0.0;
        std::uint64_t sequence = 0;
    };

    struct RenderOutputBlock {
        std::vector<float> audio;
        std::vector<float*> channels;
        std::array<MidiEvent, maxMidiEventsPerBlock> midi{};
        std::size_t midiCount = 0;
        double endBeat = 0.0;
        std::uint64_t sequence = 0;
    };

    bool prepareRenderBuffers();
    bool processGraphBlock(const float* const* inputs, float* const* outputs,
                           std::size_t channels, std::size_t frames,
                           const MidiEvent* midi, std::size_t midiCount,
                           MidiEvent* outputMidi,
                           std::size_t& outputMidiCount) noexcept;
    void processDirect(const float* const* inputs, float* const* outputs,
                       std::size_t channels, std::size_t frames) noexcept;
    void processBuffered(const float* const* inputs, float* const* outputs,
                         std::size_t channels, std::size_t frames) noexcept;
    void renderWorkerLoop() noexcept;
    void stopRenderWorker() noexcept;
    void drainControlMidi() noexcept;

    mutable std::mutex controlMutex_;
    std::string serializedGraph_;
    mutable Diagnostics diagnostics_;
    TransportClock transport_;
    AudioDevice* device_ = nullptr;
    AudioDeviceConfig deviceConfig_;
    std::unique_ptr<AudioGraph> audioGraph_;
    std::unique_ptr<RoutedAudioGraph> routedAudioGraph_;
    std::size_t graphChannels_ = 0;
    std::size_t graphFrames_ = 0;
    std::atomic<std::uint64_t> processedBlocks_{0};
    std::atomic<std::uint64_t> underruns_{0};
    std::atomic<std::uint64_t> midiEvents_{0};
    std::atomic<std::uint64_t> renderLateBlocks_{0};
    std::atomic<std::uint64_t> renderQueueDrops_{0};
    std::atomic<std::uint64_t> renderTimeNanoseconds_{0};
    std::atomic<std::uint64_t> maximumRenderNanoseconds_{0};
    std::atomic<std::uint64_t> timedRenderBlocks_{0};
    std::atomic<double> positionBeats_{0.0};
    std::array<MidiEvent, maxMidiEventsPerBlock> midiEventBuffer_{};
    std::size_t midiEventCount_ = 0;
    std::array<MidiEvent, maxMidiEventsPerBlock> midiOutputBuffer_{};
    std::size_t midiOutputCount_ = 0;
    std::array<MidiEvent, maxMidiEventsPerBlock> midiControlQueue_{};
    std::atomic<std::size_t> midiQueueRead_{0};
    std::atomic<std::size_t> midiQueueWrite_{0};
    std::array<RenderInputBlock, renderQueueCapacity> renderInputQueue_;
    std::array<RenderOutputBlock, renderQueueCapacity> renderOutputQueue_;
    std::atomic<std::size_t> renderInputRead_{0};
    std::atomic<std::size_t> renderInputWrite_{0};
    std::atomic<std::size_t> renderOutputRead_{0};
    std::atomic<std::size_t> renderOutputWrite_{0};
    std::atomic<bool> stopRenderWorker_{false};
    std::thread renderWorker_;
    std::size_t renderAheadBlocks_ = 0;
    std::size_t requestedProcessingThreads_ = 0;
    std::uint64_t callbackSequence_ = 0;
};

} // namespace transmission
