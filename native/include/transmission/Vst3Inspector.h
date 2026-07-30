// native/include/transmission/Vst3Inspector.h

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace transmission {

struct Vst3ClassDescriptor {
    std::string modulePath;
    std::string classId;
    std::string name;
    std::string vendor;
    std::string category;
};

struct Vst3PortDescriptor {
    std::string name;
    std::size_t bus = 0;
    std::size_t channel = 0;
};

struct Vst3ParameterDescriptor {
    std::uint32_t id = 0;
    std::string title;
    std::string shortTitle;
    std::string units;
    double defaultNormalized = 0.0;
    std::int32_t stepCount = 0;
    std::int32_t flags = 0;
};

struct Vst3PluginTopology {
    std::string name;
    std::vector<Vst3PortDescriptor> audioInputs;
    std::vector<Vst3PortDescriptor> audioOutputs;
    std::size_t midiInputs = 0;
    std::size_t midiOutputs = 0;
    std::vector<Vst3ParameterDescriptor> parameters;
};

/** VST3 SDK-backed metadata inspection, intentionally separate from processing. */
class Vst3Inspector {
public:
    std::vector<Vst3ClassDescriptor> inspect(const std::string& modulePath,
                                             std::string& error) const;
    bool inspectTopology(const std::string& modulePath,
                         Vst3PluginTopology& topology,
                         std::string& error) const;
};

} // namespace transmission
