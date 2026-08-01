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

bool number(std::string_view text, double& result) {
    try {
        std::size_t parsed = 0;
        result = std::stod(std::string(text), &parsed);
        return parsed == text.size() && std::isfinite(result);
    } catch (...) {
        return false;
    }
}

} // namespace

std::string encodeUiProject(const UiProject& project) {
    std::ostringstream output;
    output << "TRANSMISSION_UI\t4\n";
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
        const auto fail = [&] {
            error = "invalid native UI project interchange at line " +
                    std::to_string(lineNumber);
            return false;
        };
        if (!header) {
            if (values.size() != 2 || values[0] != "TRANSMISSION_UI" ||
                (values[1] != "1" && values[1] != "2" &&
                 values[1] != "3" && values[1] != "4")) return fail();
            header = true;
            continue;
        }
        if (values.empty()) return fail();
        if (values[0] == "END") {
            if (values.size() != 1) return fail();
            ended = true;
            break;
        }
        if (values[0] == "PROJECT") {
            if (values.size() != 3 || !hexDecode(values[1], candidate.id) ||
                !hexDecode(values[2], candidate.label)) return fail();
        } else if (values[0] == "TRANSPORT") {
            int enabled = 0;
            if (values.size() != 4 || !number(values[1], candidate.tempo) ||
                !number(values[2], candidate.loopBars) ||
                !integer(values[3], enabled) || (enabled != 0 && enabled != 1) ||
                candidate.tempo <= 0.0 || candidate.loopBars <= 0.0) return fail();
            candidate.loopEnabled = enabled == 1;
        } else if (values[0] == "INPUT" || values[0] == "OUTPUT") {
            std::size_t index = 0;
            std::string connection;
            if (values.size() != 3 || !integer(values[1], index) || index >= 2 ||
                !hexDecode(values[2], connection)) return fail();
            auto& connections = values[0] == "INPUT"
                ? candidate.systemInputConnections
                : candidate.systemOutputConnections;
            connections[index] = std::move(connection);
        } else if (values[0] == "NODE") {
            UiProjectNode node;
            int kind = 0;
            std::string resource;
            if (values.size() != 11 || !hexDecode(values[1], node.id) ||
                !hexDecode(values[2], node.label) || !integer(values[3], kind) ||
                kind < 0 || kind > static_cast<int>(UiProjectNodeKind::Gain) ||
                !integer(values[4], node.audioInputs) ||
                !integer(values[5], node.audioOutputs) ||
                !integer(values[6], node.midiInputs) ||
                !integer(values[7], node.midiOutputs) ||
                !number(values[8], node.x) || !number(values[9], node.y) ||
                !hexDecode(values[10], resource) || node.id.empty())
                return fail();
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
            if (values.size() != 4 || !hexDecode(values[1], nodeId) ||
                !integer(values[2], parameterId) ||
                !number(values[3], normalizedValue) ||
                normalizedValue < 0.0 || normalizedValue > 1.0)
                return fail();
            const auto node = std::find_if(
                candidate.nodes.begin(), candidate.nodes.end(),
                [&nodeId](const auto& current) {
                    return current.id == nodeId;
                });
            if (node == candidate.nodes.end()) return fail();
            node->parameters.push_back({parameterId, normalizedValue});
        } else if (values[0] == "STATE") {
            std::string nodeId;
            std::string component;
            std::string controller;
            if (values.size() != 4 || !hexDecode(values[1], nodeId) ||
                !hexDecode(values[2], component) ||
                !hexDecode(values[3], controller))
                return fail();
            const auto node = std::find_if(
                candidate.nodes.begin(), candidate.nodes.end(),
                [&nodeId](const auto& current) {
                    return current.id == nodeId;
                });
            if (node == candidate.nodes.end()) return fail();
            node->componentState.assign(component.begin(), component.end());
            node->controllerState.assign(controller.begin(), controller.end());
        } else if (values[0] == "NODE_GAIN") {
            std::string nodeId;
            double gainDb = 0.0;
            double pan = 0.0;
            if ((values.size() != 3 && values.size() != 4) ||
                !hexDecode(values[1], nodeId) ||
                !number(values[2], gainDb) ||
                (values.size() == 4 && !number(values[3], pan)) ||
                gainDb < GainProcessor::minimumGainDb ||
                gainDb > GainProcessor::maximumGainDb ||
                pan < -1.0 || pan > 1.0)
                return fail();
            const auto node = std::find_if(candidate.nodes.begin(), candidate.nodes.end(),
                [&](const auto& current) { return current.id == nodeId; });
            if (node == candidate.nodes.end() || node->kind != UiProjectNodeKind::Gain)
                return fail();
            node->gainDb = gainDb;
            node->pan = pan;
        } else if (values[0] == "EDGE") {
            UiProjectConnection connection;
            int kind = 0;
            if (values.size() != 6 || !hexDecode(values[1], connection.from) ||
                !hexDecode(values[2], connection.to) || !integer(values[3], kind) ||
                kind < 0 || kind > static_cast<int>(UiProjectConnectionKind::Midi) ||
                !integer(values[4], connection.fromPort) ||
                !integer(values[5], connection.toPort) ||
                connection.from.empty() || connection.to.empty()) return fail();
            connection.kind = static_cast<UiProjectConnectionKind>(kind);
            candidate.connections.push_back(std::move(connection));
        } else if (values[0] == "ARRANGEMENT") {
            if (values.size() != 2 || !number(values[1], candidate.arrangementLengthBeats) ||
                candidate.arrangementLengthBeats < 0.0) return fail();
        } else if (values[0] == "CLIP") {
            UiProjectMidiClip clip;
            if (values.size() != 5 || !hexDecode(values[1], clip.id) ||
                !hexDecode(values[2], clip.targetNodeId) ||
                !number(values[3], clip.startBeat) || !number(values[4], clip.lengthBeats) ||
                clip.id.empty() || clip.targetNodeId.empty() || clip.startBeat < 0.0 ||
                clip.lengthBeats <= 0.0) return fail();
            candidate.midiClips.push_back(std::move(clip));
        } else if (values[0] == "NOTE") {
            std::string clipId;
            UiProjectMidiNote note;
            unsigned pitch = 0, velocity = 0, channel = 0;
            if (values.size() != 7 || !hexDecode(values[1], clipId) ||
                !number(values[2], note.startBeat) || !number(values[3], note.durationBeats) ||
                !integer(values[4], pitch) || !integer(values[5], velocity) ||
                !integer(values[6], channel) || note.startBeat < 0.0 ||
                note.durationBeats <= 0.0 || pitch > 127 || velocity == 0 ||
                velocity > 127 || channel > 15) return fail();
            const auto clip = std::find_if(candidate.midiClips.begin(), candidate.midiClips.end(),
                [&](const auto& current) { return current.id == clipId; });
            if (clip == candidate.midiClips.end() ||
                note.startBeat + note.durationBeats > clip->lengthBeats) return fail();
            note.pitch = static_cast<std::uint8_t>(pitch);
            note.velocity = static_cast<std::uint8_t>(velocity);
            note.channel = static_cast<std::uint8_t>(channel);
            clip->notes.push_back(note);
        } else if (values[0] == "GAIN_LANE") {
            UiProjectGainLane lane;
            if (values.size() != 2 || !hexDecode(values[1], lane.targetNodeId) ||
                lane.targetNodeId.empty()) return fail();
            candidate.gainLanes.push_back(std::move(lane));
        } else if (values[0] == "GAIN_POINT") {
            std::string nodeId;
            GainEnvelopePoint point;
            int linear = 0;
            if (values.size() != 5 || !hexDecode(values[1], nodeId) ||
                !number(values[2], point.beat) || !number(values[3], point.valueDb) ||
                !integer(values[4], linear) || point.beat < 0.0 ||
                (linear != 0 && linear != 1)) return fail();
            const auto lane = std::find_if(candidate.gainLanes.begin(), candidate.gainLanes.end(),
                [&](const auto& current) { return current.targetNodeId == nodeId; });
            if (lane == candidate.gainLanes.end() ||
                (!lane->points.empty() && point.beat <= lane->points.back().beat)) return fail();
            point.linear = linear == 1;
            lane->points.push_back(point);
        } else {
            return fail();
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
