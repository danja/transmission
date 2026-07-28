// native/src/Vst3OfflineProbe.cpp

#include "transmission/Vst3OfflineProbe.h"

#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace transmission {

bool Vst3OfflineProbe::process(const std::string& modulePath, Vst3ProbeResult& result,
                               std::string& error, std::size_t frames,
                               double sampleRate) const {
    if (frames == 0 || sampleRate <= 0.0) {
        error = "frames and sampleRate must be positive";
        return false;
    }

    auto module = VST3::Hosting::Module::create(modulePath, error);
    if (!module) return false;
    auto factory = module->getFactory();
    Steinberg::Vst::HostApplication hostApplication;
    Steinberg::Vst::PluginContextFactory::instance().setPluginContext(&hostApplication);

    VST3::Hosting::ClassInfo selected;
    bool found = false;
    for (const auto& info : factory.classInfos()) {
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

    auto provider = Steinberg::owned(new Steinberg::Vst::PlugProvider(factory, selected, true));
    if (!provider || !provider->initialize()) {
        error = "failed to initialize VST3 component";
        return false;
    }
    auto component = provider->getComponentPtr();
    Steinberg::FUnknownPtr<Steinberg::Vst::IAudioProcessor> audioProcessor(component);
    if (!component || !audioProcessor) {
        error = "VST3 component does not expose IAudioProcessor";
        return false;
    }

    result.pluginName = selected.name();
    result.classId = selected.ID().toString();
    result.frames = frames;
    result.inputChannels = 0;
    result.outputChannels = 0;
    Steinberg::Vst::ProcessSetup setup{};
    // Use the broadly supported real-time processing mode while driving the
    // processor from deterministic offline buffers.
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.sampleRate = sampleRate;
    setup.maxSamplesPerBlock = static_cast<int32_t>(frames);
    component->setActive(false);
    if (audioProcessor->setupProcessing(setup) != Steinberg::kResultOk) {
        error = "VST3 setupProcessing failed";
        return false;
    }
    for (int32_t index = 0; index < component->getBusCount(Steinberg::Vst::kAudio, Steinberg::Vst::kInput); ++index) {
        if (component->activateBus(Steinberg::Vst::kAudio, Steinberg::Vst::kInput, index, true) != Steinberg::kResultOk) {
            error = "VST3 input bus activation failed";
            return false;
        }
    }
    for (int32_t index = 0; index < component->getBusCount(Steinberg::Vst::kAudio, Steinberg::Vst::kOutput); ++index) {
        if (component->activateBus(Steinberg::Vst::kAudio, Steinberg::Vst::kOutput, index, true) != Steinberg::kResultOk) {
            error = "VST3 output bus activation failed";
            return false;
        }
    }
    Steinberg::Vst::HostProcessData data;
    if (!data.prepare(*component, static_cast<int32_t>(frames), Steinberg::Vst::kSample32)) {
        error = "failed to prepare VST3 process buffers";
        return false;
    }
    const auto activeResult = component->setActive(true);
    if (activeResult != Steinberg::kResultOk) {
        error = "VST3 component activation failed: " + std::to_string(activeResult);
        data.unprepare();
        return false;
    }
    const auto processingResult = audioProcessor->setProcessing(true);
    // The SDK's AudioEffect base implementation returns kNotImplemented
    // (and, in non-COM builds of some SDK revisions, kInvalidArgument) even
    // though process() is fully usable. The official audiohost example also
    // deliberately does not fail the host when setProcessing is unsupported.
    const bool processingStateAccepted =
        processingResult == Steinberg::kResultOk ||
        processingResult == Steinberg::kNotImplemented ||
        processingResult == Steinberg::kInvalidArgument;
    if (!processingStateAccepted) {
        error = "VST3 processor activation failed: " + std::to_string(processingResult);
        data.unprepare();
        component->setActive(false);
        return false;
    }
    result.inputChannels = data.numInputs > 0 ? data.inputs[0].numChannels : 0;
    result.outputChannels = data.numOutputs > 0 ? data.outputs[0].numChannels : 0;
    std::vector<float> source(frames);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        constexpr double pi = 3.14159265358979323846;
        source[frame] = static_cast<float>(0.25 * std::sin(2.0 * pi * 220.0 * static_cast<double>(frame) / sampleRate));
    }
    double inputEnergy = 0.0;
    if (data.numInputs > 0) {
        for (int32_t channel = 0; channel < data.inputs[0].numChannels; ++channel) {
            auto* buffer = data.inputs[0].channelBuffers32[channel];
            for (std::size_t frame = 0; frame < frames; ++frame) {
                buffer[frame] = source[frame];
                inputEnergy += source[frame] * source[frame];
            }
        }
    }
    data.numSamples = static_cast<int32_t>(frames);
    // Provide a deterministic gain change for the SDK's AGain fixture. The
    // first probe is intentionally small and does not yet expose a generic
    // parameter-mapping API; hosts can omit this queue for effects without a
    // parameter with ID 1.
    Steinberg::Vst::ParameterChanges parameterChanges(1);
    int32_t parameterIndex = 0;
    if (auto* gain = parameterChanges.addParameterData(1, parameterIndex)) {
        int32_t pointIndex = 0;
        gain->addPoint(0, 1.0, pointIndex);
        data.inputParameterChanges = &parameterChanges;
    }
    Steinberg::Vst::ProcessContext context{};
    context.sampleRate = sampleRate;
    context.state = Steinberg::Vst::ProcessContext::kPlaying;
    context.tempo = 120.0;
    context.projectTimeSamples = 0;
    context.continousTimeSamples = 0;
    context.projectTimeMusic = 0.0;
    data.processContext = &context;
    if (audioProcessor->process(data) != Steinberg::kResultOk) {
        error = "VST3 process failed";
        audioProcessor->setProcessing(false);
        component->setActive(false);
        return false;
    }
    double outputEnergy = 0.0;
    if (data.numOutputs > 0) {
        for (int32_t channel = 0; channel < data.outputs[0].numChannels; ++channel) {
            auto* buffer = data.outputs[0].channelBuffers32[channel];
            for (std::size_t frame = 0; frame < frames; ++frame) outputEnergy += buffer[frame] * buffer[frame];
        }
    }
    result.inputRms = std::sqrt(inputEnergy / static_cast<double>(std::max<std::size_t>(1, frames * result.inputChannels)));
    result.outputRms = std::sqrt(outputEnergy / static_cast<double>(std::max<std::size_t>(1, frames * result.outputChannels)));
    data.unprepare();
    audioProcessor->setProcessing(false);
    component->setActive(false);
    return true;
}

} // namespace transmission
