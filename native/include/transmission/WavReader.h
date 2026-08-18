// native/include/transmission/WavReader.h
#pragma once

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace transmission {

struct WavLoadResult {
    std::vector<float> samples; // interleaved stereo: L,R,L,R,...
    std::uint32_t sampleRate = 0;
    std::uint32_t frameCount = 0;
    std::string error;
};

inline WavLoadResult loadWav(const std::string& path) {
    WavLoadResult result;
    std::ifstream file(path, std::ios::binary);
    if (!file) { result.error = "Cannot open: " + path; return result; }

    auto r16 = [&]() -> std::uint16_t {
        std::uint8_t b[2]{};
        file.read(reinterpret_cast<char*>(b), 2);
        return static_cast<std::uint16_t>(b[0] | (b[1] << 8));
    };
    auto r32 = [&]() -> std::uint32_t {
        std::uint8_t b[4]{};
        file.read(reinterpret_cast<char*>(b), 4);
        return static_cast<std::uint32_t>(b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24));
    };

    char tag[4]{};
    file.read(tag, 4);
    if (std::memcmp(tag, "RIFF", 4) != 0) { result.error = "Not a RIFF file"; return result; }
    r32(); // total size
    file.read(tag, 4);
    if (std::memcmp(tag, "WAVE", 4) != 0) { result.error = "Not a WAVE file"; return result; }

    std::uint16_t format = 0, channels = 0, bitsPerSample = 0;
    std::uint32_t sampleRate = 0, dataSize = 0;
    bool foundFmt = false, foundData = false;

    while (file) {
        char id[4]{};
        file.read(id, 4);
        if (file.gcount() < 4) break;
        const auto chunkSize = r32();
        if (std::memcmp(id, "fmt ", 4) == 0) {
            format       = r16();
            channels     = r16();
            sampleRate   = r32();
            r32(); r16(); // byte rate, block align
            bitsPerSample = r16();
            if (chunkSize > 16) file.seekg(chunkSize - 16, std::ios::cur);
            foundFmt = true;
        } else if (std::memcmp(id, "data", 4) == 0) {
            dataSize = chunkSize;
            foundData = true;
            break;
        } else {
            file.seekg(chunkSize + (chunkSize & 1), std::ios::cur);
        }
    }

    if (!foundFmt || !foundData) { result.error = "Missing fmt or data chunk"; return result; }
    if (format != 1 && format != 3) { result.error = "Unsupported WAV format (PCM/float only)"; return result; }
    if (channels == 0 || channels > 32) { result.error = "Unsupported channel count"; return result; }
    if (bitsPerSample != 16 && bitsPerSample != 24 && bitsPerSample != 32)
        { result.error = "Unsupported bit depth"; return result; }

    const std::uint32_t bytesPerSample = bitsPerSample / 8;
    const std::uint32_t bytesPerFrame  = channels * bytesPerSample;
    if (bytesPerFrame == 0) { result.error = "Zero bytes per frame"; return result; }
    const std::uint32_t frameCount = dataSize / bytesPerFrame;

    std::vector<std::uint8_t> raw(dataSize);
    file.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(dataSize));

    result.sampleRate = sampleRate;
    result.frameCount = frameCount;
    result.samples.resize(static_cast<std::size_t>(frameCount) * 2, 0.0f);

    for (std::uint32_t f = 0; f < frameCount; ++f) {
        for (std::uint32_t outCh = 0; outCh < 2; ++outCh) {
            // Fold multi-channel down: ch 0 → L, ch 1 → R, mono → both
            const std::uint32_t srcCh = std::min(outCh, static_cast<std::uint32_t>(channels - 1));
            const std::uint8_t* p = raw.data() + f * bytesPerFrame + srcCh * bytesPerSample;
            float s = 0.0f;
            if (format == 3 && bitsPerSample == 32) {
                std::memcpy(&s, p, 4);
            } else if (format == 1 && bitsPerSample == 16) {
                const auto v = static_cast<std::int16_t>(p[0] | (p[1] << 8));
                s = v / 32768.0f;
            } else if (format == 1 && bitsPerSample == 24) {
                std::int32_t v = static_cast<std::int32_t>(p[0]) |
                                 (static_cast<std::int32_t>(p[1]) << 8) |
                                 (static_cast<std::int32_t>(p[2]) << 16);
                if (v & 0x800000) v -= 0x1000000;
                s = v / 8388608.0f;
            } else if (format == 1 && bitsPerSample == 32) {
                std::int32_t v;
                std::memcpy(&v, p, 4);
                s = static_cast<float>(v) / 2147483648.0f;
            }
            result.samples[static_cast<std::size_t>(f) * 2 + outCh] = s;
        }
    }
    return result;
}

} // namespace transmission
