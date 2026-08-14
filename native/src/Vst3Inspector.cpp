// native/src/Vst3Inspector.cpp

#include "transmission/Vst3Inspector.h"

#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"

#include <algorithm>
#include <memory>

namespace transmission {

std::vector<Vst3ClassDescriptor> Vst3Inspector::inspect(const std::string& modulePath,
                                                        std::string& error) const {
    std::vector<Vst3ClassDescriptor> descriptors;
    auto module = VST3::Hosting::Module::create(modulePath, error);
    if (!module) return descriptors;
    for (const auto& info : module->getFactory().classInfos()) {
        descriptors.push_back({modulePath, info.ID().toString(), info.name(), info.vendor(), info.subCategoriesString()});
    }
    return descriptors;
}

namespace {

std::string busName(const Steinberg::Vst::BusInfo& info,
                    Steinberg::int32 bus, Steinberg::int32 channel) {
    char text[128]{};
    Steinberg::UString128(info.name).toAscii(text, 128);
    std::string name = text;
    if (name.empty()) name = "Bus " + std::to_string(bus + 1);
    if (info.channelCount > 1)
        name += " " + std::to_string(channel + 1);
    return name;
}

std::string parameterText(const Steinberg::Vst::String128& value) {
    char text[128]{};
    Steinberg::UString128(value).toAscii(text, 128);
    return text;
}

void appendPorts(Steinberg::Vst::IComponent& component,
                 Steinberg::Vst::BusDirection direction,
                 std::vector<Vst3PortDescriptor>& ports) {
    const auto count = component.getBusCount(Steinberg::Vst::kAudio, direction);
    for (Steinberg::int32 bus = 0; bus < count; ++bus) {
        Steinberg::Vst::BusInfo info{};
        if (component.getBusInfo(Steinberg::Vst::kAudio, direction, bus, info) !=
            Steinberg::kResultOk) continue;
        for (Steinberg::int32 channel = 0; channel < info.channelCount; ++channel)
            ports.push_back({busName(info, bus, channel),
                             static_cast<std::size_t>(bus),
                             static_cast<std::size_t>(channel)});
    }
}

} // namespace

bool Vst3Inspector::inspectTopology(const std::string& modulePath,
                                    Vst3PluginTopology& topology,
                                    std::string& error) const {
    topology = {};
    auto module = VST3::Hosting::Module::create(modulePath, error);
    if (!module) return false;
    VST3::Hosting::ClassInfo selected;
    bool found = false;
    for (const auto& info : module->getFactory().classInfos()) {
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
    Steinberg::Vst::HostApplication hostApplication;
    Steinberg::Vst::PluginContextFactory::instance().setPluginContext(
        &hostApplication);
    auto provider = std::make_unique<Steinberg::Vst::PlugProvider>(
        module->getFactory(), selected, true);
    if (!provider->initialize()) {
        error = "failed to initialize VST3 component";
        return false;
    }
    auto component = provider->getComponentPtr();
    if (!component) {
        error = "VST3 component is unavailable";
        return false;
    }
    topology.name = selected.name();
    appendPorts(*component, Steinberg::Vst::kInput, topology.audioInputs);
    appendPorts(*component, Steinberg::Vst::kOutput, topology.audioOutputs);
    topology.midiInputs = static_cast<std::size_t>(std::max<Steinberg::int32>(
        0, component->getBusCount(Steinberg::Vst::kEvent,
                                  Steinberg::Vst::kInput)));
    topology.midiOutputs = static_cast<std::size_t>(std::max<Steinberg::int32>(
        0, component->getBusCount(Steinberg::Vst::kEvent,
                                  Steinberg::Vst::kOutput)));
    if (auto controller = provider->getControllerPtr()) {
        const auto parameterCount = controller->getParameterCount();
        topology.parameters.reserve(static_cast<std::size_t>(
            std::max<Steinberg::int32>(0, parameterCount)));
        for (Steinberg::int32 index = 0; index < parameterCount; ++index) {
            Steinberg::Vst::ParameterInfo info{};
            if (controller->getParameterInfo(index, info) != Steinberg::kResultTrue)
                continue;
            topology.parameters.push_back({
                static_cast<std::uint32_t>(info.id),
                parameterText(info.title),
                parameterText(info.shortTitle),
                parameterText(info.units),
                info.defaultNormalizedValue,
                info.stepCount,
                info.flags
            });
        }
    }
    return true;
}

} // namespace transmission
