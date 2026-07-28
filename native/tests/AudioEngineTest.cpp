// native/tests/AudioEngineTest.cpp

#include "transmission/AudioEngine.h"

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
    return 0;
}
