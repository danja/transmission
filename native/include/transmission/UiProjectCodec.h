#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "AudioProcessor.h"

namespace transmission {

enum class UiProjectNodeKind {
    SystemInput, SystemOutput, PassThrough, Plugin, MidiInput, MidiOutput, Gain
};
enum class UiProjectConnectionKind { Audio, Midi };

struct UiProjectParameter {
    std::uint32_t id = 0;
    double normalizedValue = 0.0;
};

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
    std::string externalPort;
    std::vector<UiProjectParameter> parameters;
    std::vector<std::uint8_t> componentState;
    std::vector<std::uint8_t> controllerState;
    double gainDb = 0.0;
    double pan = 0.0;
};

struct UiProjectConnection {
    std::string from;
    std::string to;
    UiProjectConnectionKind kind = UiProjectConnectionKind::Audio;
    std::size_t fromPort = 0;
    std::size_t toPort = 0;
};

struct UiProjectMidiNote {
    double startBeat = 0.0;
    double durationBeats = 0.0;
    std::uint8_t pitch = 0;
    std::uint8_t velocity = 1;
    std::uint8_t channel = 0;
};

struct UiProjectMidiClip {
    std::string id;
    std::string targetNodeId;
    double startBeat = 0.0;
    double lengthBeats = 0.0;
    std::vector<UiProjectMidiNote> notes;
};

struct UiProjectGainLane {
    std::string targetNodeId;
    std::vector<GainEnvelopePoint> points;
};

struct UiProjectMidiParameterMapping {
    std::string targetNodeId;
    std::uint32_t parameterId = 0;
    int channel = -1;
    std::uint8_t controller = 0;
    bool consume = true;
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
    double arrangementLengthBeats = 0.0;
    std::vector<UiProjectMidiClip> midiClips;
    std::vector<UiProjectGainLane> gainLanes;
    std::vector<UiProjectMidiParameterMapping> midiMappings;
    std::size_t renderAheadMilliseconds = 200;
    std::size_t requestedBufferSize = 0;
    std::size_t processingThreads = 0;
};

std::string encodeUiProject(const UiProject& project);
bool decodeUiProject(const std::string& text, UiProject& project,
                     std::string& error);

} // namespace transmission
