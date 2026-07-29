#include "transmission/RoutedAudioGraph.h"

#include <algorithm>
#include <cassert>
#include <memory>

namespace {

class EightToTwoProcessor final : public transmission::AudioProcessor {
public:
    void process(const float* const*, float* const* outputs,
                 std::size_t channels, std::size_t frames) noexcept override {
        for (std::size_t channel = 0; channel < channels; ++channel)
            std::fill_n(outputs[channel], frames, 0.0F);
    }

    void process(const float* const* inputs, std::size_t inputChannels,
                 float* const* outputs, std::size_t outputChannels,
                 std::size_t frames) noexcept override {
        assert(inputChannels == 8);
        assert(outputChannels == 2);
        std::copy_n(inputs[6], frames, outputs[0]);
        std::copy_n(inputs[7], frames, outputs[1]);
    }
};

} // namespace

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

    transmission::RoutedAudioGraph asymmetric;
    assert(asymmetric.addNode(
        "input", std::make_unique<transmission::PassThroughProcessor>(), 2, 2));
    assert(asymmetric.addNode(
        "mixer", std::make_unique<EightToTwoProcessor>(), 8, 2));
    assert(asymmetric.addNode(
        "output", std::make_unique<transmission::PassThroughProcessor>(), 2, 2));
    assert(asymmetric.setExternalAudioInput("input"));
    assert(asymmetric.setExternalAudioOutput("output"));
    assert(asymmetric.connect("input", 0, "mixer", 6));
    assert(asymmetric.connect("input", 1, "mixer", 7));
    assert(asymmetric.connect("mixer", 0, "output", 0));
    assert(asymmetric.connect("mixer", 1, "output", 1));
    assert(!asymmetric.connect("input", 2, "mixer", 0));
    assert(!asymmetric.connect("mixer", 0, "output", 2));
    assert(asymmetric.prepare(2, 4));
    const float left[] = {1.0F, 2.0F, 3.0F, 4.0F};
    const float right[] = {5.0F, 6.0F, 7.0F, 8.0F};
    const float* asymmetricInputs[] = {left, right};
    float leftOutput[4]{};
    float rightOutput[4]{};
    float* asymmetricOutputs[] = {leftOutput, rightOutput};
    asymmetric.process(asymmetricInputs, asymmetricOutputs, 2, 4);
    for (std::size_t frame = 0; frame < 4; ++frame) {
        assert(leftOutput[frame] == left[frame]);
        assert(rightOutput[frame] == right[frame]);
    }
    return 0;
}
