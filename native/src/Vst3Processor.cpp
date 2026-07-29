#include "transmission/Vst3Processor.h"

#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <utility>
#include <vector>

namespace transmission {

struct Vst3Processor::Impl {
    VST3::Hosting::Module::Ptr module;
    Steinberg::Vst::HostApplication hostApplication;
    std::unique_ptr<Steinberg::Vst::PlugProvider> provider;
    Steinberg::IPtr<Steinberg::Vst::IComponent> component;
    Steinberg::FUnknownPtr<Steinberg::Vst::IAudioProcessor> processor;
    Steinberg::Vst::HostProcessData processData;
    Steinberg::Vst::EventList inputEvents{256};
    Steinberg::Vst::EventList outputEvents{256};
    Steinberg::Vst::ParameterChanges parameterChanges{256};
    Steinberg::Vst::ParameterChanges outputParameterChanges{256};
    Steinberg::Vst::ProcessContext processContext{};
    std::string name;
    std::size_t inputChannels = 0;
    std::size_t outputChannels = 0;
    std::vector<std::pair<Steinberg::int32, Steinberg::int32>> inputLocations;
    std::vector<std::pair<Steinberg::int32, Steinberg::int32>> outputLocations;
    std::size_t frames = 0;
    bool active = false;
    bool processing = false;
    bool hasAudioInput = false;
    bool hasParameterChanges = false;
    static constexpr std::size_t maxPendingParameters = 256;
    // 0 = free, 1 = producer writing, 2 = ready for the audio thread.
    std::array<std::atomic<std::uint8_t>, maxPendingParameters> pendingParameterSlots{};
    std::array<std::atomic<std::uint32_t>, maxPendingParameters> pendingParameterIds{};
    std::array<std::atomic<double>, maxPendingParameters> pendingParameterValues{};

