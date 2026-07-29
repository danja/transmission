// native/tests/AudioEngineTest.cpp

#include "transmission/AudioEngine.h"
#include "transmission/FakeAudioDevice.h"

#include <cassert>

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
    return 0;
}
