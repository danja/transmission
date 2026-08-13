#include "transmission/SmfWriter.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <vector>

namespace transmission {
namespace {

constexpr std::uint16_t PPQ = 480;

void writeU32BE(std::vector<std::uint8_t>& buf, std::uint32_t v) {
    buf.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
    buf.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
    buf.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    buf.push_back(static_cast<std::uint8_t>(v & 0xFF));
}

void writeU16BE(std::vector<std::uint8_t>& buf, std::uint16_t v) {
    buf.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    buf.push_back(static_cast<std::uint8_t>(v & 0xFF));
}

void writeVarLen(std::vector<std::uint8_t>& buf, std::uint32_t v) {
    if (v < 0x80) { buf.push_back(static_cast<std::uint8_t>(v)); return; }
    std::uint8_t bytes[4]{};
    int n = 0;
    while (v > 0) { bytes[n++] = static_cast<std::uint8_t>(v & 0x7F); v >>= 7; }
    for (int i = n - 1; i > 0; --i) buf.push_back(bytes[i] | 0x80);
    buf.push_back(bytes[0]);
}

struct RawEvent {
    std::uint32_t tick = 0;
    std::vector<std::uint8_t> bytes;
};

std::uint32_t beatToTick(double beat) {
    return static_cast<std::uint32_t>(std::round(beat * PPQ));
}

std::vector<std::uint8_t> eventsToTrack(std::vector<RawEvent> events) {
    std::vector<std::uint8_t> data;
    std::uint32_t currentTick = 0;
    for (const auto& ev : events) {
        const auto delta = ev.tick >= currentTick ? ev.tick - currentTick : 0U;
        currentTick = ev.tick;
        writeVarLen(data, delta);
        data.insert(data.end(), ev.bytes.begin(), ev.bytes.end());
    }
    std::vector<std::uint8_t> chunk;
    chunk.push_back('M'); chunk.push_back('T');
    chunk.push_back('r'); chunk.push_back('k');
    writeU32BE(chunk, static_cast<std::uint32_t>(data.size()));
    chunk.insert(chunk.end(), data.begin(), data.end());
    return chunk;
}

std::vector<std::uint8_t> buildTempoTrack(double bpm) {
    const auto usPerBeat = static_cast<std::uint32_t>(
        std::round(60'000'000.0 / (bpm > 0.0 ? bpm : 120.0)));
    std::vector<RawEvent> events;
    events.push_back({0, {0xFF, 0x03, 0x05, 'T', 'e', 'm', 'p', 'o'}});
    events.push_back({0, {0xFF, 0x51, 0x03,
        static_cast<std::uint8_t>((usPerBeat >> 16) & 0xFF),
        static_cast<std::uint8_t>((usPerBeat >> 8) & 0xFF),
        static_cast<std::uint8_t>(usPerBeat & 0xFF)}});
    events.push_back({0, {0xFF, 0x2F, 0x00}});
    return eventsToTrack(std::move(events));
}

std::vector<std::uint8_t> buildNoteTrack(
    const std::string& nodeId,
    const std::vector<const UiProjectMidiClip*>& clips)
{
    const auto slash = nodeId.rfind('/');
    const auto label = slash != std::string::npos
        ? nodeId.substr(slash + 1) : nodeId;
    const auto nameLen = static_cast<std::uint8_t>(
        std::min(label.size(), std::size_t{127}));
    std::vector<RawEvent> events;
    std::vector<std::uint8_t> nameBytes = {0xFF, 0x03, nameLen};
    nameBytes.insert(nameBytes.end(), label.begin(), label.begin() + nameLen);
    events.push_back({0, std::move(nameBytes)});

    for (const auto* clip : clips) {
        for (const auto& note : clip->notes) {
            const double absStart = clip->startBeat + note.startBeat;
            const double absEnd = absStart + note.durationBeats;
            const auto ch = static_cast<std::uint8_t>(note.channel & 0x0F);
            events.push_back({beatToTick(absStart),
                {static_cast<std::uint8_t>(0x90 | ch), note.pitch, note.velocity}});
            events.push_back({beatToTick(absEnd),
                {static_cast<std::uint8_t>(0x80 | ch), note.pitch, 0x00}});
        }
    }

    // Sort by tick; note-offs before note-ons at the same tick.
    std::stable_sort(events.begin() + 1, events.end(),
        [](const RawEvent& a, const RawEvent& b) {
            if (a.tick != b.tick) return a.tick < b.tick;
            const bool aOff = !a.bytes.empty() && (a.bytes[0] & 0xF0) == 0x80;
            const bool bOff = !b.bytes.empty() && (b.bytes[0] & 0xF0) == 0x80;
            return aOff && !bOff;
        });

    const auto lastTick = events.empty() ? 0U : events.back().tick;
    events.push_back({lastTick, {0xFF, 0x2F, 0x00}});
    return eventsToTrack(std::move(events));
}

} // namespace

std::vector<std::uint8_t> arrangementToSmf(
    const std::vector<UiProjectMidiClip>& clips, double bpm)
{
    std::map<std::string, std::vector<const UiProjectMidiClip*>> byNode;
    for (const auto& clip : clips)
        byNode[clip.targetNodeId].push_back(&clip);

    std::vector<std::vector<std::uint8_t>> tracks;
    tracks.push_back(buildTempoTrack(bpm));
    for (const auto& [nodeId, nodeclips] : byNode)
        tracks.push_back(buildNoteTrack(nodeId, nodeclips));

    const auto numTracks = static_cast<std::uint16_t>(tracks.size());
    std::vector<std::uint8_t> smf;
    smf.push_back('M'); smf.push_back('T');
    smf.push_back('h'); smf.push_back('d');
    writeU32BE(smf, 6);
    writeU16BE(smf, numTracks == 1 ? 0 : 1);
    writeU16BE(smf, numTracks);
    writeU16BE(smf, PPQ);
    for (const auto& track : tracks)
        smf.insert(smf.end(), track.begin(), track.end());
    return smf;
}

std::vector<std::uint8_t> buildCapturedTrack(
    const std::string& nodeId,
    const std::vector<const CapturedMidiEvent*>& events)
{
    const auto slash = nodeId.rfind('/');
    const auto label = slash != std::string::npos
        ? nodeId.substr(slash + 1) : nodeId;
    const auto nameLen = static_cast<std::uint8_t>(
        std::min(label.size(), std::size_t{127}));
    std::vector<RawEvent> trackEvents;
    std::vector<std::uint8_t> nameBytes = {0xFF, 0x03, nameLen};
    nameBytes.insert(nameBytes.end(), label.begin(), label.begin() + nameLen);
    trackEvents.push_back({0, std::move(nameBytes)});

    for (const auto* ev : events) {
        if ((ev->status & 0x80U) == 0) continue;
        trackEvents.push_back({beatToTick(ev->beatPosition),
            {ev->status, ev->data1, ev->data2}});
    }

    std::stable_sort(trackEvents.begin() + 1, trackEvents.end(),
        [](const RawEvent& a, const RawEvent& b) {
            return a.tick < b.tick;
        });

    const auto lastTick = trackEvents.size() > 1 ? trackEvents.back().tick : 0U;
    trackEvents.push_back({lastTick, {0xFF, 0x2F, 0x00}});
    return eventsToTrack(std::move(trackEvents));
}

std::string writeSmf(const std::string& path,
                     const std::vector<UiProjectMidiClip>& clips,
                     double bpm)
{
    const auto buffer = arrangementToSmf(clips, bpm);
    const auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec) return "Unable to create directory: " + ec.message();
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return "Unable to open output file: " + path;
    out.write(reinterpret_cast<const char*>(buffer.data()),
              static_cast<std::streamsize>(buffer.size()));
    if (!out) return "Failed to write MIDI file: " + path;
    return {};
}

