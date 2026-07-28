// native/include/transmission/AudioEngine.h

#pragma once

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>

#include "AudioDevice.h"
#include "AudioGraph.h"
#include "TransportClock.h"

namespace transmission {

struct Diagnostics {
    std::uint64_t underruns = 0;
    std::uint64_t processedBlocks = 0;
    std::uint64_t midiEvents = 0;
    std::uint64_t controlEventsDropped = 0;
    bool running = false;
    bool graphLoaded = false;
    double positionBeats = 0.0;
};

/** Control-plane lifecycle contract for the future VST3/JACK engine. */
class AudioEngine final : public AudioCallback {
public:
    AudioEngine() = default;
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    bool loadRuntimeGraph(std::string serializedGraph);
    std::string runtimeGraph() const;
    bool configureDevice(AudioDevice& device, const AudioDeviceConfig& config);
    bool setAudioGraph(std::unique_ptr<AudioGraph> graph,
                       std::size_t channels, std::size_t frames);
    bool start();
    void stop();
    bool setTempo(double bpm, double atBeat);
    bool setLoop(double startBeat, double endBeat, bool enabled);
    void seek(double beat);
    TransportAdvance advanceTransport(std::size_t frames) noexcept;
    Diagnostics diagnostics() const;
    void process(const float* const* inputs, float* const* outputs,
                 std::size_t channels, std::size_t frames) noexcept override;
    void handleMidi(const MidiEvent& event) noexcept override;

private:
    mutable std::mutex controlMutex_;
    std::string serializedGraph_;
    mutable Diagnostics diagnostics_;
    TransportClock transport_;
    AudioDevice* device_ = nullptr;
    AudioDeviceConfig deviceConfig_;
    std::unique_ptr<AudioGraph> audioGraph_;
    std::size_t graphChannels_ = 0;
    std::size_t graphFrames_ = 0;
    std::atomic<std::uint64_t> processedBlocks_{0};
    std::atomic<std::uint64_t> underruns_{0};
    std::atomic<std::uint64_t> midiEvents_{0};
    std::atomic<double> positionBeats_{0.0};
};

} // namespace transmission
