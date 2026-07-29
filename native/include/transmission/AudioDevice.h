// native/include/transmission/AudioDevice.h

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace transmission {

inline constexpr std::size_t maxMidiEventsPerBlock = 256;

struct AudioDeviceConfig {
    std::size_t channels = 2;
    std::size_t blockSize = 256;
    double sampleRate = 48000.0;
    bool autoConnect = false;
    std::size_t midiInputs = 1;
};

struct MidiEvent {
    std::size_t frameOffset = 0;
    std::uint8_t size = 0;
    std::array<std::uint8_t, 3> data{};
};

class AudioCallback {
public:
    virtual ~AudioCallback() = default;
    virtual void process(const float* const* inputs, float* const* outputs,
                         std::size_t channels, std::size_t frames) noexcept = 0;
    virtual void handleMidi(const MidiEvent& /*event*/) noexcept {}
};

/** Device lifecycle abstraction; concrete JACK/PipeWire code stays behind it. */
class AudioDevice {
public:
    virtual ~AudioDevice() = default;
    virtual bool configure(const AudioDeviceConfig& config) = 0;
    virtual bool start(AudioCallback& callback) = 0;
    virtual void stop() noexcept = 0;
};

} // namespace transmission
