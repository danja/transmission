// native/src/Vst3Inspector.cpp

#include "transmission/Vst3Inspector.h"

#include "public.sdk/source/vst/hosting/module.h"

namespace transmission {

std::vector<Vst3ClassDescriptor> Vst3Inspector::inspect(const std::string& modulePath,
                                                        std::string& error) const {
    std::vector<Vst3ClassDescriptor> descriptors;
    auto module = VST3::Hosting::Module::create(modulePath, error);
    if (!module) return descriptors;
    for (const auto& info : module->getFactory().classInfos()) {
        descriptors.push_back({modulePath, info.ID().toString(), info.name(), info.vendor(), info.category()});
    }
    return descriptors;
}

} // namespace transmission