std::string writeCapturedSmf(const std::string& path,
                             const std::vector<CapturedMidiEvent>& events,
                             double bpm)
{
    std::map<std::string, std::vector<const CapturedMidiEvent*>> byNode;
    for (const auto& ev : events)
        byNode[ev.nodeId].push_back(&ev);

    std::vector<std::vector<std::uint8_t>> tracks;
    tracks.push_back(buildTempoTrack(bpm));
    for (const auto& [nodeId, nodeEvents] : byNode)
        tracks.push_back(buildCapturedTrack(nodeId, nodeEvents));

    const auto numTracks = static_cast<std::uint16_t>(tracks.size());
    std::vector<std::uint8_t> smf;
    smf.push_back('M'); smf.push_back('T');
    smf.push_back('h'); smf.push_back('d');
    writeU32BE(smf, 6);
    writeU16BE(smf, numTracks == 1 ? 0 : 1);
    writeU16BE(smf, numTracks);
    writeU16BE(smf, PPQ);
    for (const auto& track : tracks)
        smf.insert(smf.end(), track.begin(), track.end());

    const auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec) return "Unable to create directory: " + ec.message();
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return "Unable to open output file: " + path;
    out.write(reinterpret_cast<const char*>(smf.data()),
              static_cast<std::streamsize>(smf.size()));
    if (!out) return "Failed to write MIDI file: " + path;
    return {};
}

} // namespace transmission
