#include "transmission/RoutedAudioGraph.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <memory>
#include <thread>

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

class ConcurrentProcessor final : public transmission::AudioProcessor {
public:
    ConcurrentProcessor(std::atomic<int>& active,
                        std::atomic<int>& maximum)
        : active_(active), maximum_(maximum) {}

    void process(const float* const* inputs, float* const* outputs,
                 std::size_t channels, std::size_t frames) noexcept override {
        const auto active = active_.fetch_add(1) + 1;
        auto maximum = maximum_.load();
        while (maximum < active &&
               !maximum_.compare_exchange_weak(maximum, active)) {}
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        for (std::size_t channel = 0; channel < channels; ++channel)
            std::copy_n(inputs[channel], frames, outputs[channel]);
        active_.fetch_sub(1);
    }

private:
    std::atomic<int>& active_;
    std::atomic<int>& maximum_;
};

class CountingMidiProcessor final : public transmission::AudioProcessor {
public:
    explicit CountingMidiProcessor(std::atomic<int>& calls) : calls_(calls) {}

    void process(const float* const*, float* const* outputs,
                 std::size_t channels, std::size_t frames) noexcept override {
        calls_.fetch_add(1);
        for (std::size_t channel = 0; channel < channels; ++channel)
            std::fill_n(outputs[channel], frames, 0.0F);
    }

    void processWithMidi(
        const float* const*, std::size_t, float* const* outputs,
        std::size_t outputChannels, std::size_t frames,
        const transmission::MidiEvent*, std::size_t) noexcept override {
        calls_.fetch_add(1);
        for (std::size_t channel = 0; channel < outputChannels; ++channel)
            std::fill_n(outputs[channel], frames, 0.0F);
    }

private:
    std::atomic<int>& calls_;
};

class MappedParameterProcessor final : public transmission::AudioProcessor {
public:
    void process(const float* const*, float* const* outputs,
                 std::size_t channels, std::size_t frames) noexcept override {
        for (std::size_t channel = 0; channel < channels; ++channel)
            std::fill_n(outputs[channel], frames, 0.0F);
    }

    void processWithMidi(
        const float* const* inputs, float* const* outputs,
        std::size_t channels, std::size_t frames,
        const transmission::MidiEvent*, std::size_t eventCount) noexcept override {
        receivedMidi = eventCount;
        process(inputs, outputs, channels, frames);
    }

    bool enqueueParameter(std::uint32_t id, double value) noexcept override {
        parameterId = id;
        parameterValue = value;
        ++parameterChanges;
        return true;
    }

    std::size_t receivedMidi = 0;
    std::uint32_t parameterId = 0;
    double parameterValue = 0.0;
    std::size_t parameterChanges = 0;
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
    graph.setTimingEnabled(true);

    constexpr float input[] = {0.25F, -0.5F, 0.75F, 1.0F};
    float output[] = {0.0F, 0.0F, 0.0F, 0.0F};
    const float* inputs[] = {input};
    float* outputs[] = {output};
    graph.process(inputs, outputs, 1, 4);
    for (int index = 0; index < 4; ++index) assert(output[index] == input[index] * 2.0F);
    float sourceOutput[4]{};
    assert(graph.copyNodeAudioOutput("source", 0, sourceOutput, 4));
    assert(!graph.copyNodeAudioOutput("missing", 0, sourceOutput, 4));
    for (int index = 0; index < 4; ++index)
        assert(sourceOutput[index] == input[index]);
    const auto timings = graph.processorTimings();
    assert(timings.size() == 3);
    assert(std::all_of(timings.begin(), timings.end(),
                       [](const auto& timing) {
                           return timing.calls == 1 &&
                                  timing.maximumMicroseconds >= 0.0;
                       }));
    assert(graph.maximumParallelWidth() == 2);
    assert(graph.configureProcessingThreads(8));
    assert(graph.processingThreadCount() == 2);
    graph.process(inputs, outputs, 1, 4);

