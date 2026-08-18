// native/include/transmission/MidiClipProcessor.h
#pragma once

#include "AudioDevice.h"
#include "AudioProcessor.h"
#include "SmfReader.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace transmission {

/** Loops a pre-loaded SMF file, emitting MIDI events from the transport beat position. */
class MidiClipProcessor final : public AudioProcessor {
public:
    /** Load from a MIDI file. Safe to call on the control thread before audio starts. */
    bool load(const std::string& path, std::string& error);

    std::uint16_t trackCount() const noexcept { return trackCount_; }
    double        lengthBeats() const noexcept { return lengthBeats_; }

    void process(const float* const* inputs, float* const* outputs,
                 std::size_t channels, std::size_t frames) noexcept override;
    void processWithMidi(const float* const* inputs, float* const* outputs,
                         std::size_t channels, std::size_t frames,
                         const MidiEvent* events, std::size_t eventCount) noexcept override;
    std::size_t takeOutputMidi(MidiEvent* events, std::size_t capacity) noexcept override;
    void setProcessContext(const AudioProcessContext& context) noexcept override;
    bool reconfigure(std::size_t frames, std::string&) override;

private:
    void collectEvents(double blockStart, double blockEnd, std::size_t frames) noexcept;

    std::vector<SmfEvent> events_;
    double lengthBeats_ = 16.0;
    std::uint16_t trackCount_ = 0;
    double sampleRate_ = 48000.0;
    AudioProcessContext context_;
    std::array<MidiEvent, maxMidiEventsPerBlock> outputBuffer_{};
    std::size_t outputCount_ = 0;
    std::size_t blockFrames_ = 256;
};

} // namespace transmission
