#pragma once

#include "AudioDevice.h"

#include <atomic>
#include <string>
#include <vector>

struct _jack_client;
struct _jack_port;

namespace transmission {

/**
 * JACK device adapter. Client and port creation happen during configure();
 * the JACK process callback only obtains port buffers and invokes the engine
 * callback.
 */
class JackAudioDevice final : public AudioDevice {
public:
    JackAudioDevice() = default;
    ~JackAudioDevice() override;

    JackAudioDevice(const JackAudioDevice&) = delete;
    JackAudioDevice& operator=(const JackAudioDevice&) = delete;

    bool configure(const AudioDeviceConfig& config) override;
    bool start(AudioCallback& callback) override;
    void enableCallbacks(AudioCallback& callback) noexcept override;
    void stop() noexcept override;
    std::size_t actualBlockSize() const noexcept override;

    const std::string& lastError() const noexcept { return lastError_; }
    bool connectPhysicalPorts();

private:
    static int processThunk(unsigned frames, void* opaque) noexcept;
    static int bufferSizeThunk(unsigned frames, void* opaque) noexcept;
    static int xrunThunk(void* opaque) noexcept;
    int process(unsigned frames) noexcept;
    void closeClient() noexcept;

    _jack_client* client_ = nullptr;
    AudioDeviceConfig config_;
    std::vector<_jack_port*> inputPorts_;
    std::vector<_jack_port*> outputPorts_;
    std::vector<_jack_port*> midiInputPorts_;
    std::vector<_jack_port*> midiOutputPorts_;
    std::vector<const float*> inputPointers_;
    std::vector<float*> outputPointers_;
    std::array<MidiEvent, maxMidiEventsPerBlock> midiOutputEvents_{};
    std::atomic<AudioCallback*> callback_{nullptr};
    std::atomic<bool> running_{false};
    std::string lastError_;
    bool configured_ = false;
};

} // namespace transmission
