// native/include/transmission/SmfReader.h
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace transmission {

struct SmfEvent {
    double beatTime = 0.0;
    std::uint8_t data[3]{};
    std::uint8_t size = 0;
};

struct SmfLoadResult {
    std::vector<SmfEvent> events;
    double lengthBeats = 0.0;
    std::uint16_t trackCount = 0;
    std::string error;
};

namespace smf_detail {

inline std::uint32_t readBE32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
           (std::uint32_t(p[2]) << 8)  |  std::uint32_t(p[3]);
}
inline std::uint16_t readBE16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>((p[0] << 8) | p[1]);
}

inline std::uint32_t readVlq(const std::uint8_t*& p, const std::uint8_t* end) {
    std::uint32_t v = 0;
    for (int i = 0; i < 4 && p < end; ++i) {
        const std::uint8_t b = *p++;
        v = (v << 7) | (b & 0x7Fu);
        if (!(b & 0x80u)) break;
    }
    return v;
}

// Parse one SMF track chunk, appending note/CC/PC events to `out`.
// ticksPerBeat: from file header.
// microsecondsPerBeat: updated in-place when tempo meta events are encountered.
// Returns the maximum beat time seen in this track.
inline double parseTrack(const std::uint8_t* data, std::size_t size,
                         std::uint32_t ticksPerBeat,
                         double& microsecondsPerBeat,
                         std::vector<SmfEvent>& out) {
    const std::uint8_t* p   = data;
    const std::uint8_t* end = data + size;
    std::uint64_t tick = 0;
    std::uint8_t  runningStatus = 0;
    double maxBeat = 0.0;

    while (p < end) {
        const std::uint32_t delta = readVlq(p, end);
        tick += delta;
        const double beat = static_cast<double>(tick) / static_cast<double>(ticksPerBeat);
        if (beat > maxBeat) maxBeat = beat;

        if (p >= end) break;
        const std::uint8_t first = *p;

        if (first == 0xFFu) {
            // Meta event
            ++p;
            if (p >= end) break;
            const std::uint8_t metaType = *p++;
            const std::uint32_t len = readVlq(p, end);
            if (p + len > end) break;
            if (metaType == 0x51u && len == 3) {
                const std::uint32_t us = (std::uint32_t(p[0]) << 16) |
                                         (std::uint32_t(p[1]) << 8)  |
                                          std::uint32_t(p[2]);
                microsecondsPerBeat = us > 0u ? static_cast<double>(us) : 500000.0;
            }
            p += len;
            runningStatus = 0;
        } else if (first == 0xF0u || first == 0xF7u) {
            // SysEx
            ++p;
            const std::uint32_t len = readVlq(p, end);
            if (p + len > end) break;
            p += len;
            runningStatus = 0;
        } else {
            // Channel message (possibly with running status)
            std::uint8_t status;
            if (first & 0x80u) {
                status = first;
                runningStatus = first;
                ++p;
            } else {
                status = runningStatus;
            }
            if (status == 0u) break;

            SmfEvent ev{};
            ev.beatTime = beat;
            ev.data[0]  = status;
            const std::uint8_t type = status & 0xF0u;

            if (type == 0xC0u || type == 0xD0u) {
                if (p >= end) break;
                ev.data[1] = *p++;
                ev.size = 2;
            } else if (type == 0x80u || type == 0x90u ||
                       type == 0xA0u || type == 0xB0u || type == 0xE0u) {
                if (p + 1 > end) break;
                ev.data[1] = *p++;
                ev.data[2] = *p++;
                ev.size = 3;
                // Normalise note-on with velocity 0 → note-off
                if (type == 0x90u && ev.data[2] == 0u)
                    ev.data[0] = (status & 0x0Fu) | 0x80u;
            } else {
                // Real-time byte inside a track; skip
                continue;
            }

            out.push_back(ev);
        }
    }
    return maxBeat;
}

} // namespace smf_detail

inline SmfLoadResult loadSmf(const std::string& path) {
    SmfLoadResult result;
    std::ifstream file(path, std::ios::binary);
    if (!file) { result.error = "Cannot open: " + path; return result; }

    std::vector<std::uint8_t> data(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());

    if (data.size() < 14u) { result.error = "File too small"; return result; }
    const auto* raw = data.data();
    if (std::memcmp(raw, "MThd", 4) != 0) { result.error = "Not a MIDI file"; return result; }

    const std::uint32_t headerLen = smf_detail::readBE32(raw + 4);
    if (headerLen < 6u || 8u + headerLen > data.size())
        { result.error = "Invalid header"; return result; }

    // const std::uint16_t format   = smf_detail::readBE16(raw + 8);
    const std::uint16_t numTracks = smf_detail::readBE16(raw + 10);
    const std::uint16_t division  = smf_detail::readBE16(raw + 12);

    if (division & 0x8000u) { result.error = "SMPTE time not supported"; return result; }
    if (division == 0u) { result.error = "Zero ticks-per-beat"; return result; }

    result.trackCount = numTracks;
    double microsecondsPerBeat = 500000.0; // 120 BPM default

    std::size_t offset = 8 + headerLen;
    for (std::uint16_t t = 0; t < numTracks && offset + 8 <= data.size(); ++t) {
        if (std::memcmp(data.data() + offset, "MTrk", 4) != 0) break;
        const std::uint32_t trackLen = smf_detail::readBE32(data.data() + offset + 4);
        offset += 8;
        if (offset + trackLen > data.size()) break;
        const double trackEnd = smf_detail::parseTrack(
            data.data() + offset, trackLen,
            static_cast<std::uint32_t>(division),
            microsecondsPerBeat, result.events);
        if (trackEnd > result.lengthBeats) result.lengthBeats = trackEnd;
        offset += trackLen;
    }

    std::stable_sort(result.events.begin(), result.events.end(),
        [](const SmfEvent& a, const SmfEvent& b) { return a.beatTime < b.beatTime; });

    if (result.lengthBeats <= 0.0) result.lengthBeats = 16.0;
    return result;
}

} // namespace transmission
