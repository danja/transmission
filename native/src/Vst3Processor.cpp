#include "transmission/Vst3Processor.h"

#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <algorithm>
#include <memory>

namespace transmission {

struct Vst3Processor::Impl {
    VST3::Hosting::Module::Ptr module;
    Steinberg::Vst::HostApplication hostApplication;
    std::unique_ptr<Steinberg::Vst::PlugProvider> provider;
    Steinberg::IPtr<Steinberg::Vst::IComponent> component;
    Steinberg::FUnknownPtr<Steinberg::Vst::IAudioProcessor> processor;
    Steinberg::Vst::HostProcessData processData;
    Steinberg::Vst::ParameterChanges parameterChanges{4};
    Steinberg::Vst::ProcessContext processContext{};
    std::string name;
    std::size_t channels = 0;
    std::size_t frames = 0;
    bool active = false;
    bool processing = false;
    bool hasParameterChanges = false;

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
    impl_.reset();
    if (modulePath.empty() || channels == 0 || frames == 0 || sampleRate <= 0.0) {
        error = "module path, channels, frames, and sample rate must be valid";
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
    if (inputBusCount < 1 || outputBusCount < 1) {
        error = "VST3 effect has no audio input/output bus";
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
    if (candidate->processData.numInputs < 1 || candidate->processData.numOutputs < 1 ||
        candidate->processData.inputs[0].numChannels < static_cast<Steinberg::int32>(channels) ||
        candidate->processData.outputs[0].numChannels < static_cast<Steinberg::int32>(channels)) {
        error = "VST3 effect does not provide the requested channel count";
        return false;
    }
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
    candidate->channels = channels;
    candidate->frames = frames;
    candidate->processContext.sampleRate = sampleRate;
    candidate->processContext.state = Steinberg::Vst::ProcessContext::kPlaying;
    candidate->processContext.tempo = 120.0;
    candidate->processContext.projectTimeMusic = 0.0;
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
    impl_->parameterChanges.clearQueue();
    Steinberg::int32 queueIndex = 0;
    auto* queue = impl_->parameterChanges.addParameterData(
        static_cast<Steinberg::Vst::ParamID>(parameterId), queueIndex);
    if (!queue) {
        error = "failed to create VST3 parameter queue";
        return false;
    }
    Steinberg::int32 pointIndex = 0;
    if (queue->addPoint(0, normalizedValue, pointIndex) != Steinberg::kResultOk) {
        error = "failed to queue VST3 parameter value";
        return false;
    }
    impl_->hasParameterChanges = true;
    return true;
}

void Vst3Processor::process(const float* const* inputs, float* const* outputs,
                            std::size_t channels, std::size_t frames) noexcept {
    if (!ready() || !inputs || !outputs || channels != impl_->channels ||
        frames != impl_->frames) {
        if (outputs) {
            for (std::size_t channel = 0; channel < channels; ++channel)
                if (outputs[channel]) std::fill(outputs[channel], outputs[channel] + frames, 0.0F);
        }
        return;
    }
    impl_->processData.numSamples = static_cast<Steinberg::int32>(frames);
    impl_->processData.processContext = &impl_->processContext;
    for (std::size_t channel = 0; channel < channels; ++channel) {
        if (!inputs[channel] || !outputs[channel]) return;
        impl_->processData.setChannelBuffer(Steinberg::Vst::kInput, 0,
                                             static_cast<Steinberg::int32>(channel),
                                             const_cast<float*>(inputs[channel]));
        impl_->processData.setChannelBuffer(Steinberg::Vst::kOutput, 0,
                                             static_cast<Steinberg::int32>(channel), outputs[channel]);
    }
    impl_->processData.inputParameterChanges =
        impl_->hasParameterChanges ? &impl_->parameterChanges : nullptr;
    if (impl_->processor->process(impl_->processData) != Steinberg::kResultOk) {
        for (std::size_t channel = 0; channel < channels; ++channel)
            if (outputs[channel]) std::fill(outputs[channel], outputs[channel] + frames, 0.0F);
    }
}

} // namespace transmission
