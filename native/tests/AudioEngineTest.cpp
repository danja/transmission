// native/tests/AudioEngineTest.cpp

#include "transmission/AudioEngine.h"
#include "transmission/FakeAudioDevice.h"

#include <cassert>
#include <chrono>
#include <thread>

int main() {
    transmission::AudioEngine engine;
    assert(!engine.start());
    assert(!engine.loadRuntimeGraph(""));
    assert(engine.loadRuntimeGraph(R"({"version":1})"));
    assert(engine.setTempo(90.0, 0.0));
    assert(engine.setLoop(0.0, 4.0, true));
    engine.seek(2.0);
    assert(engine.diagnostics().positionBeats == 2.0);
    assert(engine.diagnostics().graphLoaded);
    assert(engine.runtimeGraph() == R"({"version":1})");
    assert(engine.start());
    assert(engine.diagnostics().running);
    assert(!engine.setTempo(120.0, 2.0));
    assert(engine.advanceTransport(24000).endBeat > 2.0);
    assert(!engine.loadRuntimeGraph(R"({"version":1})"));
    engine.stop();
    assert(!engine.diagnostics().running);

    transmission::FakeAudioDevice device;
    assert(engine.configureDevice(device, {1, 4, 48000.0}));
    auto graph = std::make_unique<transmission::AudioGraph>();
    graph->addProcessor(std::make_unique<transmission::PassThroughProcessor>());
    assert(engine.setAudioGraph(std::move(graph), 1, 4));
    assert(engine.start());
    const float input[] = {1.0F, 2.0F, 3.0F, 4.0F};
    float output[] = {0.0F, 0.0F, 0.0F, 0.0F};
    const float* inputs[] = {input};
    float* outputs[] = {output};
    assert(device.render(inputs, outputs));
    assert(output[3] == 4.0F);
    assert(engine.diagnostics().processedBlocks == 1);
    transmission::MidiEvent noteOn;
    noteOn.size = 3;
    noteOn.data = {0x90, 60, 100};
    engine.handleMidi(noteOn);
    assert(engine.diagnostics().midiEvents == 1);
    engine.handleXrun();
    assert(engine.diagnostics().underruns == 1);
    engine.stop();

    transmission::AudioEngine buffered;
    transmission::FakeAudioDevice bufferedDevice;
    assert(buffered.loadRuntimeGraph(R"({"version":1})"));
    assert(!buffered.setRenderAheadBlocks(49));
    assert(buffered.setRenderAheadBlocks(1));
    assert(buffered.setProcessingThreadCount(2));
    assert(buffered.configureDevice(bufferedDevice, {1, 4, 48000.0}));
    auto bufferedGraph = std::make_unique<transmission::AudioGraph>();
    bufferedGraph->addProcessor(
        std::make_unique<transmission::PassThroughProcessor>());
    assert(buffered.setAudioGraph(std::move(bufferedGraph), 1, 4));
    assert(buffered.start());
    assert(!buffered.setRenderAheadBlocks(2));
    assert(!buffered.setProcessingThreadCount(1));
    float delayedOutput[] = {9.0F, 9.0F, 9.0F, 9.0F};
    float* delayedOutputs[] = {delayedOutput};
    assert(bufferedDevice.render(inputs, delayedOutputs));
    assert(delayedOutput[0] == 0.0F && delayedOutput[3] == 0.0F);
    for (int attempt = 0;
         attempt < 100 && buffered.diagnostics().processedBlocks == 0;
         ++attempt)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(buffered.diagnostics().processedBlocks == 1);
    const float silence[] = {0.0F, 0.0F, 0.0F, 0.0F};
    const float* silentInputs[] = {silence};
    assert(bufferedDevice.render(silentInputs, delayedOutputs));
    assert(delayedOutput[0] == input[0] && delayedOutput[3] == input[3]);
    const auto bufferedDiagnostics = buffered.diagnostics();
    assert(bufferedDiagnostics.renderAheadBlocks == 1);
    assert(bufferedDiagnostics.renderLateBlocks == 0);
    assert(bufferedDiagnostics.maximumRenderMicroseconds >= 0.0);
    buffered.stop();
    return 0;
}
