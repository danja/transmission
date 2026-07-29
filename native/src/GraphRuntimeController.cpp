#include "transmission/GraphRuntimeController.h"

namespace transmission {

GraphRuntimeController::GraphRuntimeController(RuntimeProcessorFactory processorFactory)
    : compiler_(std::move(processorFactory)) {}

GraphRuntimeController::~GraphRuntimeController() { stop(); }

bool GraphRuntimeController::start(const RuntimeGraphSnapshot& snapshot,
                                   AudioDevice& device,
                                   const AudioDeviceConfig& deviceConfig,
                                   const RuntimeTransportConfig& transportConfig,
                                   std::string& error) {
    stop();
    auto graph = compiler_.compile(snapshot, deviceConfig, error);
    if (!graph) return false;
    if (!engine_.configureDevice(device, deviceConfig)) {
        error = "unable to configure the audio device";
        return false;
    }
    if (!engine_.loadRuntimeGraph("{\"source\":\"native-gtk-compiler\",\"version\":1}") ||
        !engine_.setRoutedAudioGraph(std::move(graph), deviceConfig.channels,
                                     deviceConfig.blockSize)) {
        error = "unable to install the compiled runtime graph";
        return false;
    }
    engine_.seek(0.0);
    if (!engine_.setTempo(transportConfig.tempo, 0.0) ||
        !engine_.setLoop(transportConfig.loopStartBeat,
                         transportConfig.loopEndBeat,
                         transportConfig.loopEnabled)) {
        error = "unable to configure runtime transport";
        return false;
    }
    if (!engine_.start()) {
        error = "unable to start the native audio engine";
        return false;
    }
    return true;
}

void GraphRuntimeController::stop() noexcept { engine_.stop(); }

bool GraphRuntimeController::running() const {
    return engine_.diagnostics().running;
}

Diagnostics GraphRuntimeController::diagnostics() const {
    return engine_.diagnostics();
}

} // namespace transmission
