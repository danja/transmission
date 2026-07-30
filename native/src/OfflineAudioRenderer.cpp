#include "transmission/OfflineAudioRenderer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <thread>
#include <vector>

namespace transmission {
namespace {

void writeU16(std::ostream& output, std::uint16_t value) {
    const char bytes[] = {
        static_cast<char>(value & 0xffU),
        static_cast<char>((value >> 8U) & 0xffU)};
    output.write(bytes, sizeof(bytes));
}

void writeU32(std::ostream& output, std::uint32_t value) {
    const char bytes[] = {
        static_cast<char>(value & 0xffU),
        static_cast<char>((value >> 8U) & 0xffU),
        static_cast<char>((value >> 16U) & 0xffU),
        static_cast<char>((value >> 24U) & 0xffU)};
    output.write(bytes, sizeof(bytes));
}

bool writeWaveHeader(std::ostream& output, const OfflineRenderOptions& options,
                     std::uint32_t dataBytes) {
    const auto sampleRate = static_cast<std::uint32_t>(
        std::llround(options.sampleRate));
    const auto channels = static_cast<std::uint16_t>(options.channels);
    const auto blockAlign = static_cast<std::uint16_t>(channels * sizeof(float));
    const auto byteRate = sampleRate * static_cast<std::uint32_t>(blockAlign);
    output.write("RIFF", 4);
    writeU32(output, 36U + dataBytes);
    output.write("WAVEfmt ", 8);
    writeU32(output, 16U);
    writeU16(output, 3U);
    writeU16(output, channels);
    writeU32(output, sampleRate);
    writeU32(output, byteRate);
    writeU16(output, blockAlign);
    writeU16(output, 32U);
    output.write("data", 4);
    writeU32(output, dataBytes);
    return static_cast<bool>(output);
}

std::string temporaryPathFor(const std::string& outputPath) {
    const auto nonce = std::chrono::steady_clock::now()
                           .time_since_epoch().count();
    return outputPath + ".transmission-part-" + std::to_string(nonce);
}

void advanceBeat(double& beat, const OfflineRenderOptions& options) {
    beat += static_cast<double>(options.blockSize) * options.tempo /
            (60.0 * options.sampleRate);
    if (!options.loopEnabled ||
        options.loopEndBeat <= options.loopStartBeat ||
        beat < options.loopEndBeat)
        return;
    const auto length = options.loopEndBeat - options.loopStartBeat;
    beat = options.loopStartBeat +
           std::fmod(beat - options.loopStartBeat, length);
}

} // namespace

OfflineAudioRenderer::OfflineAudioRenderer(
    RuntimeProcessorFactory processorFactory)
    : compiler_(std::move(processorFactory)) {}

bool OfflineAudioRenderer::renderWave(
    const RuntimeGraphSnapshot& snapshot, const OfflineRenderOptions& options,
    OfflineRenderResult& result, std::string& error,
    OfflineRenderProgress progress) const {
    result = {};
    error.clear();
    const auto maximumDataBytes =
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) -
        36U;
    const auto bytesPerFrame =
        options.channels > std::numeric_limits<std::uint64_t>::max() /
                               sizeof(float)
        ? std::uint64_t{0}
        : static_cast<std::uint64_t>(options.channels) * sizeof(float);
    const auto dataBytes =
        bytesPerFrame != 0 &&
                options.totalFrames <= maximumDataBytes / bytesPerFrame
        ? options.totalFrames * bytesPerFrame
        : maximumDataBytes + 1U;
    if (options.outputPath.empty() || options.channels == 0 ||
        options.channels > std::numeric_limits<std::uint16_t>::max() ||
        options.blockSize == 0 || options.totalFrames == 0 ||
        !std::isfinite(options.sampleRate) || options.sampleRate <= 0.0 ||
        options.sampleRate >
            static_cast<double>(std::numeric_limits<std::uint32_t>::max()) ||
        !std::isfinite(options.tempo) || options.tempo <= 0.0 ||
        dataBytes > maximumDataBytes) {
        error = "offline render options are invalid or exceed the WAV size limit";
        return false;
    }

    const AudioDeviceConfig config{
        options.channels, options.blockSize, options.sampleRate, false, 0, 0};
    auto graph = compiler_.compile(snapshot, config, error);
    if (!graph) return false;
    if (!graph->prepare(options.channels, options.blockSize)) {
        error = "unable to prepare the offline render graph";
        return false;
    }
    auto threads = options.processingThreads;
    if (threads == 0)
        threads = std::max<std::size_t>(
            1, static_cast<std::size_t>(std::thread::hardware_concurrency()));
    if (!graph->configureProcessingThreads(threads)) {
        error = "unable to configure offline processing threads";
        return false;
    }

    const auto temporaryPath = temporaryPathFor(options.outputPath);
    std::ofstream output(
        temporaryPath, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!output || !writeWaveHeader(
            output, options, static_cast<std::uint32_t>(dataBytes))) {
        error = "unable to create the WAV output file";
        output.close();
        std::error_code ignored;
        std::filesystem::remove(temporaryPath, ignored);
        return false;
    }

    std::vector<float> input(options.channels * options.blockSize, 0.0F);
    std::vector<float> rendered(options.channels * options.blockSize, 0.0F);
    std::vector<float> interleaved(options.channels * options.blockSize, 0.0F);
    std::vector<const float*> inputs(options.channels);
    std::vector<float*> outputs(options.channels);
    for (std::size_t channel = 0; channel < options.channels; ++channel) {
        inputs[channel] = input.data() + channel * options.blockSize;
        outputs[channel] = rendered.data() + channel * options.blockSize;
    }

    bool completed = true;
    double beat = 0.0;
    while (result.framesWritten < options.totalFrames) {
        graph->setProcessContext({beat, options.tempo, true});
        graph->process(
            inputs.data(), outputs.data(), options.channels, options.blockSize);
        const auto frames = static_cast<std::size_t>(std::min<std::uint64_t>(
            options.blockSize, options.totalFrames - result.framesWritten));
        for (std::size_t frame = 0; frame < frames; ++frame) {
            for (std::size_t channel = 0; channel < options.channels; ++channel) {
                auto sample = rendered[channel * options.blockSize + frame];
                if (!std::isfinite(sample)) sample = 0.0F;
                result.peak = std::max(result.peak, std::fabs(sample));
                interleaved[frame * options.channels + channel] = sample;
            }
        }
        output.write(
            reinterpret_cast<const char*>(interleaved.data()),
            static_cast<std::streamsize>(
                frames * options.channels * sizeof(float)));
        if (!output) {
            error = "unable to write the WAV output file";
            completed = false;
            break;
        }
        result.framesWritten += frames;
        if (progress &&
            !progress(static_cast<double>(result.framesWritten) /
                      static_cast<double>(options.totalFrames))) {
            error = "Audio rendering was cancelled";
            completed = false;
            break;
        }
        advanceBeat(beat, options);
    }
    output.close();
    if (!output && completed) {
        error = "unable to finish the WAV output file";
        completed = false;
    }
    if (!completed) {
        std::error_code ignored;
        std::filesystem::remove(temporaryPath, ignored);
        return false;
    }

    std::error_code fileError;
    std::filesystem::rename(temporaryPath, options.outputPath, fileError);
    if (fileError) {
        error = "unable to replace the output file: " + fileError.message();
        std::filesystem::remove(temporaryPath, fileError);
        return false;
    }
    return true;
}

} // namespace transmission
