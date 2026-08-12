#include "transmission/JackPortIdentity.h"

#include <algorithm>
#include <cctype>

namespace transmission {

std::string stableJackPortIdentity(const std::string& portName) {
    const auto separator = portName.find(':');
    if (separator == std::string::npos) return portName;
    const auto suffix = portName.rfind('-', separator);
    if (suffix == std::string::npos || suffix + 1 == separator) return portName;
    const auto numeric = std::all_of(
        portName.begin() + static_cast<std::ptrdiff_t>(suffix + 1),
        portName.begin() + static_cast<std::ptrdiff_t>(separator),
        [](unsigned char value) { return std::isdigit(value) != 0; });
    if (!numeric) return portName;
    return portName.substr(0, suffix) + portName.substr(separator);
}

std::string resolveJackPortName(const std::string& requested,
                                const std::vector<std::string>& available) {
    if (std::find(available.begin(), available.end(), requested) != available.end())
        return requested;
    const auto identity = stableJackPortIdentity(requested);
    std::string match;
    for (const auto& candidate : available) {
        if (stableJackPortIdentity(candidate) != identity) continue;
        if (match.empty()) match = candidate;
    }
    return match.empty() ? requested : match;
}

} // namespace transmission
