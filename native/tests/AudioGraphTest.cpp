// native/tests/AudioGraphTest.cpp

#include "transmission/AudioGraph.h"

#include <cassert>
#include <memory>

int main() {
    transmission::AudioGraph graph;
    graph.addProcessor(std::make_unique<transmission::PassThroughProcessor>());
    graph.addProcessor(std::make_unique<transmission::PassThroughProcessor>());
    assert(graph.prepare(1, 4));

    constexpr float input[] = {0.25F, -0.5F, 0.75F, 1.0F};
    float output[] = {0.0F, 0.0F, 0.0F, 0.0F};
    const float* inputs[] = {input};
    float* outputs[] = {output};
    graph.process(inputs, outputs, 1, 4);
    for (int i = 0; i < 4; ++i) assert(output[i] == input[i]);
    return 0;
}
