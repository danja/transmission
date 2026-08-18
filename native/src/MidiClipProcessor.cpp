// native/src/MidiClipProcessor.cpp

#include "transmission/MidiClipProcessor.h"

#include <algorithm>
#include <cmath>

namespace transmission {

bool MidiClipProcessor::load(const std::string& path, std::string& error) {
    auto smf = loadSmf(path);
    if (!smf.error.empty()) { error = smf.error; return false; }
    events_    = std::move(smf.events);
    lengthBeats_ = smf.lengthBeats;
    trackCount_ = smf.trackCount;
    return true;
}

bool MidiClipProcessor::reconfigure(std::size_t frames, std::string&) {
    blockFrames_ = frames > 0 ? frames : 256;
    return true;
}

void MidiClipProcessor::setProcessContext(const AudioProcessContext& ctx) noexcept {
    context_ = ctx;
}

void MidiClipProcessor::collectEvents(double blockStart, double blockEnd,
                                      std::size_t frames) noexcept {
    if (lengthBeats_ <= 0.0 || events_.empty()) return;
    outputCount_ = 0;

    // Both blockStart and blockEnd are already in clip-local beat space (fmod applied).
    // Handle the wrap-around within a single block.
    const bool wraps = blockEnd >= lengthBeats_;

    const auto emit = [&](const SmfEvent& ev, double clipBeat) {
        if (outputCount_ >= maxMidiEventsPerBlock) return;
        // frame offset within this block
        const double beatOffset = clipBeat - blockStart;
        const double blockBeats = blockEnd - blockStart;
        const std::size_t frameOff = blockBeats > 0.0
            ? static_cast<std::size_t>(std::clamp(
                  beatOffset / blockBeats * static_cast<double>(frames),
                  0.0, static_cast<double>(frames - 1)))
            : 0;
        auto& out      = outputBuffer_[outputCount_++];
        out.frameOffset = frameOff;
        out.port        = 0;
        out.size        = ev.size;
        out.data        = {ev.data[0], ev.data[1], ev.data[2]};
    };

    for (const auto& ev : events_) {
        if (ev.beatTime >= blockStart && ev.beatTime < blockEnd && !wraps) {
            emit(ev, ev.beatTime);
        }
    }
    if (wraps) {
        // First segment: [blockStart, lengthBeats_)
        for (const auto& ev : events_)
            if (ev.beatTime >= blockStart && ev.beatTime < lengthBeats_)
                emit(ev, ev.beatTime);
        // Second segment: [0, blockEnd - lengthBeats_)
        const double wrapEnd = blockEnd - lengthBeats_;
        for (const auto& ev : events_)
            if (ev.beatTime < wrapEnd)
                emit(ev, lengthBeats_ + ev.beatTime); // offset for frame calc
    }
}

void MidiClipProcessor::process(const float* const* /*inputs*/,
                                float* const* outputs,
                                std::size_t channels,
                                std::size_t frames) noexcept {
    for (std::size_t ch = 0; ch < channels; ++ch)
        if (outputs && outputs[ch]) std::fill_n(outputs[ch], frames, 0.0f);
    outputCount_ = 0;
    if (!context_.playing || lengthBeats_ <= 0.0 || events_.empty()) return;

    const double beatsPerFrame = (context_.tempo > 0.0 && sampleRate_ > 0.0)
        ? context_.tempo / (60.0 * sampleRate_) : 0.0;
    const double blockBeats = beatsPerFrame * static_cast<double>(frames);
    const double clipStart  = std::fmod(context_.projectTimeMusic, lengthBeats_);
    collectEvents(clipStart, clipStart + blockBeats, frames);
}

void MidiClipProcessor::processWithMidi(const float* const* inputs, float* const* outputs,
                                        std::size_t channels, std::size_t frames,
                                        const MidiEvent* /*events*/,
                                        std::size_t /*eventCount*/) noexcept {
    process(inputs, outputs, channels, frames);
}

std::size_t MidiClipProcessor::takeOutputMidi(MidiEvent* events,
                                              std::size_t capacity) noexcept {
    const std::size_t count = std::min(outputCount_, capacity);
    if (events) std::copy_n(outputBuffer_.data(), count, events);
    outputCount_ = 0;
    return count;
}

} // namespace transmission
