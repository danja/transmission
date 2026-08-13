#pragma once

#include "UiProjectCodec.h"
#include <cstdint>
#include <string>
#include <vector>

namespace transmission {

// Returns a Type-1 SMF buffer for the given arrangement clips and tempo.
std::vector<std::uint8_t> arrangementToSmf(
    const std::vector<UiProjectMidiClip>& clips, double bpm);

// Writes arrangement clips to a .mid file. Empty return = success.
std::string writeSmf(const std::string& path,
                     const std::vector<UiProjectMidiClip>& clips,
                     double bpm);

// A single MIDI event captured from the offline block loop.
struct CapturedMidiEvent {
    std::string nodeId;
    double beatPosition = 0.0;
    std::uint8_t status = 0;
    std::uint8_t data1 = 0;
    std::uint8_t data2 = 0;
};

// Writes captured runtime events to a .mid file (one track per source node).
// Empty return = success.
std::string writeCapturedSmf(const std::string& path,
                             const std::vector<CapturedMidiEvent>& events,
                             double bpm);

} // namespace transmission
