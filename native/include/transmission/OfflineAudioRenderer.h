#pragma once

#include "GraphRuntimeCompiler.h"

#include <cstdint>
#include <functional>
#include <string>

namespace transmission {

struct OfflineRenderOptions {
    std::string outputPath;
    std::size_t channels = 2;
    std::size_t blockSize = 1024;
    double sampleRate = 48000.0;
    std::uint64_t totalFrames = 0;
    double tempo = 120.0;
    double loopStartBeat = 0.0;
    double loopEndBeat = 16.0;
    bool loopEnabled = false;
    /** Zero selects a hardware- and graph-appropriate thread count. */
    std::size_t processingThreads = 0;
};

struct OfflineRenderResult {
    std::uint64_t framesWritten = 0;
    float peak = 0.0F;
};

using OfflineRenderProgress = std::function<bool(double)>;

/**
 * Compiles a runtime snapshot and writes its system output as stereo IEEE-float
 * WAV without involving an audio device. The progress callback runs on the
 * caller's thread and may return false to cancel.
 */
class OfflineAudioRenderer {
public:
    explicit OfflineAudioRenderer(RuntimeProcessorFactory processorFactory);

    bool renderWave(const RuntimeGraphSnapshot& snapshot,
                    const OfflineRenderOptions& options,
                    OfflineRenderResult& result,
                    std::string& error,
                    OfflineRenderProgress progress = {}) const;

private:
    GraphRuntimeCompiler compiler_;
};

} // namespace transmission
