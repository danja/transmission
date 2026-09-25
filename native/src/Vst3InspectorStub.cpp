// Stub compiled into transmission_engine when TRANSMISSION_WITH_VST3 is off.
// The editor still references Vst3Inspector for port inspection; without VST3
// hosting there is nothing to inspect, so every call reports itself
// unavailable instead of failing to link. Mirrors the Vst3EditorHost stub.
#include "transmission/Vst3Inspector.h"

namespace transmission {

std::vector<Vst3ClassDescriptor> Vst3Inspector::inspect(
    const std::string&, std::string& error) const {
    error = "This build does not include VST3 hosting support";
    return {};
}

bool Vst3Inspector::inspectTopology(const std::string&,
                                    Vst3PluginTopology&,
                                    std::string& error) const {
    error = "This build does not include VST3 hosting support";
    return false;
}

} // namespace transmission
