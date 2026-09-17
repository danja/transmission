// native/src/JigdawProcessor.cpp

#include "transmission/JigdawProcessor.h"

#include "jigdaw/Abi.hpp"
#include "jigdaw/Chain.hpp"
#include "jigdaw/Fetch.hpp"
#include "jigdaw/Integrity.hpp"
#include "jigdaw/Midi.hpp"
#include "jigdaw/Module.hpp"
#include "jigdaw/Profile.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>

namespace transmission {
namespace {

/**
 * Transmission counts a bar as four beats everywhere (the loop length is in
 * bars and is turned into beats by multiplying by four), and its timeline
 * starts at bar 1 beat 1. Bar, beat and tick are therefore the host's own
 * counting rather than something derived behind the module's back, which is
 * what jig:Abi2 requires before a host may set the BBT valid bit.
 */
constexpr int beatsPerBar = 4;
constexpr std::uint32_t ticksPerBeat = 960;

double denormalize(const jigdaw::Port& port, double normalized) noexcept {
    const double clamped = std::clamp(normalized, 0.0, 1.0);
    const double value = port.minimum +
                         clamped * (static_cast<double>(port.maximum) - port.minimum);
    // A switch or an enumeration names integers, and a value between two of
    // them means whichever is nearer rather than something in between.
    if (port.toggled || port.enumeration) return std::round(value);
    return value;
}

void describe(const jigdaw::Profile& profile, JigdawPluginTopology& topology) {
    topology.iri = profile.iri;
    topology.name = profile.label;
    topology.comment = profile.comment;
    topology.vendor = profile.vendor;
    topology.abi = profile.abi;
    // jig:audioOutputs counts ports in the Web Audio sense; jig:outputChannels
    // counts the channels in them, which is what this engine routes.
    topology.audioInputs = profile.audioInputs > 0
        ? static_cast<std::size_t>(std::max(0, profile.inputChannels)) : 0;
    topology.audioOutputs = profile.audioOutputs > 0
        ? static_cast<std::size_t>(std::max(0, profile.outputChannels)) : 0;
    topology.midiInputs = profile.acceptsMidi() ? 1 : 0;
    topology.midiOutputs = profile.producesMidi() ? 1 : 0;
    topology.requiresTransport = profile.requiresTransport();
    topology.parameters.clear();
    for (const auto& port : profile.portsByIndex()) {
        JigdawParameterDescriptor descriptor;
        descriptor.id = static_cast<std::uint32_t>(std::max(0, port.index));
        descriptor.symbol = port.symbol;
        descriptor.name = port.name;
        descriptor.unit = port.unit;
        descriptor.minimum = port.minimum;
        descriptor.maximum = port.maximum;
        descriptor.defaultValue = port.defaultValue;
        descriptor.toggled = port.toggled;
        descriptor.enumeration = port.enumeration;
        for (const auto& point : port.scalePoints)
            descriptor.scalePoints.push_back({point.label, point.value});
        topology.parameters.push_back(std::move(descriptor));
    }
}

} // namespace

struct JigdawProcessor::Impl {
    static constexpr std::size_t maxPendingParameters = 64;

    jigdaw::Profile profile;
    jigdaw::Module module;
    JigdawPluginTopology topology;
    std::vector<jigdaw::Port> ports;          ///< sorted by jig:paramIndex
    std::vector<double> appliedValues;        ///< parallel to ports, denormalized
    bool loaded = false;

    double sampleRate = 48000.0;
    std::uint32_t maxFrames = 0;

    AudioProcessContext context;

    // Preallocated at initialize, never resized while processing.
    std::array<jigdaw::MidiEvent, maxMidiEventsPerBlock> chunkMidi{};
    std::array<MidiEvent, maxMidiEventsPerBlock> outputMidi{};
    std::size_t outputMidiCount = 0;

    std::array<std::atomic<std::uint8_t>, maxPendingParameters> pendingSlots{};
    std::array<std::atomic<std::uint32_t>, maxPendingParameters> pendingIds{};
    std::array<std::atomic<double>, maxPendingParameters> pendingValues{};