    ~Impl() {
        if (processing && processor) processor->setProcessing(false);
        if (active && component) component->setActive(false);
        processData.unprepare();
        // Release queried interfaces before the provider unloads the module.
        processor = nullptr;
        component = nullptr;
        provider.reset();
        module.reset();
    }
};

Vst3Processor::Vst3Processor() = default;
Vst3Processor::~Vst3Processor() = default;

bool Vst3Processor::initialize(const std::string& modulePath, std::size_t channels,
                               std::size_t frames, double sampleRate,
                               std::string& error) {
    return initialize(modulePath, channels, channels, frames, sampleRate, error);
}

bool Vst3Processor::initialize(const std::string& modulePath,
                               std::size_t inputChannels,
                               std::size_t outputChannels,
                               std::size_t frames, double sampleRate,
                               std::string& error) {
    impl_.reset();
    if (modulePath.empty() || outputChannels == 0 || frames == 0 || sampleRate <= 0.0) {
        error = "module path, output channels, frames, and sample rate must be valid";
        return false;
    }

    auto candidate = std::make_unique<Impl>();
    candidate->module = VST3::Hosting::Module::create(modulePath, error);
    if (!candidate->module) return false;

    Steinberg::Vst::PluginContextFactory::instance().setPluginContext(
        &candidate->hostApplication);
    VST3::Hosting::ClassInfo selected;
    bool found = false;
    for (const auto& info : candidate->module->getFactory().classInfos()) {
        if (info.category() == kVstAudioEffectClass) {
            selected = info;
            found = true;
            break;
        }
    }
    if (!found) {
        error = "module has no VST3 audio effect class";
        return false;
    }

    candidate->provider = std::make_unique<Steinberg::Vst::PlugProvider>(
        candidate->module->getFactory(), selected, true);
    if (!candidate->provider->initialize()) {
        error = "failed to initialize VST3 component";
        return false;
    }
    candidate->component = candidate->provider->getComponentPtr();
    candidate->processor = Steinberg::FUnknownPtr<Steinberg::Vst::IAudioProcessor>(
        candidate->component);
    if (!candidate->component || !candidate->processor) {
        error = "VST3 component does not expose IAudioProcessor";
        return false;
    }

    const auto inputBusCount = candidate->component->getBusCount(
        Steinberg::Vst::kAudio, Steinberg::Vst::kInput);
    const auto outputBusCount = candidate->component->getBusCount(
        Steinberg::Vst::kAudio, Steinberg::Vst::kOutput);
    if (outputBusCount < 1) {
        error = "VST3 effect has no audio output bus";
        return false;
    }

    Steinberg::Vst::ProcessSetup setup{};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.maxSamplesPerBlock = static_cast<Steinberg::int32>(frames);
    setup.sampleRate = sampleRate;
    if (candidate->processor->setupProcessing(setup) != Steinberg::kResultOk) {
        error = "VST3 setupProcessing failed";
        return false;
    }
    for (Steinberg::int32 index = 0; index < inputBusCount; ++index) {
        if (candidate->component->activateBus(Steinberg::Vst::kAudio,
                                               Steinberg::Vst::kInput, index, true) != Steinberg::kResultOk) {
            error = "VST3 input bus activation failed";
            return false;
        }
    }
    for (Steinberg::int32 index = 0; index < outputBusCount; ++index) {
        if (candidate->component->activateBus(Steinberg::Vst::kAudio,
                                               Steinberg::Vst::kOutput, index, true) != Steinberg::kResultOk) {
            error = "VST3 output bus activation failed";
            return false;
        }
    }
    if (!candidate->processData.prepare(*candidate->component, 0,
                                        Steinberg::Vst::kSample32)) {
        error = "failed to prepare VST3 process buffers";
        return false;
    }
    candidate->hasAudioInput = inputBusCount > 0 && candidate->processData.numInputs > 0;
    for (Steinberg::int32 bus = 0; bus < candidate->processData.numInputs; ++bus)
        for (Steinberg::int32 channel = 0;
             channel < candidate->processData.inputs[bus].numChannels; ++channel)
            candidate->inputLocations.emplace_back(bus, channel);
    for (Steinberg::int32 bus = 0; bus < candidate->processData.numOutputs; ++bus)
        for (Steinberg::int32 channel = 0;
             channel < candidate->processData.outputs[bus].numChannels; ++channel)
            candidate->outputLocations.emplace_back(bus, channel);
    if (candidate->outputLocations.size() < outputChannels ||
        candidate->inputLocations.size() < inputChannels) {
        error = "VST3 effect does not provide the requested channel count";
        return false;
    }
    candidate->inputLocations.resize(inputChannels);
    candidate->outputLocations.resize(outputChannels);
    if (candidate->component->setActive(true) != Steinberg::kResultOk) {
        error = "VST3 component activation failed";
        return false;
    }
    candidate->active = true;
    const auto processingResult = candidate->processor->setProcessing(true);
    if (processingResult != Steinberg::kResultOk &&
        processingResult != Steinberg::kNotImplemented &&
        processingResult != Steinberg::kInvalidArgument) {
        error = "VST3 processor activation failed";
        return false;
    }
    candidate->processing = true;
    candidate->name = selected.name();
    candidate->inputChannels = inputChannels;
    candidate->outputChannels = outputChannels;
    candidate->frames = frames;
    candidate->processContext.sampleRate = sampleRate;
    candidate->processContext.state = Steinberg::Vst::ProcessContext::kPlaying |
                                      Steinberg::Vst::ProcessContext::kTempoValid |
                                      Steinberg::Vst::ProcessContext::kProjectTimeMusicValid |
                                      Steinberg::Vst::ProcessContext::kTimeSigValid;
    candidate->processContext.tempo = 120.0;
    candidate->processContext.projectTimeMusic = 0.0;
    candidate->processContext.timeSigNumerator = 4;
    candidate->processContext.timeSigDenominator = 4;
    impl_ = std::move(candidate);
    return true;
}

bool Vst3Processor::ready() const noexcept { return impl_ && impl_->processing; }

const std::string& Vst3Processor::pluginName() const noexcept {
    static const std::string empty;
    return impl_ ? impl_->name : empty;
}

bool Vst3Processor::setParameter(std::uint32_t parameterId, double normalizedValue,
                                 std::string& error) {
    if (!ready()) {
        error = "VST3 processor is not initialized";
        return false;
    }
    if (normalizedValue < 0.0 || normalizedValue > 1.0) {
        error = "VST3 parameter value must be normalized to [0, 1]";
        return false;
    }
    if (!enqueueParameter(parameterId, normalizedValue)) {
        error = "VST3 parameter queue is full";
        return false;
    }
    return true;
}

bool Vst3Processor::enqueueParameter(std::uint32_t parameterId, double normalizedValue) noexcept {
    if (!ready() || normalizedValue < 0.0 || normalizedValue > 1.0) return false;
    for (std::size_t index = 0; index < Impl::maxPendingParameters; ++index) {
        std::uint8_t available = 0;
        if (impl_->pendingParameterSlots[index].compare_exchange_strong(
                available, 1, std::memory_order_acquire, std::memory_order_relaxed)) {
            impl_->pendingParameterIds[index].store(parameterId, std::memory_order_relaxed);
            impl_->pendingParameterValues[index].store(normalizedValue, std::memory_order_relaxed);
            impl_->pendingParameterSlots[index].store(2, std::memory_order_release);
            return true;
        }
    }
    return false;
}

void Vst3Processor::applyPendingParameters() noexcept {
    if (!ready()) return;
    impl_->parameterChanges.clearQueue();
    impl_->hasParameterChanges = false;
    Steinberg::int32 queueIndex = 0;
    for (std::size_t index = 0; index < Impl::maxPendingParameters; ++index) {
        std::uint8_t ready = 2;
        if (!impl_->pendingParameterSlots[index].compare_exchange_strong(
                ready, 0, std::memory_order_acquire, std::memory_order_relaxed)) continue;
        auto* queue = impl_->parameterChanges.addParameterData(
            static_cast<Steinberg::Vst::ParamID>(impl_->pendingParameterIds[index].load(std::memory_order_relaxed)),
            queueIndex);
        if (!queue) continue;
        Steinberg::int32 pointIndex = 0;
        if (queue->addPoint(0, impl_->pendingParameterValues[index].load(std::memory_order_relaxed), pointIndex) ==
            Steinberg::kResultOk) impl_->hasParameterChanges = true;
    }
}

void Vst3Processor::process(const float* const* inputs, float* const* outputs,
                            std::size_t channels, std::size_t frames) noexcept {
    processWithMidi(inputs, outputs, channels, frames, nullptr, 0);
}

void Vst3Processor::processWithMidi(const float* const* inputs, float* const* outputs,
                                    std::size_t channels, std::size_t frames,
                                    const MidiEvent* events, std::size_t eventCount) noexcept {
    if (!impl_ || channels != impl_->inputChannels ||
        channels != impl_->outputChannels) {
        if (outputs)
            for (std::size_t channel = 0; channel < channels; ++channel)
                if (outputs[channel])
                    std::fill_n(outputs[channel], frames, 0.0F);
        return;
    }
    processWithMidi(inputs, impl_->inputChannels, outputs, impl_->outputChannels,
                    frames, events, eventCount);
}

void Vst3Processor::process(const float* const* inputs, std::size_t inputChannels,
                            float* const* outputs, std::size_t outputChannels,
                            std::size_t frames) noexcept {
    processWithMidi(inputs, inputChannels, outputs, outputChannels, frames, nullptr, 0);
}

void Vst3Processor::processWithMidi(
    const float* const* inputs, std::size_t inputChannels,
    float* const* outputs, std::size_t outputChannels, std::size_t frames,
    const MidiEvent* events, std::size_t eventCount) noexcept {
    if (!ready() || (inputChannels != 0 && !inputs) || !outputs ||
        inputChannels != impl_->inputChannels ||
        outputChannels != impl_->outputChannels || frames != impl_->frames) {
        if (outputs) {
            for (std::size_t channel = 0; channel < outputChannels; ++channel)
                if (outputs[channel]) std::fill(outputs[channel], outputs[channel] + frames, 0.0F);
        }
        return;
    }
    impl_->processData.numSamples = static_cast<Steinberg::int32>(frames);
    impl_->processData.processContext = &impl_->processContext;
    for (std::size_t channel = 0; channel < inputChannels; ++channel) {
        if (!inputs[channel]) return;
        const auto [bus, busChannel] = impl_->inputLocations[channel];
        impl_->processData.setChannelBuffer(
            Steinberg::Vst::kInput, bus, busChannel,
            const_cast<float*>(inputs[channel]));
    }
    for (std::size_t channel = 0; channel < outputChannels; ++channel) {
        if (!outputs[channel]) return;
        const auto [bus, busChannel] = impl_->outputLocations[channel];
        impl_->processData.setChannelBuffer(
            Steinberg::Vst::kOutput, bus, busChannel, outputs[channel]);
    }
    impl_->inputEvents.clear();
    impl_->outputEvents.clear();
    impl_->outputParameterChanges.clearQueue();
    for (std::size_t index = 0; index < eventCount; ++index) {
        const auto& midi = events[index];
        if (midi.size < 3) continue;
        const auto status = midi.data[0] & 0xf0;
        const auto channel = static_cast<Steinberg::int16>(midi.data[0] & 0x0f);
        Steinberg::Vst::Event event{};
        event.busIndex = 0;
        event.sampleOffset = static_cast<Steinberg::int32>(
            std::min<std::size_t>(midi.frameOffset, frames - 1));
        event.ppqPosition = 0.0;
        event.flags = Steinberg::Vst::Event::kIsLive;
        if (status == 0x90 && midi.data[2] != 0) {
            event.type = Steinberg::Vst::Event::kNoteOnEvent;
            event.noteOn.channel = channel;
            event.noteOn.pitch = midi.data[1];
            event.noteOn.tuning = 0.0F;
            event.noteOn.velocity = static_cast<float>(midi.data[2]) / 127.0F;
            event.noteOn.length = 0;
            event.noteOn.noteId = -1;
        } else if (status == 0x80 || status == 0x90) {
            event.type = Steinberg::Vst::Event::kNoteOffEvent;
            event.noteOff.channel = channel;
            event.noteOff.pitch = midi.data[1];
            event.noteOff.velocity = static_cast<float>(midi.data[2]) / 127.0F;
            event.noteOff.noteId = -1;
            event.noteOff.tuning = 0.0F;
        } else {
            continue;
        }
        impl_->inputEvents.addEvent(event);
    }
    impl_->processData.inputEvents = &impl_->inputEvents;
    impl_->processData.outputEvents = &impl_->outputEvents;
    impl_->processData.inputParameterChanges =
        impl_->hasParameterChanges ? &impl_->parameterChanges : nullptr;
    impl_->processData.outputParameterChanges = &impl_->outputParameterChanges;
    if (impl_->processor->process(impl_->processData) != Steinberg::kResultOk) {
        for (std::size_t channel = 0; channel < outputChannels; ++channel)
            if (outputs[channel]) std::fill(outputs[channel], outputs[channel] + frames, 0.0F);
    }
}

void Vst3Processor::setProcessContext(const AudioProcessContext& context) noexcept {
    if (!impl_) return;
    impl_->processContext.projectTimeMusic = context.projectTimeMusic;
    impl_->processContext.tempo = context.tempo;
    impl_->processContext.state = Steinberg::Vst::ProcessContext::kTempoValid |
                                  Steinberg::Vst::ProcessContext::kProjectTimeMusicValid |
                                  Steinberg::Vst::ProcessContext::kTimeSigValid;
    if (context.playing)
        impl_->processContext.state |= Steinberg::Vst::ProcessContext::kPlaying;
}

std::size_t Vst3Processor::takeOutputMidi(MidiEvent* events,
                                         std::size_t capacity) noexcept {
    if (!impl_ || !events || capacity == 0) return 0;
    const auto available = std::min<std::size_t>(
        static_cast<std::size_t>(std::max<Steinberg::int32>(
            0, impl_->outputEvents.getEventCount())), capacity);
    std::size_t count = 0;
    for (std::size_t index = 0; index < available; ++index) {
        Steinberg::Vst::Event event{};
        if (impl_->outputEvents.getEvent(static_cast<Steinberg::int32>(index), event) !=
            Steinberg::kResultOk) continue;
        MidiEvent midi;
        midi.frameOffset = static_cast<std::size_t>(
            std::max<Steinberg::int32>(0, event.sampleOffset));
        midi.size = 3;
        if (event.type == Steinberg::Vst::Event::kNoteOnEvent) {
            midi.data = {
                static_cast<std::uint8_t>(0x90 | (event.noteOn.channel & 0x0f)),
                static_cast<std::uint8_t>(std::clamp<Steinberg::int16>(
                    event.noteOn.pitch, 0, 127)),
                static_cast<std::uint8_t>(std::clamp(
                    static_cast<int>(event.noteOn.velocity * 127.0F), 0, 127))};
        } else if (event.type == Steinberg::Vst::Event::kNoteOffEvent) {
            midi.data = {
                static_cast<std::uint8_t>(0x80 | (event.noteOff.channel & 0x0f)),
                static_cast<std::uint8_t>(std::clamp<Steinberg::int16>(
                    event.noteOff.pitch, 0, 127)),
                static_cast<std::uint8_t>(std::clamp(
                    static_cast<int>(event.noteOff.velocity * 127.0F), 0, 127))};
        } else {
            continue;
        }
        events[count++] = midi;
    }
    return count;
}

} // namespace transmission
