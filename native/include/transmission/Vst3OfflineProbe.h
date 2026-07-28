// native/include/transmission/Vst3OfflineProbe.h

#pragma once

#include <cstddef>
#include <string>

namespace transmission {

struct Vst3ProbeResult {
    std::string pluginName;
    std::string classId;
    std::size_t inputChannels = 0;
    std::size_t outputChannels = 0;
    std::size_t frames = 0;
    double inputRms = 0.0;
    double outputRms = 0.0;
};

/** Instantiates one VST3 audio effect and processes a deterministic offline block. */
class Vst3OfflineProbe {
public:
    bool process(const std::string& modulePath, Vst3ProbeResult& result,
                 std::string& error, std::size_t frames = 512,
                 double sampleRate = 48000.0) const;
};

} // namespace transmission
