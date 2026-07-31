#include "transmission/OfflineAudioRenderer.h"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace {

class ConstantProcessor final : public transmission::AudioProcessor {
public:
    void process(const float* const*, std::size_t,
                 float* const* outputs, std::size_t outputChannels,
                 std::size_t frames) noexcept override {
        for (std::size_t channel = 0; channel < outputChannels; ++channel)
            std::fill(outputs[channel], outputs[channel] + frames, 0.25F);
    }

    void process(const float* const*, float* const* outputs,
                 std::size_t channels, std::size_t frames) noexcept override {
        for (std::size_t channel = 0; channel < channels; ++channel)
            std::fill(outputs[channel], outputs[channel] + frames, 0.25F);
    }
};

class MidiReactiveProcessor final : public transmission::AudioProcessor {
public:
    void process(const float* const*, float* const* outputs,
                 std::size_t channels, std::size_t frames) noexcept override {
        for (std::size_t channel = 0; channel < channels; ++channel)
            std::fill_n(outputs[channel], frames, 0.0F);
    }
    void processWithMidi(const float* const*, float* const* outputs,
                         std::size_t channels, std::size_t frames,
                         const transmission::MidiEvent* events,
                         std::size_t eventCount) noexcept override {
        process(nullptr, outputs, channels, frames);
        for (std::size_t event = 0; event < eventCount; ++event)
            if ((events[event].data[0] & 0xf0U) == 0x90U)
                for (std::size_t channel = 0; channel < channels; ++channel)
                    std::fill(outputs[channel] + events[event].frameOffset,
                              outputs[channel] + frames, 0.5F);
    }
};

transmission::RuntimeGraphSnapshot snapshot() {
    using Kind = transmission::RuntimeNodeKind;
    using Edge = transmission::RuntimeConnectionKind;
    transmission::RuntimeGraphSnapshot result;
    result.nodes = {
        {"source", Kind::Plugin, "/constant.vst3", 0, 0, 2},
        {"output", Kind::SystemOutput, "", 0, 2, 0}};
    result.connections = {
        {"source", "output", Edge::Audio, 0, 0},
        {"source", "output", Edge::Audio, 1, 1}};
    return result;
}

std::uint32_t readU32(const std::vector<char>& bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(
        static_cast<unsigned char>(bytes[offset]) |
        static_cast<unsigned char>(bytes[offset + 1]) << 8U |
        static_cast<unsigned char>(bytes[offset + 2]) << 16U |
        static_cast<unsigned char>(bytes[offset + 3]) << 24U);
}

} // namespace

int main() {
    transmission::OfflineAudioRenderer renderer(
        [](const transmission::RuntimeGraphNode& node,
           const transmission::AudioDeviceConfig&, std::string&)
            -> std::unique_ptr<transmission::AudioProcessor> {
            if (node.pluginPath == "/midi.vst3")
                return std::make_unique<MidiReactiveProcessor>();
            if (node.kind == transmission::RuntimeNodeKind::Plugin)
                return std::make_unique<ConstantProcessor>();
            return std::make_unique<transmission::PassThroughProcessor>();
        });
    const auto path =
        std::filesystem::temp_directory_path() /
        "transmission-offline-renderer-test.wav";
    transmission::OfflineRenderOptions options;
    options.outputPath = path.string();
    options.channels = 2;
    options.blockSize = 4;
    options.sampleRate = 48000.0;
    options.totalFrames = 6;
    transmission::OfflineRenderResult result;
    std::string error;
    double progress = 0.0;
    assert(renderer.renderWave(
        snapshot(), options, result, error,
        [&](double value) {
            progress = value;
            return true;
        }));
    assert(error.empty());
    assert(result.framesWritten == 6);
    assert(std::fabs(result.peak - 0.25F) < 1.0e-6F);
    assert(progress == 1.0);

    std::ifstream input(path, std::ios::binary);
    const std::vector<char> bytes(
        std::istreambuf_iterator<char>(input), {});
    assert(bytes.size() == 44 + 6 * 2 * sizeof(float));
    assert(std::string(bytes.data(), 4) == "RIFF");
    assert(std::string(bytes.data() + 8, 4) == "WAVE");
    assert(readU32(bytes, 40) == 6 * 2 * sizeof(float));
    const auto* samples =
        reinterpret_cast<const float*>(bytes.data() + 44);
    for (std::size_t index = 0; index < 12; ++index)
        assert(std::fabs(samples[index] - 0.25F) < 1.0e-6F);
    std::filesystem::remove(path);

    auto scheduled = snapshot();
    scheduled.nodes[0].pluginPath = "/midi.vst3";
    scheduled.nodes[0].audioInputs = 2;
    scheduled.scheduledMidiEvents.push_back(
        {"source", 0.5, {0x90, 60, 100}});
    options.sampleRate = 4.0;
    options.tempo = 60.0;
    options.blockSize = 4;
    options.totalFrames = 4;
    assert(renderer.renderWave(scheduled, options, result, error));
    assert(result.peak == 0.5F);
    std::filesystem::remove(path);

    options.totalFrames = 0;
    assert(!renderer.renderWave(snapshot(), options, result, error));
    assert(!error.empty());

    options.totalFrames = 8;
    assert(!renderer.renderWave(
        snapshot(), options, result, error,
        [](double) { return false; }));
    assert(error == "Audio rendering was cancelled");
    assert(!std::filesystem::exists(path));
    return 0;
}
