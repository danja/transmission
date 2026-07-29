#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace transmission {

enum class UiProjectNodeKind { SystemInput, SystemOutput, PassThrough, Plugin };
enum class UiProjectConnectionKind { Audio, Midi };

struct UiProjectNode {
    std::string id;
    std::string label;
    UiProjectNodeKind kind = UiProjectNodeKind::Plugin;
    std::size_t audioInputs = 0;
    std::size_t audioOutputs = 0;
    std::size_t midiInputs = 0;
    std::size_t midiOutputs = 0;
    double x = 0.0;
    double y = 0.0;
    std::string pluginPath;
};

struct UiProjectConnection {
    std::string from;
    std::string to;
    UiProjectConnectionKind kind = UiProjectConnectionKind::Audio;
    std::size_t fromPort = 0;
    std::size_t toPort = 0;
};

struct UiProject {
    std::string id = "http://purl.org/stuff/transmissions/main";
    std::string label = "Transmission";
    std::vector<UiProjectNode> nodes;
    std::vector<UiProjectConnection> connections;
    std::array<std::string, 2> systemInputConnections{
        "system:capture_1", "system:capture_2"};
    std::array<std::string, 2> systemOutputConnections{
        "system:playback_1", "system:playback_2"};
    double tempo = 120.0;
    double loopBars = 4.0;
    bool loopEnabled = false;
};

std::string encodeUiProject(const UiProject& project);
bool decodeUiProject(const std::string& text, UiProject& project,
                     std::string& error);

} // namespace transmission
