// native/include/transmission/Vst3Inspector.h

#pragma once

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

/** VST3 SDK-backed metadata inspection, intentionally separate from processing. */
class Vst3Inspector {
public:
    std::vector<Vst3ClassDescriptor> inspect(const std::string& modulePath,
                                             std::string& error) const;
};

} // namespace transmission
