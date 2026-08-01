#include "transmission/FakeAudioDevice.h"
#include "transmission/GraphRuntimeCompiler.h"
#include "transmission/GraphRuntimeController.h"
#include "transmission/UiProjectCodec.h"
#include "transmission/Vst3Processor.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <memory>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

namespace {

using transmission::RuntimeConnectionKind;
using transmission::RuntimeGraphNode;
using transmission::RuntimeGraphSnapshot;
using transmission::RuntimeNodeKind;

struct Options {
    std::string interchangePath;
    double seconds = 30.0;
    std::size_t blockSize = 1024;
    double sampleRate = 48000.0;
    bool realtime = false;
    double renderAheadMilliseconds = 200.0;
    std::size_t processingThreads = 0;
    double startBeat = 0.0;
};

struct AudioWindow {
    double energy = 0.0;
    double sum = 0.0;
    float peak = 0.0F;
    std::uint64_t samples = 0;
};

bool positiveNumber(const char* text, double& value) {
    try {
        std::size_t parsed = 0;
        const std::string input(text);
        value = std::stod(input, &parsed);
        return parsed == input.size() && std::isfinite(value) && value > 0.0;
    } catch (...) {
        return false;
    }
}

bool parseOptions(int argc, char** argv, Options& options) {
    if (argc < 2) return false;
    options.interchangePath = argv[1];
    for (int index = 2; index < argc; ++index) {
        const std::string argument(argv[index]);
        if (argument == "--realtime") {
            options.realtime = true;
            continue;
        }
        if (index + 1 >= argc) return false;
        double value = 0.0;
        if (!positiveNumber(argv[index + 1], value)) return false;
        if (argument == "--seconds")
            options.seconds = value;
        else if (argument == "--block-size")
            options.blockSize = static_cast<std::size_t>(value);
        else if (argument == "--sample-rate")
            options.sampleRate = value;
        else if (argument == "--render-ahead-ms")
            options.renderAheadMilliseconds = value;
        else if (argument == "--threads")
            options.processingThreads = static_cast<std::size_t>(value);
        else if (argument == "--start-beat")
            options.startBeat = value;
        else
            return false;
        ++index;
    }
    return options.blockSize > 0;
}

bool readInterchange(const std::string& path, std::string& text) {
    if (path == "-") {
        text.assign(std::istreambuf_iterator<char>(std::cin), {});
        return !text.empty();
    }
    std::ifstream input(path);
    if (!input) return false;
    text.assign(std::istreambuf_iterator<char>(input), {});
    return !text.empty();
}

RuntimeNodeKind runtimeKind(transmission::UiProjectNodeKind kind) {
    using UiKind = transmission::UiProjectNodeKind;
    switch (kind) {
    case UiKind::SystemInput: return RuntimeNodeKind::SystemInput;
    case UiKind::SystemOutput: return RuntimeNodeKind::SystemOutput;
    case UiKind::PassThrough: return RuntimeNodeKind::PassThrough;
    case UiKind::Plugin: return RuntimeNodeKind::Plugin;
    case UiKind::MidiInput: return RuntimeNodeKind::MidiInput;
    case UiKind::MidiOutput: return RuntimeNodeKind::MidiOutput;
    case UiKind::Gain: return RuntimeNodeKind::Gain;
    }
    return RuntimeNodeKind::PassThrough;
}

RuntimeGraphSnapshot snapshotFor(const transmission::UiProject& project) {
    RuntimeGraphSnapshot snapshot;
    for (const auto& node : project.nodes) {
        RuntimeGraphNode runtime;
        runtime.id = node.id;
        runtime.kind = runtimeKind(node.kind);
        runtime.pluginPath = node.pluginPath;
        runtime.audioInputs = node.audioInputs;
        runtime.audioOutputs = node.audioOutputs;
        for (const auto& parameter : node.parameters)
            runtime.parameters.push_back({parameter.id,
                                          parameter.normalizedValue});
        runtime.state = {node.componentState, node.controllerState};
        runtime.gainDb = node.gainDb;
        runtime.pan = node.pan;
        const auto lane = std::find_if(
            project.gainLanes.begin(), project.gainLanes.end(),
            [&](const auto& candidate) {
                return candidate.targetNodeId == node.id;
            });
        if (lane != project.gainLanes.end())
            runtime.gainEnvelope = lane->points;
        snapshot.nodes.push_back(std::move(runtime));
    }
    for (const auto& connection : project.connections)
        snapshot.connections.push_back(
            {connection.from, connection.to,
             connection.kind == transmission::UiProjectConnectionKind::Audio
                 ? RuntimeConnectionKind::Audio
                 : RuntimeConnectionKind::Midi,
             connection.fromPort, connection.toPort});
    for (const auto& clip : project.midiClips) {
        for (const auto& note : clip.notes) {
            snapshot.scheduledMidiEvents.push_back({
                clip.targetNodeId, clip.startBeat + note.startBeat,
                {static_cast<std::uint8_t>(0x90U | note.channel),
                 note.pitch, note.velocity}});
            snapshot.scheduledMidiEvents.push_back({
                clip.targetNodeId,
                clip.startBeat + note.startBeat + note.durationBeats,
                {static_cast<std::uint8_t>(0x80U | note.channel),
                 note.pitch, 0}});
        }
    }
    return snapshot;
}

transmission::RuntimeProcessorFactory processorFactory() {
    return
        [](const RuntimeGraphNode& node,
           const transmission::AudioDeviceConfig& device,
           std::string& error)
            -> std::unique_ptr<transmission::AudioProcessor> {
            if (node.kind == RuntimeNodeKind::Plugin) {
                auto processor =
                    std::make_unique<transmission::Vst3Processor>();
                if (!processor->initialize(
                        node.pluginPath, node.audioInputs, node.audioOutputs,
                        device.blockSize, device.sampleRate, error))
                    return nullptr;
                return processor;
            }
            if (node.kind == RuntimeNodeKind::MidiInput ||
                node.kind == RuntimeNodeKind::MidiOutput)
                return std::make_unique<
                    transmission::MidiEndpointProcessor>();
            return std::make_unique<transmission::PassThroughProcessor>();
        };
}

transmission::GraphRuntimeCompiler makeCompiler() {
    return transmission::GraphRuntimeCompiler(processorFactory());
}

std::unique_ptr<transmission::RoutedAudioGraph> compile(
    const RuntimeGraphSnapshot& snapshot,
    const transmission::AudioDeviceConfig& config, std::string& error) {
    auto graph = makeCompiler().compile(snapshot, config, error);
    if (graph && !graph->prepare(config.channels, config.blockSize)) {
        graph.reset();
        error = "unable to prepare probe graph";
    }
    return graph;
}

void advanceBeat(double& beat, const transmission::UiProject& project,
                 std::size_t frames, double sampleRate) {
    beat += static_cast<double>(frames) * project.tempo /
            (60.0 * sampleRate);
    if (!project.loopEnabled) return;
    const double loopEnd = project.loopBars * 4.0;
    if (loopEnd > 0.0 && beat >= loopEnd)
        beat = std::fmod(beat, loopEnd);
}

struct Buffers {
    Buffers(std::size_t channels, std::size_t frames)
        : input(channels * frames, 0.0F),
          output(channels * frames, 0.0F),
          inputs(channels), outputs(channels) {
        for (std::size_t channel = 0; channel < channels; ++channel) {
            inputs[channel] = input.data() + channel * frames;
            outputs[channel] = output.data() + channel * frames;
        }
    }
    std::vector<float> input;
    std::vector<float> output;
    std::vector<const float*> inputs;
    std::vector<float*> outputs;
};

void printMidi(const std::string& nodeId,
               const std::vector<std::uint64_t>& windows) {
    const auto total =
        std::accumulate(windows.begin(), windows.end(), std::uint64_t{0});
    std::cout << "MIDI node=" << nodeId << " totalEvents=" << total
              << " windows=";
    for (std::size_t index = 0; index < windows.size(); ++index) {
        if (index != 0) std::cout << ',';
        std::cout << index << ':' << windows[index];
    }
    std::cout << '\n';
}

void printAudio(const std::string& nodeId,
                const std::vector<AudioWindow>& windows) {
    double totalEnergy = 0.0;
    double totalSum = 0.0;
    std::uint64_t totalSamples = 0;
    int lastActive = -1;
    for (std::size_t index = 0; index < windows.size(); ++index) {
        totalEnergy += windows[index].energy;
        totalSum += windows[index].sum;
        totalSamples += windows[index].samples;
        const auto mean = windows[index].samples == 0
            ? 0.0
            : windows[index].sum /
                  static_cast<double>(windows[index].samples);
        const auto acRms = windows[index].samples == 0
            ? 0.0
            : std::sqrt(std::max(
                  0.0, windows[index].energy /
                           static_cast<double>(windows[index].samples) -
                           mean * mean));
        if (acRms > 1.0e-7)
            lastActive = static_cast<int>(index);
    }
    const auto totalRms = totalSamples == 0
        ? 0.0
        : std::sqrt(totalEnergy / static_cast<double>(totalSamples));
    const auto totalMean = totalSamples == 0
        ? 0.0
        : totalSum / static_cast<double>(totalSamples);
    const auto totalAcRms = std::sqrt(
        std::max(0.0, totalRms * totalRms - totalMean * totalMean));
    std::cout << "AUDIO node=" << nodeId << " totalRms="
              << std::scientific << std::setprecision(6) << totalRms
              << " totalAcRms=" << totalAcRms
              << " dc=" << totalMean
              << " lastActiveSecond=" << lastActive << " windows=";
    for (std::size_t index = 0; index < windows.size(); ++index) {
        if (index != 0) std::cout << ',';
        const auto rms = windows[index].samples == 0
            ? 0.0
            : std::sqrt(windows[index].energy /
                        static_cast<double>(windows[index].samples));
        const auto mean = windows[index].samples == 0
            ? 0.0
            : windows[index].sum /
                  static_cast<double>(windows[index].samples);
        const auto acRms = std::sqrt(
            std::max(0.0, rms * rms - mean * mean));
        std::cout << index << ':' << acRms << '/' << mean << '/'
                  << windows[index].peak;
    }
    std::cout << std::defaultfloat << '\n';
}

bool runRealtime(const RuntimeGraphSnapshot& snapshot,
                 const transmission::UiProject& project,
                 const transmission::AudioDeviceConfig& config,
                 const Options& options, std::string& error) {
    transmission::GraphRuntimeController runtime(processorFactory());
    const auto renderAheadBlocks = static_cast<std::size_t>(std::ceil(
        options.renderAheadMilliseconds * 0.001 * config.sampleRate /
        static_cast<double>(config.blockSize)));
    if (!runtime.setRenderAheadBlocks(renderAheadBlocks) ||
        !runtime.setProcessingThreadCount(options.processingThreads)) {
        error = "invalid render-ahead or processing-thread configuration";
        return false;
    }
    transmission::FakeAudioDevice device;
    const transmission::RuntimeTransportConfig transport{
        project.tempo, 0.0, project.loopBars * 4.0, project.loopEnabled};
    if (!runtime.start(snapshot, device, config, transport, error))
        return false;

    Buffers buffers(config.channels, config.blockSize);
    const auto desiredBlocks = static_cast<std::uint64_t>(std::ceil(
        options.seconds * config.sampleRate /
        static_cast<double>(config.blockSize)));
    const auto callbackBlocks = desiredBlocks + renderAheadBlocks;
    const auto period = std::chrono::duration_cast<
        std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(
            static_cast<double>(config.blockSize) / config.sampleRate));
    auto deadline = std::chrono::steady_clock::now();
    double sum = 0.0;
    double energy = 0.0;
    float peak = 0.0F;
    std::uint64_t samples = 0;
    for (std::uint64_t block = 0; block < callbackBlocks; ++block) {
        if (!device.render(buffers.inputs.data(), buffers.outputs.data())) {
            runtime.stop();
            error = "fake audio device render failed";
            return false;
        }
        if (block >= renderAheadBlocks) {
            for (const auto sample : buffers.output) {
                sum += sample;
                energy += static_cast<double>(sample) * sample;
                peak = std::max(peak, std::fabs(sample));
                ++samples;
            }
        }
        deadline += period;
        std::this_thread::sleep_until(deadline);
    }
    const auto diagnostics = runtime.diagnostics();
    const auto timings = runtime.processorTimings();
    runtime.stop();
    const auto mean =
        samples == 0 ? 0.0 : sum / static_cast<double>(samples);
    const auto rms = samples == 0
        ? 0.0
        : std::sqrt(energy / static_cast<double>(samples));
    const auto acRms =
        std::sqrt(std::max(0.0, rms * rms - mean * mean));
    std::cout << "REALTIME renderAheadBlocks=" << renderAheadBlocks
              << " processingThreads=" << diagnostics.processingThreads
              << " underruns=" << diagnostics.underruns
              << " lateBlocks=" << diagnostics.renderLateBlocks
              << " queueDrops=" << diagnostics.renderQueueDrops
              << " processedBlocks=" << diagnostics.processedBlocks
              << " averageRenderUs="
              << diagnostics.averageRenderMicroseconds
              << " maximumRenderUs="
              << diagnostics.maximumRenderMicroseconds
              << " outputAcRms=" << acRms << " dc=" << mean
              << " peak=" << peak << '\n';
    for (const auto& timing : timings)
        std::cout << "REALTIME_TIMING node=" << timing.nodeId
                  << " averageUs=" << timing.averageMicroseconds
                  << " maximumUs=" << timing.maximumMicroseconds << '\n';
    return true;
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parseOptions(argc, argv, options)) {
        std::cerr
            << "Usage: transmission_vst3_project_probe <interchange|-> "
               "[--seconds N] [--block-size N] [--sample-rate N] "
               "[--start-beat N] [--realtime] [--render-ahead-ms N] [--threads N]\n";
        return 2;
    }
    std::string encoded;
    if (!readInterchange(options.interchangePath, encoded)) {
        std::cerr << "Unable to read project interchange\n";
        return 1;
    }
    transmission::UiProject project;
    std::string error;
    if (!transmission::decodeUiProject(encoded, project, error)) {
        std::cerr << error << '\n';
        return 1;
    }
    const transmission::AudioDeviceConfig config{
        2, options.blockSize, options.sampleRate, false, 1};
    const auto totalFrames = static_cast<std::uint64_t>(
        std::ceil(options.seconds * options.sampleRate));
    const auto base = snapshotFor(project);

    std::cout << "PROJECT id=" << project.id << " seconds="
              << options.seconds << " blockSize=" << options.blockSize
              << " sampleRate=" << options.sampleRate << '\n';
    if (options.realtime) {
        if (!runRealtime(base, project, config, options, error)) {
            std::cerr << "Real-time probe failed: " << error << '\n';
            return 1;
        }
        return 0;
    }

    auto graph = compile(base, config, error);
    if (!graph) {
        std::cerr << "Project probe failed: " << error << '\n';
        return 1;
    }
    graph->setTimingEnabled(true);
    const auto windowFrames = static_cast<std::uint64_t>(
        std::llround(config.sampleRate));
    const auto windowCount = static_cast<std::size_t>(
        (totalFrames + windowFrames - 1) / windowFrames);
    std::vector<std::pair<std::string, std::vector<std::uint64_t>>>
        midiActivity;
    for (const auto& node : project.nodes) {
        const bool routed = node.midiOutputs > 0 && std::any_of(
            project.connections.begin(), project.connections.end(),
            [&node](const auto& connection) {
                return connection.kind ==
                           transmission::UiProjectConnectionKind::Midi &&
                       connection.from == node.id;
            });
        if (!routed) continue;
        midiActivity.push_back(
            {node.id, std::vector<std::uint64_t>(windowCount, 0)});
    }
    std::vector<std::pair<RuntimeGraphNode, std::vector<AudioWindow>>>
        audioActivity;
    for (const auto& node : base.nodes) {
        const bool routed = node.audioOutputs > 0 && std::any_of(
            base.connections.begin(), base.connections.end(),
            [&node](const auto& connection) {
                return connection.kind == RuntimeConnectionKind::Audio &&
                       connection.from == node.id;
            });
        if (!routed) continue;
        audioActivity.push_back(
            {node, std::vector<AudioWindow>(windowCount)});
    }

    Buffers buffers(config.channels, config.blockSize);
    std::vector<float> scratch(config.blockSize, 0.0F);
    std::array<transmission::MidiEvent,
               transmission::maxMidiEventsPerBlock>
        midi {};
    std::uint64_t rendered = 0;
    std::uint64_t reportedSecond = 0;
    std::uint64_t processedBlocks = 0;
    std::uint64_t lateBlocks = 0;
    std::uint64_t totalProcessNanoseconds = 0;
    std::uint64_t maximumProcessNanoseconds = 0;
    const auto deadlineNanoseconds = static_cast<std::uint64_t>(
        1.0e9 * static_cast<double>(config.blockSize) /
        config.sampleRate);
    double beat = options.startBeat;
    while (rendered < totalFrames) {
        graph->setProcessContext({beat, project.tempo, true});
        const auto started = std::chrono::steady_clock::now();
        graph->process(buffers.inputs.data(), buffers.outputs.data(),
                       config.channels, config.blockSize);
        const auto processNanoseconds = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - started)
                .count());
        totalProcessNanoseconds += processNanoseconds;
        maximumProcessNanoseconds =
            std::max(maximumProcessNanoseconds, processNanoseconds);
        ++processedBlocks;
        if (processNanoseconds > deadlineNanoseconds) ++lateBlocks;
        for (auto& [nodeId, windows] : midiActivity) {
            const auto count =
                graph->copyNodeMidiOutput(nodeId, midi.data(), midi.size());
            for (std::size_t event = 0; event < count; ++event) {
                const auto absoluteFrame =
                    rendered + std::min(midi[event].frameOffset,
                                        config.blockSize - 1);
                if (absoluteFrame < totalFrames)
                    ++windows[static_cast<std::size_t>(
                        absoluteFrame / windowFrames)];
            }
        }
        for (auto& [node, windows] : audioActivity) {
            for (std::size_t channel = 0;
                 channel < std::min(config.channels, node.audioOutputs);
                 ++channel) {
                if (!graph->copyNodeAudioOutput(
                        node.id, channel, scratch.data(), config.blockSize)) {
                    std::cerr << "Unable to read probe output for "
                              << node.id << '\n';
                    return 1;
                }
                for (std::size_t frame = 0; frame < config.blockSize;
                     ++frame) {
                    const auto absoluteFrame = rendered + frame;
                    if (absoluteFrame >= totalFrames) break;
                    const auto window = static_cast<std::size_t>(
                        absoluteFrame / windowFrames);
                    const auto sample = scratch[frame];
                    windows[window].energy +=
                        static_cast<double>(sample) * sample;
                    windows[window].sum += sample;
                    windows[window].peak =
                        std::max(windows[window].peak,
                                 std::fabs(sample));
                    ++windows[window].samples;
                }
            }
        }
        rendered += config.blockSize;
        const auto completedSecond =
            std::min(rendered, totalFrames) / windowFrames;
        if (completedSecond > reportedSecond) {
            reportedSecond = completedSecond;
            std::cerr << "Rendered " << reportedSecond << " second(s)\n";
        }
        advanceBeat(beat, project, config.blockSize, config.sampleRate);
    }
    for (const auto& [nodeId, windows] : midiActivity)
        printMidi(nodeId, windows);
    for (const auto& [node, windows] : audioActivity)
        printAudio(node.id, windows);
    std::cout << "PERFORMANCE blockDeadlineUs="
              << static_cast<double>(deadlineNanoseconds) / 1000.0
              << " averageBlockUs="
              << (processedBlocks == 0
                      ? 0.0
                      : static_cast<double>(totalProcessNanoseconds) /
                            static_cast<double>(processedBlocks) / 1000.0)
              << " maximumBlockUs="
              << static_cast<double>(maximumProcessNanoseconds) / 1000.0
              << " lateBlocks=" << lateBlocks << '/' << processedBlocks
              << '\n';
    for (const auto& timing : graph->processorTimings())
        std::cout << "TIMING node=" << timing.nodeId
                  << " averageUs=" << timing.averageMicroseconds
                  << " maximumUs=" << timing.maximumMicroseconds << '\n';
    return 0;
}
