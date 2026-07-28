// native/tests/AudioEngineTest.cpp

#include "transmission/AudioEngine.h"

#include <cassert>

int main() {
    transmission::AudioEngine engine;
    assert(!engine.start());
    assert(!engine.loadRuntimeGraph(""));
    assert(engine.loadRuntimeGraph(R"({"version":1})"));
    assert(engine.start());
    assert(engine.diagnostics().running);
    assert(!engine.loadRuntimeGraph(R"({"version":1})"));
    engine.stop();
    assert(!engine.diagnostics().running);
    return 0;
}