    std::size_t indexOf(std::uint32_t parameterId) const noexcept {
        for (std::size_t index = 0; index < ports.size(); ++index)
            if (ports[index].index >= 0 &&
                static_cast<std::uint32_t>(ports[index].index) == parameterId)
                return index;
        return ports.size();
    }
};

JigdawProcessor::JigdawProcessor() : impl_(std::make_unique<Impl>()) {}
JigdawProcessor::~JigdawProcessor() = default;

bool JigdawInspector::inspectTopology(const std::string& pluginIri,
                                      JigdawPluginTopology& topology,
                                      std::string& error) const {
    jigdaw::Profile profile;
    error = jigdaw::Chain::fetchProfile(pluginIri, profile);
    if (!error.empty()) return false;
    if (profile.abi != jigdaw::kAbi1 && profile.abi != jigdaw::kAbi2) {
        error = profile.abi.empty()
            ? "this plugin declares no jig:abi, so its module is private to its "
              "JavaScript processor and this host cannot load it"
            : "unsupported JigDAW ABI: " + profile.abi;
        return false;
    }
    describe(profile, topology);
    return true;
}

bool JigdawProcessor::initialize(const std::string& pluginIri, std::size_t blockSize,
                                 double sampleRate, std::string& error) {
    if (pluginIri.empty() || blockSize == 0 || sampleRate <= 0.0) {
        error = "JigDAW plugin IRI, block size and sample rate must be valid";
        return false;
    }
    auto& impl = *impl_;
    impl.loaded = false;
    impl.sampleRate = sampleRate;

    error = jigdaw::Chain::fetchProfile(pluginIri, impl.profile);
    if (!error.empty()) return false;
    if (!impl.profile.module) {
        error = impl.profile.label +
                " declares no WebAssembly module, so there is nothing to run here";
        return false;
    }

    const auto wasm = jigdaw::fetchUrl(impl.profile.module->location);
    if (!wasm.ok) {
        error = wasm.error;
        return false;
    }
    // Contract section 3.2: verified before anything is instantiated, with no
    // way to skip. An unverified profile is an instruction to execute whatever
    // currently sits at a URL.
    if (auto bad = jigdaw::verifyIntegrity(wasm.bytes, impl.profile.module->integrity);
        !bad.empty()) {
        error = impl.profile.label + ": " + bad;
        return false;
    }

    error = impl.module.load(wasm.bytes, impl.profile, sampleRate);
    if (!error.empty()) return false;

    impl.maxFrames = impl.module.maxFrames();
    if (impl.maxFrames == 0) {
        error = impl.profile.label + ": the module reports a maximum of zero frames";
        return false;
    }

    describe(impl.profile, impl.topology);
    impl.ports = impl.profile.portsByIndex();
    // Module::load has already written every declared default, so the cache
    // starts out agreeing with the module and the first block sets nothing.
    impl.appliedValues.assign(impl.ports.size(), 0.0);
    for (std::size_t index = 0; index < impl.ports.size(); ++index)
        impl.appliedValues[index] = impl.ports[index].defaultValue;

    for (auto& slot : impl.pendingSlots) slot.store(0, std::memory_order_relaxed);
    impl.outputMidiCount = 0;
    impl.loaded = true;
    return true;
}

bool JigdawProcessor::ready() const noexcept {
    return impl_ && impl_->loaded && impl_->module.ready();
}

const std::string& JigdawProcessor::pluginName() const noexcept {
    return impl_->topology.name;
}

const JigdawPluginTopology& JigdawProcessor::topology() const noexcept {
    return impl_->topology;
}

bool JigdawProcessor::setParameter(std::uint32_t parameterId, double normalizedValue,
                                   std::string& error) {
    if (!ready()) {
        error = "JigDAW processor is not initialized";
        return false;
    }
    if (impl_->indexOf(parameterId) == impl_->ports.size()) {
        error = "unknown JigDAW parameter index: " + std::to_string(parameterId);
        return false;
    }
    if (!enqueueParameter(parameterId, normalizedValue)) {
        error = "JigDAW parameter queue is full";
        return false;
    }
    return true;
}

bool JigdawProcessor::enqueueParameter(std::uint32_t parameterId,
                                       double normalizedValue) noexcept {
    if (!ready() || normalizedValue < 0.0 || normalizedValue > 1.0) return false;
    for (std::size_t index = 0; index < Impl::maxPendingParameters; ++index) {
        std::uint8_t available = 0;
        if (impl_->pendingSlots[index].compare_exchange_strong(
                available, 1, std::memory_order_acquire, std::memory_order_relaxed)) {
            impl_->pendingIds[index].store(parameterId, std::memory_order_relaxed);
            impl_->pendingValues[index].store(normalizedValue, std::memory_order_relaxed);
            impl_->pendingSlots[index].store(2, std::memory_order_release);
            return true;
        }
    }
    return false;
}

void JigdawProcessor::applyPendingParameters() noexcept {
    if (!ready()) return;
    auto& impl = *impl_;
    for (std::size_t slot = 0; slot < Impl::maxPendingParameters; ++slot) {
        std::uint8_t filled = 2;
        if (!impl.pendingSlots[slot].compare_exchange_strong(
                filled, 0, std::memory_order_acquire, std::memory_order_relaxed))
            continue;
        const auto parameterId = impl.pendingIds[slot].load(std::memory_order_relaxed);
        const auto normalized = impl.pendingValues[slot].load(std::memory_order_relaxed);
        const auto index = impl.indexOf(parameterId);
        if (index == impl.ports.size()) continue;
        const double value = denormalize(impl.ports[index], normalized);
        // docs/module-abi.md: write only on a change. Some plugins retune delay
        // lines on a parameter write, and doing that every block is wasteful.
        if (value == impl.appliedValues[index]) continue;
        impl.appliedValues[index] = value;
        impl.module.setParam(parameterId, static_cast<float>(value));
    }
}

void JigdawProcessor::setProcessContext(const AudioProcessContext& context) noexcept {
    impl_->context = context;
}

std::size_t JigdawProcessor::takeOutputMidi(MidiEvent* events,
                                            std::size_t capacity) noexcept {
    if (!events || capacity == 0) return 0;
    auto& impl = *impl_;
    const auto count = std::min(capacity, impl.outputMidiCount);
    for (std::size_t index = 0; index < count; ++index) events[index] = impl.outputMidi[index];
    impl.outputMidiCount = 0;
    return count;
}

bool JigdawProcessor::reconfigure(std::size_t frames, std::string& error) {
    // A block of any size is split into sub-blocks of at most jig_max_frames(),
    // so a device block size change needs nothing from the module.
    if (frames == 0) {
        error = "block size must be positive";
        return false;
    }
    return true;
}

void JigdawProcessor::process(const float* const* inputs, float* const* outputs,
                              std::size_t channels, std::size_t frames) noexcept {
    processWithMidi(inputs, channels, outputs, channels, frames, nullptr, 0);
}

void JigdawProcessor::process(const float* const* inputs, std::size_t inputChannels,
                              float* const* outputs, std::size_t outputChannels,
                              std::size_t frames) noexcept {
    processWithMidi(inputs, inputChannels, outputs, outputChannels, frames, nullptr, 0);
}

void JigdawProcessor::processWithMidi(const float* const* inputs, float* const* outputs,
                                      std::size_t channels, std::size_t frames,
                                      const MidiEvent* events,
                                      std::size_t eventCount) noexcept {
    processWithMidi(inputs, channels, outputs, channels, frames, events, eventCount);
}

void JigdawProcessor::processWithMidi(const float* const* inputs,
                                      std::size_t inputChannels,
                                      float* const* outputs,
                                      std::size_t outputChannels,
                                      std::size_t frames, const MidiEvent* events,
                                      std::size_t eventCount) noexcept {
    auto& impl = *impl_;
    impl.outputMidiCount = 0;
    if (outputs)
        for (std::size_t channel = 0; channel < outputChannels; ++channel)
            if (outputs[channel]) std::fill_n(outputs[channel], frames, 0.0F);
    if (!ready() || frames == 0) return;

    const auto moduleInputs = impl.topology.audioInputs;
    const auto moduleOutputs = impl.topology.audioOutputs;
    const double beatsPerFrame = impl.context.tempo > 0.0
        ? impl.context.tempo / (60.0 * impl.sampleRate) : 0.0;

    for (std::size_t offset = 0; offset < frames;) {
        const std::size_t chunk = std::min<std::size_t>(impl.maxFrames, frames - offset);

        if (auto* transport = impl.module.transport()) {
            const double beat = impl.context.projectTimeMusic +
                                beatsPerFrame * static_cast<double>(offset);
            const double barStart =
                std::floor(beat / beatsPerBar) * static_cast<double>(beatsPerBar);
            transport->playing = impl.context.playing ? 1u : 0u;
            transport->ticksPerBeat = ticksPerBeat;
            transport->bpm = impl.context.tempo;
            transport->beat = beat;
            transport->barStartBeat = barStart;
            transport->bar = static_cast<std::int32_t>(std::floor(beat / beatsPerBar)) + 1;
            transport->beatInBar =
                static_cast<std::int32_t>(std::floor(beat - barStart)) + 1;
            transport->tick = static_cast<std::int32_t>(
                (beat - std::floor(beat)) * static_cast<double>(ticksPerBeat));
            transport->numerator = beatsPerBar;
            transport->denominator = 4;
            // No seconds bit: this engine has a tempo map, so wall-clock time
            // is not the current tempo times the beat, and a host that has not
            // got a field says so rather than guessing at it.
            transport->seconds = 0.0;
            transport->valid = jigdaw::kTransportBpm | jigdaw::kTransportBeat |
                               jigdaw::kTransportBbt | jigdaw::kTransportMeter;
        }

        if (impl.module.hasMidi()) {
            std::size_t chunkCount = 0;
            for (std::size_t index = 0; index < eventCount; ++index) {
                const auto& event = events[index];
                if (event.frameOffset < offset || event.frameOffset >= offset + chunk)
                    continue;
                if (event.size < 1 || event.size > 3) continue;
                if (chunkCount >= impl.chunkMidi.size()) break;
                auto& target = impl.chunkMidi[chunkCount++];
                target.frame = static_cast<std::uint32_t>(event.frameOffset - offset);
                target.size = event.size;
                target.data[0] = event.data[0];
                target.data[1] = event.data[1];
                target.data[2] = event.data[2];
            }
            if (impl.module.hasMidiIn())
                impl.module.sendMidi(impl.chunkMidi.data(),
                                     static_cast<std::uint32_t>(chunkCount));
            else
                // A version 1 module knows notes and nothing else, and the
                // decoding that turns a message into one lives in jigdaw::Midi
                // so that both ABIs share it.
                for (std::size_t index = 0; index < chunkCount; ++index)
                    jigdaw::applyMidi(impl.module, impl.chunkMidi[index].data,
                                      impl.chunkMidi[index].size);
        }

        if (moduleInputs > 0 && inputs) {
            for (std::size_t channel = 0; channel < moduleInputs; ++channel) {
                float* into = impl.module.input(static_cast<std::uint32_t>(channel));
                if (!into) continue;
                // A mono source feeding a stereo module repeats its one channel
                // rather than leaving the other silent.
                const std::size_t source = inputChannels == 0
                    ? 0 : std::min(channel, inputChannels - 1);
                if (inputChannels == 0 || !inputs[source]) {
                    std::fill_n(into, chunk, 0.0F);
                    continue;
                }
                std::memcpy(into, inputs[source] + offset, chunk * sizeof(float));
            }
        }

        impl.module.process(static_cast<std::uint32_t>(chunk));

        if (moduleOutputs > 0 && outputs) {
            for (std::size_t channel = 0; channel < outputChannels; ++channel) {
                if (!outputs[channel]) continue;
                const float* from = impl.module.output(static_cast<std::uint32_t>(channel));
                if (!from) continue;
                std::memcpy(outputs[channel] + offset, from, chunk * sizeof(float));
            }
        }

        if (impl.module.hasMidiOut()) {
            std::uint32_t produced = 0;
            const auto* emitted = impl.module.midiOut(produced);
            for (std::uint32_t index = 0; emitted && index < produced; ++index) {
                const auto& event = emitted[index];
                if (event.size < 1 || event.size > 3) continue;   // the ABI says ignore
                if (impl.outputMidiCount >= impl.outputMidi.size()) break;
                auto& target = impl.outputMidi[impl.outputMidiCount++];
                target.frameOffset =
                    offset + std::min<std::size_t>(event.frame, chunk - 1);
                target.port = 0;
                target.size = event.size;
                target.data = {event.data[0], event.data[1], event.data[2]};
            }
        }

        offset += chunk;
    }
}

} // namespace transmission
