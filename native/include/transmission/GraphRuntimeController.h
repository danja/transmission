#pragma once

#include "AudioEngine.h"
#include "GraphRuntimeCompiler.h"

namespace transmission {

struct RuntimeTransportConfig {
    double tempo = 120.0;
    double loopStartBeat = 0.0;
    double loopEndBeat = 16.0;
    bool loopEnabled = false;
};

/** Owns control-thread compile/configure/start sequencing for one live graph. */
class GraphRuntimeController {
public:
    explicit GraphRuntimeController(RuntimeProcessorFactory processorFactory);
    ~GraphRuntimeController();

    bool start(const RuntimeGraphSnapshot& snapshot, AudioDevice& device,
               const AudioDeviceConfig& deviceConfig,
               const RuntimeTransportConfig& transportConfig,
               std::string& error);
    void stop() noexcept;
    bool setParameter(const std::string& nodeId, std::uint32_t parameterId,
                      double normalizedValue, std::string& error);
    bool setRenderAheadBlocks(std::size_t blocks);
    bool setProcessingThreadCount(std::size_t threads);
    bool running() const;
    Diagnostics diagnostics() const;
    std::vector<ProcessorTiming> processorTimings() const;

private:
    GraphRuntimeCompiler compiler_;
    AudioEngine engine_;
};

} // namespace transmission
