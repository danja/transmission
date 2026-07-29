#include "transmission/UiProjectCodec.h"

#include <cassert>

int main() {
    transmission::UiProject project;
    project.label = "Drums & synths";
    project.tempo = 128.0;
    project.loopBars = 8.0;
    project.loopEnabled = true;
    project.systemOutputConnections = {"playback:left", "playback:right"};
    project.nodes = {
        {"system-input", "System Input",
         transmission::UiProjectNodeKind::SystemInput, 0, 2, 0, 1,
         20.5, 30.5, ""},
        {"drumgen", "drumgen", transmission::UiProjectNodeKind::Plugin,
         2, 2, 1, 1, 240.0, 30.0, "/tmp/a path/drumgen.vst3"},
        {"midi-output", "MIDI Output",
         transmission::UiProjectNodeKind::MidiOutput, 0, 0, 1, 0,
         360.0, 30.0, "", "device-42:midi_in"},
        {"system-output", "System Output",
         transmission::UiProjectNodeKind::SystemOutput, 2, 0, 1, 0,
         480.0, 30.0, ""}};
    project.connections = {
        {"drumgen", "system-output",
         transmission::UiProjectConnectionKind::Audio, 0, 1}};
    const auto encoded = transmission::encodeUiProject(project);
    transmission::UiProject decoded;
    std::string error;
    assert(transmission::decodeUiProject(encoded, decoded, error));
    assert(decoded.label == project.label);
    assert(decoded.nodes[1].pluginPath == project.nodes[1].pluginPath);
    assert(decoded.nodes[2].externalPort == project.nodes[2].externalPort);
    assert(decoded.nodes[0].x == project.nodes[0].x);
    assert(decoded.connections[0].toPort == 1);
    assert(decoded.systemOutputConnections == project.systemOutputConnections);
    assert(decoded.tempo == 128.0);
    assert(decoded.loopEnabled);

    assert(!transmission::decodeUiProject("not a project\n", decoded, error));
    assert(!transmission::decodeUiProject(
        "TRANSMISSION_UI\t1\nNODE\tbroken\nEND\n", decoded, error));
    return 0;
}
