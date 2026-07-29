#pragma once

#include <string>
#include <vector>

namespace transmission {

/** Removes a generated numeric JACK client suffix while retaining the port name. */
std::string stableJackPortIdentity(const std::string& portName);

/** Resolves an exact or uniquely matching stable JACK port identity. */
std::string resolveJackPortName(const std::string& requested,
                                const std::vector<std::string>& available);

} // namespace transmission
