#include "transmission/RoutedAudioGraph.h"

#include <cassert>
#include <memory>

int main() {
    transmission::RoutedAudioGraph graph;
    assert(graph.addNode("source", std::make_unique<transmission::PassThroughProcessor>()));
    assert(graph.addNode("left", std::make_unique<transmission::PassThroughProcessor>()));
    assert(graph.addNode("right", std::make_unique<transmission::PassThroughProcessor>()));
    assert(graph.connect("source", "left"));
    assert(graph.connect("source", "right"));
    assert(!graph.connect("source", "right"));
    assert(graph.prepare(1, 4));

    constexpr float input[] = {0.25F, -0.5F, 0.75F, 1.0F};
    float output[] = {0.0F, 0.0F, 0.0F, 0.0F};
    const float* inputs[] = {input};
    float* outputs[] = {output};
    graph.process(inputs, outputs, 1, 4);
    for (int index = 0; index < 4; ++index) assert(output[index] == input[index] * 2.0F);

    transmission::RoutedAudioGraph cycle;
    assert(cycle.addNode("a", std::make_unique<transmission::PassThroughProcessor>()));
    assert(cycle.addNode("b", std::make_unique<transmission::PassThroughProcessor>()));
    assert(cycle.connect("a", "b"));
    assert(cycle.connect("b", "a"));
    assert(!cycle.prepare(1, 4));

    transmission::RoutedAudioGraph midi;
    assert(midi.addNode("midi-in",
                        std::make_unique<transmission::PassThroughProcessor>()));
    assert(midi.addNode("midi-out",
                        std::make_unique<transmission::PassThroughProcessor>()));
    assert(midi.setExternalMidiInput("midi-in", 2));
    assert(midi.setExternalMidiOutput("midi-out", 3));
    assert(midi.connectMidi("midi-in", "midi-out"));
    assert(midi.prepare(1, 4));
    transmission::MidiEvent inputEvent;
    inputEvent.port = 2;
    inputEvent.size = 3;
    inputEvent.data = {0x90, 60, 100};
    float midiOutputAudio[] = {0.0F, 0.0F, 0.0F, 0.0F};
    float* midiAudioOutputs[] = {midiOutputAudio};
    midi.processWithMidi(inputs, midiAudioOutputs, 1, 4, &inputEvent, 1);
    transmission::MidiEvent outputEvent;
    assert(midi.takeExternalMidiOutput(&outputEvent, 1) == 1);
    assert(outputEvent.port == 3);
    assert(outputEvent.data == inputEvent.data);
    return 0;
}
