#include "transmission/JackPortIdentity.h"

#include <cassert>
#include <vector>

int main() {
    using transmission::resolveJackPortName;
    using transmission::stableJackPortIdentity;

    assert(stableJackPortIdentity("UMC404HD 192k-44:playback_AUX0") ==
           "UMC404HD 192k:playback_AUX0");
    assert(stableJackPortIdentity("system:playback_1") == "system:playback_1");
    assert(stableJackPortIdentity("synth-v2:output") == "synth-v2:output");

    const std::vector<std::string> ports{
        "UMC404HD 192k-91:playback_AUX0",
        "UMC404HD 192k-91:playback_AUX1",
        "system:playback_1"};
    assert(resolveJackPortName("system:playback_1", ports) == "system:playback_1");
    assert(resolveJackPortName("UMC404HD 192k-44:playback_AUX0", ports) ==
           "UMC404HD 192k-91:playback_AUX0");
    assert(resolveJackPortName("missing:port", ports) == "missing:port");

    const std::vector<std::string> ambiguous{
        "device-10:output", "device-11:output"};
    assert(resolveJackPortName("device-3:output", ambiguous) == "device-10:output");
    return 0;
}
