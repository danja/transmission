#include "transmission/UiProjectCodec.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <sstream>
#include <string_view>

namespace transmission {
namespace {

std::string hexEncode(std::string_view value) {
    static constexpr char digits[] = "0123456789abcdef";
    if (value.empty()) return "-";
    std::string result;
    result.reserve(value.size() * 2);
    for (const auto byte : value) {
        const auto valueByte = static_cast<unsigned char>(byte);
        result.push_back(digits[valueByte >> 4]);
        result.push_back(digits[valueByte & 0x0f]);
    }
    return result;
}

int hexDigit(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

bool hexDecode(std::string_view value, std::string& result) {
    result.clear();
    if (value == "-") return true;
    if (value.size() % 2 != 0) return false;
    result.reserve(value.size() / 2);
    for (std::size_t index = 0; index < value.size(); index += 2) {
        const auto high = hexDigit(value[index]);
        const auto low = hexDigit(value[index + 1]);
        if (high < 0 || low < 0) return false;
        result.push_back(static_cast<char>((high << 4) | low));
    }
    return true;
}

std::vector<std::string_view> fields(std::string_view line) {
    std::vector<std::string_view> result;
    std::size_t start = 0;
    while (start <= line.size()) {
        const auto separator = line.find('\t', start);
        result.push_back(line.substr(start, separator == std::string_view::npos
                                               ? line.size() - start
                                               : separator - start));
        if (separator == std::string_view::npos) break;
        start = separator + 1;
    }
    return result;
}

template <typename T>
bool integer(std::string_view text, T& result) {
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}

// std::stod parses via the process's current C locale (LC_NUMERIC), which
// GTK's gtk_init() sets from the environment — under a locale that uses ','
// as the decimal separator (e.g. it_IT), "189.355" would parse as "189" and
// leave ".355" unconsumed. std::from_chars for floating point is always
// locale-independent ('.' only), matching how encodeUiProject's stream
// output is written (the classic "C" locale, unaffected by setlocale), so
// round-tripping stays symmetric regardless of the process's locale.
bool number(std::string_view text, double& result) {
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() &&
           std::isfinite(result);
}

} // namespace

std::string encodeUiProject(const UiProject& project) {
    std::ostringstream output;
    output << "TRANSMISSION_UI\t8\n";
    output << "PROJECT\t" << hexEncode(project.id) << '\t'
           << hexEncode(project.label) << '\n';
    output << "TRANSPORT\t" << project.tempo << '\t' << project.loopBars
           << '\t' << (project.loopEnabled ? 1 : 0) << '\n';
    for (std::size_t index = 0; index < 2; ++index) {
        output << "INPUT\t" << index << '\t'
               << hexEncode(project.systemInputConnections[index]) << '\n';
        output << "OUTPUT\t" << index << '\t'
               << hexEncode(project.systemOutputConnections[index]) << '\n';
    }
    for (const auto& node : project.nodes) {
        output << "NODE\t" << hexEncode(node.id) << '\t' << hexEncode(node.label)
               << '\t' << static_cast<int>(node.kind) << '\t' << node.audioInputs
               << '\t' << node.audioOutputs << '\t' << node.midiInputs << '\t'
               << node.midiOutputs << '\t' << node.x << '\t' << node.y << '\t'
               << hexEncode(node.kind == UiProjectNodeKind::MidiInput ||
                                    node.kind == UiProjectNodeKind::MidiOutput
                                ? node.externalPort : node.pluginPath)
               << '\n';
        for (const auto& parameter : node.parameters)
            output << "PARAM\t" << hexEncode(node.id) << '\t'
                   << parameter.id << '\t' << parameter.normalizedValue
                   << '\n';
        if (!node.componentState.empty() || !node.controllerState.empty()) {
            const auto component = std::string_view(
                reinterpret_cast<const char*>(node.componentState.data()),
                node.componentState.size());
            const auto controller = std::string_view(
                reinterpret_cast<const char*>(node.controllerState.data()),
                node.controllerState.size());
            output << "STATE\t" << hexEncode(node.id) << '\t'
                   << hexEncode(component) << '\t'
                   << hexEncode(controller) << '\n';
        }
        if (node.kind == UiProjectNodeKind::Gain)
            output << "NODE_GAIN\t" << hexEncode(node.id) << '\t'
                   << node.gainDb << '\t' << node.pan << '\n';
    }
    for (const auto& connection : project.connections) {
        output << "EDGE\t" << hexEncode(connection.from) << '\t'
               << hexEncode(connection.to) << '\t'
               << static_cast<int>(connection.kind) << '\t' << connection.fromPort
               << '\t' << connection.toPort << '\n';
    }
    output << "ARRANGEMENT\t" << project.arrangementLengthBeats << '\n';
    for (const auto& clip : project.midiClips) {
        output << "CLIP\t" << hexEncode(clip.id) << '\t'
               << hexEncode(clip.targetNodeId) << '\t' << clip.startBeat
               << '\t' << clip.lengthBeats << '\n';
        for (const auto& note : clip.notes)
            output << "NOTE\t" << hexEncode(clip.id) << '\t'
                   << note.startBeat << '\t' << note.durationBeats << '\t'
                   << static_cast<unsigned>(note.pitch) << '\t'
                   << static_cast<unsigned>(note.velocity) << '\t'
                   << static_cast<unsigned>(note.channel) << '\n';
    }
    for (const auto& lane : project.gainLanes) {
        output << "GAIN_LANE\t" << hexEncode(lane.targetNodeId) << '\n';
        for (const auto& point : lane.points)
            output << "GAIN_POINT\t" << hexEncode(lane.targetNodeId) << '\t'
                   << point.beat << '\t' << point.valueDb << '\t'
                   << (point.linear ? 1 : 0) << '\n';
    }
    for (const auto& mapping : project.midiMappings)
        output << "MIDI_MAP\t" << hexEncode(mapping.targetNodeId) << '\t'
               << mapping.parameterId << '\t' << mapping.channel << '\t'
               << static_cast<unsigned>(mapping.controller) << '\t'
               << (mapping.consume ? 1 : 0) << '\n';
    output << "SETTINGS\t" << project.renderAheadMilliseconds << '\t'
           << project.requestedBufferSize << '\t'
           << project.processingThreads << '\n';
    output << "END\n";
    return output.str();
}

bool decodeUiProject(const std::string& text, UiProject& project,
                     std::string& error) {
    UiProject candidate;
    bool header = false;
    bool ended = false;
    std::istringstream input(text);
    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        const auto values = fields(line);
        const auto fail = [&](std::string_view reason) {
            error = "invalid native UI project interchange at line " +
                    std::to_string(lineNumber) +
                    (values.empty() ? "" : " (" + std::string(values[0]) + ")") +
                    ": " + std::string(reason);
            return false;
        };
        const auto expectFields = [&](std::size_t expected) {
            if (values.size() == expected) return true;
            fail("expected " + std::to_string(expected) + " tab-separated fields, got " +
                 std::to_string(values.size()));
            return false;
        };
        if (!header) {
            if (values.size() != 2 || values[0] != "TRANSMISSION_UI")
                return fail("expected a \"TRANSMISSION_UI\\t<version>\" header line");
            if (values[1] != "1" && values[1] != "2" &&
                values[1] != "3" && values[1] != "4" &&
                values[1] != "5" && values[1] != "6" &&
                values[1] != "7" && values[1] != "8")
                return fail("unsupported interchange version \"" + std::string(values[1]) + "\"");
            header = true;
            continue;
        }
        if (values.empty()) return fail("empty record");
        if (values[0] == "END") {
            if (values.size() != 1) return fail("END takes no fields");
            ended = true;
            break;
        }
        if (values[0] == "PROJECT") {
            if (!expectFields(3)) return false;
            if (!hexDecode(values[1], candidate.id)) return fail("id is not valid hex");
            if (!hexDecode(values[2], candidate.label)) return fail("label is not valid hex");
        } else if (values[0] == "TRANSPORT") {
            int enabled = 0;
            if (!expectFields(4)) return false;
            if (!number(values[1], candidate.tempo)) return fail("tempo is not a number");
            if (!number(values[2], candidate.loopBars)) return fail("loopBars is not a number");
            if (!integer(values[3], enabled) || (enabled != 0 && enabled != 1))
                return fail("loopEnabled must be 0 or 1");
            if (candidate.tempo <= 0.0) return fail("tempo must be positive");
            if (candidate.loopBars <= 0.0) return fail("loopBars must be positive");
            candidate.loopEnabled = enabled == 1;
        } else if (values[0] == "INPUT" || values[0] == "OUTPUT") {
            std::size_t index = 0;
            std::string connection;
            if (!expectFields(3)) return false;
            if (!integer(values[1], index) || index >= 2)
                return fail("index must be 0 or 1");
            if (!hexDecode(values[2], connection)) return fail("port name is not valid hex");
            auto& connections = values[0] == "INPUT"
                ? candidate.systemInputConnections
                : candidate.systemOutputConnections;
            connections[index] = std::move(connection);
        } else if (values[0] == "NODE") {
            UiProjectNode node;
            int kind = 0;
            std::string resource;
            if (!expectFields(11)) return false;
            if (!hexDecode(values[1], node.id)) return fail("id is not valid hex");
            if (node.id.empty()) return fail("id must not be empty");
            if (!hexDecode(values[2], node.label)) return fail("label is not valid hex");
            if (!integer(values[3], kind))
                return fail("kind \"" + std::string(values[3]) + "\" is not an integer");
            if (kind < 0 || kind > static_cast<int>(UiProjectNodeKind::JigdawPlugin))
                return fail("kind " + std::to_string(kind) + " is out of range");
            if (!integer(values[4], node.audioInputs))
                return fail("audioInputs \"" + std::string(values[4]) + "\" is not a valid count");
            if (!integer(values[5], node.audioOutputs))
                return fail("audioOutputs \"" + std::string(values[5]) + "\" is not a valid count");
            if (!integer(values[6], node.midiInputs))
                return fail("midiInputs \"" + std::string(values[6]) + "\" is not a valid count");
            if (!integer(values[7], node.midiOutputs))
                return fail("midiOutputs \"" + std::string(values[7]) + "\" is not a valid count");
            if (!number(values[8], node.x)) return fail("x is not a number");
            if (!number(values[9], node.y)) return fail("y is not a number");
            if (!hexDecode(values[10], resource)) return fail("resource is not valid hex");
            node.kind = static_cast<UiProjectNodeKind>(kind);
            if (node.kind == UiProjectNodeKind::MidiInput ||
                node.kind == UiProjectNodeKind::MidiOutput)
                node.externalPort = std::move(resource);
            else
                node.pluginPath = std::move(resource);
            candidate.nodes.push_back(std::move(node));
        } else if (values[0] == "PARAM") {
            std::string nodeId;
            std::uint32_t parameterId = 0;
            double normalizedValue = 0.0;
            if (!expectFields(4)) return false;
            if (!hexDecode(values[1], nodeId)) return fail("node id is not valid hex");
            if (!integer(values[2], parameterId)) return fail("parameterId is not an integer");
            if (!number(values[3], normalizedValue)) return fail("normalizedValue is not a number");
            if (normalizedValue < 0.0 || normalizedValue > 1.0)
                return fail("normalizedValue must be between 0 and 1");
            const auto node = std::find_if(
                candidate.nodes.begin(), candidate.nodes.end(),
                [&nodeId](const auto& current) {
                    return current.id == nodeId;
                });
            if (node == candidate.nodes.end())
                return fail("references a node id that has not been declared yet");
            node->parameters.push_back({parameterId, normalizedValue});
        } else if (values[0] == "STATE") {
            std::string nodeId;
            std::string component;
            std::string controller;
            if (!expectFields(4)) return false;
            if (!hexDecode(values[1], nodeId)) return fail("node id is not valid hex");
            if (!hexDecode(values[2], component)) return fail("component state is not valid hex");
            if (!hexDecode(values[3], controller)) return fail("controller state is not valid hex");
            const auto node = std::find_if(
                candidate.nodes.begin(), candidate.nodes.end(),
                [&nodeId](const auto& current) {
                    return current.id == nodeId;
                });
            if (node == candidate.nodes.end())
                return fail("references a node id that has not been declared yet");
            node->componentState.assign(component.begin(), component.end());
            node->controllerState.assign(controller.begin(), controller.end());
        } else if (values[0] == "NODE_GAIN") {
            std::string nodeId;
            double gainDb = 0.0;
            double pan = 0.0;
            if (values.size() != 3 && values.size() != 4)
                return fail("expected 3 or 4 tab-separated fields, got " +
                            std::to_string(values.size()));
            if (!hexDecode(values[1], nodeId)) return fail("node id is not valid hex");
            if (!number(values[2], gainDb)) return fail("gainDb is not a number");
            if (values.size() == 4 && !number(values[3], pan)) return fail("pan is not a number");
            if (gainDb < GainProcessor::minimumGainDb || gainDb > GainProcessor::maximumGainDb)
                return fail("gainDb is out of range");
            if (pan < -1.0 || pan > 1.0) return fail("pan must be between -1 and 1");
            const auto node = std::find_if(candidate.nodes.begin(), candidate.nodes.end(),
                [&](const auto& current) { return current.id == nodeId; });
            if (node == candidate.nodes.end())
                return fail("references a node id that has not been declared yet");
            if (node->kind != UiProjectNodeKind::Gain)
                return fail("references a node that is not a Gain node");
            node->gainDb = gainDb;
            node->pan = pan;
        } else if (values[0] == "EDGE") {
            UiProjectConnection connection;
            int kind = 0;
            if (!expectFields(6)) return false;
            if (!hexDecode(values[1], connection.from)) return fail("from is not valid hex");
            if (!hexDecode(values[2], connection.to)) return fail("to is not valid hex");
            if (connection.from.empty()) return fail("from must not be empty");
            if (connection.to.empty()) return fail("to must not be empty");
            if (!integer(values[3], kind)) return fail("kind is not an integer");
            if (kind < 0 || kind > static_cast<int>(UiProjectConnectionKind::Midi))
                return fail("kind " + std::to_string(kind) + " is out of range");
            if (!integer(values[4], connection.fromPort)) return fail("fromPort is not a valid index");
            if (!integer(values[5], connection.toPort)) return fail("toPort is not a valid index");
            connection.kind = static_cast<UiProjectConnectionKind>(kind);
            candidate.connections.push_back(std::move(connection));
        } else if (values[0] == "ARRANGEMENT") {
            if (!expectFields(2)) return false;
            if (!number(values[1], candidate.arrangementLengthBeats))
                return fail("arrangementLengthBeats is not a number");
            if (candidate.arrangementLengthBeats < 0.0)
                return fail("arrangementLengthBeats must not be negative");
        } else if (values[0] == "CLIP") {
            UiProjectMidiClip clip;
            if (!expectFields(5)) return false;
            if (!hexDecode(values[1], clip.id)) return fail("id is not valid hex");
            if (clip.id.empty()) return fail("id must not be empty");
            if (!hexDecode(values[2], clip.targetNodeId)) return fail("targetNodeId is not valid hex");
            if (clip.targetNodeId.empty()) return fail("targetNodeId must not be empty");
            if (!number(values[3], clip.startBeat)) return fail("startBeat is not a number");
            if (clip.startBeat < 0.0) return fail("startBeat must not be negative");
            if (!number(values[4], clip.lengthBeats)) return fail("lengthBeats is not a number");
            if (clip.lengthBeats <= 0.0) return fail("lengthBeats must be positive");
            candidate.midiClips.push_back(std::move(clip));
        } else if (values[0] == "NOTE") {
            std::string clipId;
            UiProjectMidiNote note;
            unsigned pitch = 0, velocity = 0, channel = 0;
            if (!expectFields(7)) return false;
            if (!hexDecode(values[1], clipId)) return fail("clip id is not valid hex");
            if (!number(values[2], note.startBeat)) return fail("startBeat is not a number");
            if (!number(values[3], note.durationBeats)) return fail("durationBeats is not a number");
            if (!integer(values[4], pitch)) return fail("pitch is not an integer");
            if (!integer(values[5], velocity)) return fail("velocity is not an integer");
            if (!integer(values[6], channel)) return fail("channel is not an integer");
            if (note.startBeat < 0.0) return fail("startBeat must not be negative");
            if (note.durationBeats <= 0.0) return fail("durationBeats must be positive");
            if (pitch > 127) return fail("pitch must be 0-127");
            if (velocity == 0 || velocity > 127) return fail("velocity must be 1-127");
            if (channel > 15) return fail("channel must be 0-15");
            const auto clip = std::find_if(candidate.midiClips.begin(), candidate.midiClips.end(),
                [&](const auto& current) { return current.id == clipId; });
            if (clip == candidate.midiClips.end())
                return fail("references a clip id that has not been declared yet");
            if (note.startBeat + note.durationBeats > clip->lengthBeats)
                return fail("note extends past the end of its clip");
            note.pitch = static_cast<std::uint8_t>(pitch);
            note.velocity = static_cast<std::uint8_t>(velocity);
            note.channel = static_cast<std::uint8_t>(channel);
            clip->notes.push_back(note);
        } else if (values[0] == "GAIN_LANE") {
            UiProjectGainLane lane;
            if (!expectFields(2)) return false;
            if (!hexDecode(values[1], lane.targetNodeId)) return fail("targetNodeId is not valid hex");
            if (lane.targetNodeId.empty()) return fail("targetNodeId must not be empty");
            candidate.gainLanes.push_back(std::move(lane));
        } else if (values[0] == "GAIN_POINT") {
            std::string nodeId;
            GainEnvelopePoint point;
            int linear = 0;
            if (!expectFields(5)) return false;
            if (!hexDecode(values[1], nodeId)) return fail("node id is not valid hex");
            if (!number(values[2], point.beat)) return fail("beat is not a number");
            if (!number(values[3], point.valueDb)) return fail("valueDb is not a number");
            if (!integer(values[4], linear) || (linear != 0 && linear != 1))
                return fail("linear must be 0 or 1");
            if (point.beat < 0.0) return fail("beat must not be negative");
            const auto lane = std::find_if(candidate.gainLanes.begin(), candidate.gainLanes.end(),
                [&](const auto& current) { return current.targetNodeId == nodeId; });
            if (lane == candidate.gainLanes.end())
                return fail("references a gain lane that has not been declared yet");
            if (!lane->points.empty() && point.beat <= lane->points.back().beat)
                return fail("points must be strictly increasing in beat");
            point.linear = linear == 1;
            lane->points.push_back(point);
        } else if (values[0] == "MIDI_MAP") {
            UiProjectMidiParameterMapping mapping;
            unsigned controller = 0;
            int consume = 0;
            if (!expectFields(6)) return false;
            if (!hexDecode(values[1], mapping.targetNodeId)) return fail("targetNodeId is not valid hex");
            if (mapping.targetNodeId.empty()) return fail("targetNodeId must not be empty");
            if (!integer(values[2], mapping.parameterId)) return fail("parameterId is not an integer");
            if (!integer(values[3], mapping.channel)) return fail("channel is not an integer");
            if (mapping.channel < -1 || mapping.channel > 15) return fail("channel must be -1 to 15");
            if (!integer(values[4], controller)) return fail("controller is not an integer");
            if (controller > 127) return fail("controller must be 0-127");
            if (!integer(values[5], consume) || (consume != 0 && consume != 1))
                return fail("consume must be 0 or 1");
            const auto target = std::find_if(
                candidate.nodes.begin(), candidate.nodes.end(),
                [&](const auto& node) {
                    return node.id == mapping.targetNodeId;
                });
            if (target == candidate.nodes.end())
                return fail("references a node id that has not been declared yet");
            mapping.controller = static_cast<std::uint8_t>(controller);
            mapping.consume = consume == 1;
            candidate.midiMappings.push_back(mapping);
        } else if (values[0] == "SETTINGS") {
            if (!expectFields(4)) return false;
            if (!integer(values[1], candidate.renderAheadMilliseconds))
                return fail("renderAheadMilliseconds is not a valid count");
            if (!integer(values[2], candidate.requestedBufferSize))
                return fail("requestedBufferSize is not a valid count");
            if (!integer(values[3], candidate.processingThreads))
                return fail("processingThreads is not a valid count");
        } else {
            return fail("unrecognised record type \"" + std::string(values[0]) + "\"");
        }
    }
    if (!header || !ended || candidate.nodes.empty()) {
        error = "native UI project interchange is incomplete";
        return false;
    }
    for (std::size_t left = 0; left < candidate.midiClips.size(); ++left) {
        const auto& clip = candidate.midiClips[left];
        const auto target = std::find_if(candidate.nodes.begin(), candidate.nodes.end(),
            [&](const auto& node) { return node.id == clip.targetNodeId; });
        const auto duplicate = std::find_if(candidate.midiClips.begin(),
            candidate.midiClips.begin() + static_cast<std::ptrdiff_t>(left),
            [&](const auto& previous) { return previous.id == clip.id; });
        if (target == candidate.nodes.end() || duplicate != candidate.midiClips.begin() + static_cast<std::ptrdiff_t>(left) ||
            (candidate.arrangementLengthBeats > 0.0 &&
             clip.startBeat + clip.lengthBeats > candidate.arrangementLengthBeats)) {
            error = "native UI project arrangement is invalid";
            return false;
        }
    }
    for (const auto& lane : candidate.gainLanes) {
        const auto target = std::find_if(candidate.nodes.begin(), candidate.nodes.end(),
            [&](const auto& node) {
                return node.id == lane.targetNodeId && node.kind == UiProjectNodeKind::Gain;
            });
        if (target == candidate.nodes.end()) {
            error = "native UI project gain lane target is invalid";
            return false;
        }
    }
    project = std::move(candidate);
    return true;
}

} // namespace transmission
