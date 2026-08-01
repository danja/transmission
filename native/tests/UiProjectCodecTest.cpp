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
    project.nodes[1].parameters = {{7, 0.25}, {42, 0.75}};
    project.nodes[1].componentState = {0x00, 0x7f, 0xff};
    project.nodes[1].controllerState = {0x10, 0x20};
    project.nodes.push_back({
        "master-gain", "Master Gain", transmission::UiProjectNodeKind::Gain,
        2, 2, 0, 0, 600.0, 30.0, "", "", {}, {}, {}, -3.0, -0.25});
    project.arrangementLengthBeats = 16.0;
    project.midiClips = {{"clip-1", "drumgen", 4.0, 4.0,
                          {{0.5, 0.25, 36, 100, 9}}}};
    project.gainLanes = {{"master-gain",
                          {{12.0, 0.0, true}, {16.0, -120.0, true}}}};
    project.midiMappings = {{"master-gain", 0, -1, 19, true},
                            {"drumgen", 42, 0, 23, false}};
    const auto encoded = transmission::encodeUiProject(project);
    assert(encoded.starts_with("TRANSMISSION_UI\t5\n"));
    transmission::UiProject decoded;
    std::string error;
    assert(transmission::decodeUiProject(encoded, decoded, error));
    assert(decoded.label == project.label);
    assert(decoded.nodes[1].pluginPath == project.nodes[1].pluginPath);
    assert(decoded.nodes[1].parameters.size() == 2);
    assert(decoded.nodes[1].parameters[1].id == 42);
    assert(decoded.nodes[1].parameters[1].normalizedValue == 0.75);
    assert(decoded.nodes[1].componentState ==
           project.nodes[1].componentState);
    assert(decoded.nodes[1].controllerState ==
           project.nodes[1].controllerState);
    assert(decoded.nodes[2].externalPort == project.nodes[2].externalPort);
    assert(decoded.nodes[0].x == project.nodes[0].x);
    assert(decoded.connections[0].toPort == 1);
    assert(decoded.systemOutputConnections == project.systemOutputConnections);
    assert(decoded.tempo == 128.0);
    assert(decoded.loopEnabled);
    assert(decoded.arrangementLengthBeats == 16.0);
    assert(decoded.midiClips[0].notes[0].channel == 9);
    assert(decoded.gainLanes[0].points[1].valueDb == -120.0);
    assert(decoded.nodes.back().gainDb == -3.0);
    assert(decoded.nodes.back().pan == -0.25);
    assert(decoded.midiMappings.size() == 2);
    assert(decoded.midiMappings[0].controller == 19);
    assert(decoded.midiMappings[1].parameterId == 42);
    assert(!decoded.midiMappings[1].consume);

    assert(!transmission::decodeUiProject("not a project\n", decoded, error));
    assert(!transmission::decodeUiProject(
        "TRANSMISSION_UI\t1\nNODE\tbroken\nEND\n", decoded, error));
    assert(!transmission::decodeUiProject(
        "TRANSMISSION_UI\t2\n"
        "NODE\t6e\t6e\t3\t0\t2\t0\t0\t0\t0\t2f702e76737433\n"
        "PARAM\t6e\t7\t1.5\n"
        "END\n", decoded, error));
    assert(!transmission::decodeUiProject(
        "TRANSMISSION_UI\t5\n"
        "NODE\t6761696e\t6761696e\t6\t2\t2\t1\t0\t0\t0\t-\n"
        "MIDI_MAP\t6761696e\t0\t16\t19\t1\n"
        "END\n", decoded, error));
    assert(!transmission::decodeUiProject(
        "TRANSMISSION_UI\t4\n"
        "NODE\t6761696e\t6761696e\t6\t2\t2\t0\t0\t0\t0\t-\n"
        "NODE_GAIN\t6761696e\t0\t2\n"
        "END\n", decoded, error));
    assert(transmission::decodeUiProject(
        "TRANSMISSION_UI\t1\n"
        "NODE\t6e\t6e\t3\t0\t2\t0\t0\t0\t0\t2f702e76737433\n"
        "END\n", decoded, error));
    return 0;
}