    std::atomic<int> active{0};
    std::atomic<int> maximum{0};
    transmission::RoutedAudioGraph parallel;
    assert(parallel.addNode(
        "left", std::make_unique<ConcurrentProcessor>(active, maximum)));
    assert(parallel.addNode(
        "right", std::make_unique<ConcurrentProcessor>(active, maximum)));
    assert(parallel.prepare(1, 4));
    assert(parallel.maximumParallelWidth() == 2);
    assert(parallel.configureProcessingThreads(2));
    parallel.process(inputs, outputs, 1, 4);
    assert(maximum.load() == 2);

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
    transmission::MidiEvent nodeEvent;
    assert(midi.copyNodeMidiOutput("midi-in", &nodeEvent, 1) == 1);
    assert(nodeEvent.data == inputEvent.data);
    assert(midi.copyNodeMidiOutput("missing", &nodeEvent, 1) == 0);

    transmission::RoutedAudioGraph scheduled;
    assert(scheduled.addNode("synth", std::make_unique<transmission::PassThroughProcessor>()));
    std::string scheduleError;
    assert(scheduled.setScheduledMidiEvents(
        {{"synth", 0.5, {0x90, 64, 110}}}, 4.0, scheduleError));
    assert(scheduled.prepare(1, 4));
    scheduled.setProcessContext({0.0, 60.0, true});
    scheduled.process(inputs, outputs, 1, 4);
    assert(scheduled.copyNodeMidiOutput("synth", &nodeEvent, 1) == 1);
    assert(nodeEvent.frameOffset == 2);
    assert(nodeEvent.data[1] == 64);

    std::atomic<int> scheduledCalls{0};
    transmission::RoutedAudioGraph sleepingScheduled;
    assert(sleepingScheduled.addNode(
        "instrument", std::make_unique<CountingMidiProcessor>(scheduledCalls),
        0, 1));
    assert(sleepingScheduled.setScheduledMidiEvents(
        {{"instrument", 4.0, {0x90, 60, 100}},
         {"instrument", 5.0, {0x80, 60, 0}}},
        4.0, scheduleError));
    assert(sleepingScheduled.prepare(1, 4));
    sleepingScheduled.setProcessContext({0.0, 60.0, true});
    sleepingScheduled.process(inputs, outputs, 1, 4);
    assert(scheduledCalls.load() == 0);
    sleepingScheduled.setProcessContext({4.0, 60.0, true});
    sleepingScheduled.process(inputs, outputs, 1, 4);
    assert(scheduledCalls.load() == 1);

    transmission::RoutedAudioGraph mapped;
    assert(mapped.addNode(
        "controller", std::make_unique<transmission::PassThroughProcessor>()));
    auto mappedProcessor = std::make_unique<MappedParameterProcessor>();
    auto* mappedCapture = mappedProcessor.get();
    assert(mapped.addNode("target", std::move(mappedProcessor)));
    assert(mapped.setExternalMidiInput("controller", 2));
    assert(mapped.connectMidi("controller", "target"));
    std::string mappingError;
    assert(!mapped.setMidiParameterMappings(
        {{"missing", 42, -1, 19, true}}, mappingError));
    assert(!mapped.setMidiParameterMappings(
        {{"target", 42, 16, 19, true}}, mappingError));
    assert(mapped.setMidiParameterMappings(
        {{"target", 42, 0, 19, true}}, mappingError));
    assert(mapped.prepare(1, 4));
    transmission::MidiEvent controllerEvent;
    controllerEvent.port = 2;
    controllerEvent.size = 3;
    controllerEvent.data = {0xb0, 19, 64};
    mapped.processWithMidi(inputs, outputs, 1, 4, &controllerEvent, 1);
    assert(mappedCapture->parameterChanges == 1);
    assert(mappedCapture->parameterId == 42);
    assert(std::fabs(mappedCapture->parameterValue - 64.0 / 127.0) < 0.0001);
    assert(mappedCapture->receivedMidi == 0);
    controllerEvent.data[1] = 20;
    mapped.processWithMidi(inputs, outputs, 1, 4, &controllerEvent, 1);
    assert(mappedCapture->parameterChanges == 1);
    assert(mappedCapture->receivedMidi == 1);
    sleepingScheduled.setProcessContext(
        {5.0 + transmission::RoutedAudioGraph::scheduledInstrumentTailBeats,
         60.0, true});
    sleepingScheduled.process(inputs, outputs, 1, 4);
    assert(scheduledCalls.load() == 1);

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
