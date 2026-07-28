// native/src/PluginScanner.cpp

#include "transmission/PluginScanner.h"

#include <filesystem>

namespace transmission {

std::vector<PluginCandidate> PluginScanner::scan(const std::vector<std::string>& roots) const {
    std::vector<PluginCandidate> candidates;
    for (const auto& root : roots) {
        const std::filesystem::path path(root);
        if (!std::filesystem::exists(path)) continue;
        if (std::filesystem::is_regular_file(path) && path.extension() == ".vst3") {
            candidates.push_back({path.string(), path.string(), path.stem().string()});
            continue;
        }
        if (!std::filesystem::is_directory(path)) continue;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(path)) {
            if (entry.is_directory() && entry.path().extension() == ".vst3") {
                candidates.push_back({entry.path().string(), entry.path().string(), entry.path().stem().string()});
            }
        }
    }
    return candidates;
}

} // namespace transmission
