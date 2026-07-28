// native/include/transmission/PluginScanner.h

#pragma once

#include <string>
#include <vector>

namespace transmission {

struct PluginCandidate {
    std::string path;
    std::string id;
    std::string name;
};

/** Discovers VST3 bundle candidates without loading plugin code. */
class PluginScanner {
public:
    std::vector<PluginCandidate> scan(const std::vector<std::string>& roots) const;
};

} // namespace transmission
