#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <cairo.h>
#include "transmission/AudioProcessor.h"
#include "transmission/GraphRuntimeController.h"
#include "transmission/JackConnectionManager.h"
#include "transmission/JackAudioDevice.h"
#include "transmission/OfflineAudioRenderer.h"
#include "transmission/UiProjectCodec.h"
#include "transmission/Vst3EditorHost.h"
#include "transmission/Vst3Inspector.h"
#include "transmission/Vst3Processor.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <unistd.h>

namespace {
constexpr double nodeWidth = 190.0;
constexpr double minimumNodeHeight = 70.0;
constexpr double portSpacing = 18.0;

enum class NodeKind {
    SystemInput, SystemOutput, PassThrough, Plugin, MidiInput, MidiOutput, Gain
};

struct Node {
    std::string id;
    std::string label;
    NodeKind kind = NodeKind::Plugin;
    std::size_t audioInputs = 0;
    std::size_t audioOutputs = 0;
    std::size_t midiInputs = 0;
    std::size_t midiOutputs = 0;
    double x = 0.0;
    double y = 0.0;
    std::string pluginPath;
    std::string externalPort;
    std::vector<std::string> audioInputLabels;
    std::vector<std::string> audioOutputLabels;
    double gainDb = 0.0;
};

enum class PortKind { Audio, Midi };

struct Edge {
    std::size_t from;
    std::size_t to;
    std::size_t fromPort = 0;
    std::size_t toPort = 0;
    PortKind kind = PortKind::Audio;
};

struct GraphView {
    std::vector<Node> nodes{
        {"system-input", "System Input", NodeKind::SystemInput, 0, 2, 0, 1,
         60.0, 150.0, "", "", {}, {}},
        {"gain", "AGain / VST3", NodeKind::PassThrough, 2, 2, 1, 1,
         340.0, 150.0, "", "", {}, {}},
        {"system-output", "System Output", NodeKind::SystemOutput, 2, 0, 1, 0,
         580.0, 150.0, "", "", {}, {}},
    };
    std::vector<Edge> edges{{0, 1, 0, 0, PortKind::Audio}, {1, 2, 0, 0, PortKind::Audio},
                            {0, 1, 0, 0, PortKind::Midi}, {1, 2, 0, 0, PortKind::Midi}};
    std::size_t dragging = static_cast<std::size_t>(-1);
    double dragX = 0.0;
    double dragY = 0.0;
    std::size_t connectingFrom = static_cast<std::size_t>(-1);
    std::size_t connectingPort = 0;
    PortKind connectingKind = PortKind::Audio;
    double pointerX = 0.0;
    double pointerY = 0.0;
    std::vector<std::string> pluginPaths;
    std::unordered_map<
        std::string, std::unordered_map<std::uint32_t, double>> parameterValues;
    std::unordered_map<std::string, transmission::ProcessorState> pluginStates;
    double arrangementLengthBeats = 0.0;
    std::vector<transmission::UiProjectMidiClip> midiClips;
    std::vector<transmission::UiProjectGainLane> gainLanes;
    std::size_t nextPluginId = 1;
    std::size_t nextMidiInputId = 1;
    std::size_t nextMidiOutputId = 1;
    std::array<std::string, 2> systemInputConnections{"system:capture_1", "system:capture_2"};
    std::array<std::string, 2> systemOutputConnections{"system:playback_1", "system:playback_2"};
    std::unique_ptr<transmission::JackConnectionManager> jackConnections;
    std::unique_ptr<transmission::Vst3EditorHost> editorHost;
#if defined(TRANSMISSION_UI_WITH_JACK) && defined(TRANSMISSION_UI_WITH_VST3)
    std::unique_ptr<transmission::JackAudioDevice> jackDevice;
    std::unique_ptr<transmission::GraphRuntimeController> runtime;
#endif
    guint transportTimer = 0;
    guint externalConnectionTimer = 0;
    unsigned externalConnectionAttempts = 0;
    std::array<bool, 2> pendingInputConnections{};
    std::array<bool, 2> pendingOutputConnections{};
    std::vector<bool> pendingMidiConnections;
    GtkSpinButton* tempo = nullptr;
    GtkSpinButton* loopBars = nullptr;
    GtkToggleButton* loop = nullptr;
    GtkWidget* window = nullptr;
    GtkWidget* canvas = nullptr;
    GtkWidget* playButton = nullptr;
    std::string filePath;
    std::string projectHelperPath;
    std::string lastSavedSnapshot;
    std::size_t requestedBufferSize = 0;
    std::size_t renderAheadMilliseconds = 200;
    std::size_t processingThreads = 0;
    std::thread renderThread;
    std::atomic<bool> renderCancel{false};
    std::atomic<double> renderProgress{0.0};
    guint renderProgressTimer = 0;
    std::mutex renderCompletionMutex;
    guint renderCompletionSource = 0;
    GtkWidget* renderDialog = nullptr;
    GtkProgressBar* renderProgressBar = nullptr;
    bool renderInProgress = false;
};

struct PluginDialogContext {
    GraphView* view = nullptr;
    GtkWidget* canvas = nullptr;
    GtkWidget* dialog = nullptr;
    GtkComboBoxText* selector = nullptr;
};

struct MidiDialogContext {
    GraphView* view = nullptr;
    GtkWidget* canvas = nullptr;
    GtkWidget* dialog = nullptr;
    GtkComboBoxText* selector = nullptr;
    bool input = true;
    std::size_t node = static_cast<std::size_t>(-1);
};

struct AddNodeMenuContext {
    GraphView* view = nullptr;
    GtkWidget* canvas = nullptr;
};

struct SystemDialogContext {
    GraphView* view = nullptr;
    GtkWidget* dialog = nullptr;
    bool input = false;
    std::array<GtkComboBoxText*, 2> selectors{};
};

struct NodeMenuContext {
    GraphView* view = nullptr;
    GtkWidget* canvas = nullptr;
    std::size_t node = static_cast<std::size_t>(-1);
};

void setStatus(GraphView& view, const std::string& message, bool error = false) {
    if (!error || !view.window) return;
    auto* dialog = gtk_message_dialog_new(
        GTK_WINDOW(view.window),
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s", message.c_str());
    g_signal_connect(dialog, "response",
                     G_CALLBACK(+[](GtkDialog* current, gint, gpointer) {
                         gtk_widget_destroy(GTK_WIDGET(current));
                     }),
                     nullptr);
    gtk_widget_show(dialog);
}

bool runtimeRunning(const GraphView& view) {
#if defined(TRANSMISSION_UI_WITH_JACK) && defined(TRANSMISSION_UI_WITH_VST3)
    return view.runtime && view.runtime->running();
#else
    (void)view;
    return false;
#endif
}

transmission::RuntimeProcessorFactory uiProcessorFactory() {
    return [](const transmission::RuntimeGraphNode& node,
              const transmission::AudioDeviceConfig& config,
              std::string& error)
        -> std::unique_ptr<transmission::AudioProcessor> {
        if (node.kind == transmission::RuntimeNodeKind::MidiInput ||
            node.kind == transmission::RuntimeNodeKind::MidiOutput)
            return std::make_unique<transmission::MidiEndpointProcessor>();
        if (node.kind != transmission::RuntimeNodeKind::Plugin)
            return std::make_unique<transmission::PassThroughProcessor>();
#if defined(TRANSMISSION_UI_WITH_VST3)
        auto processor = std::make_unique<transmission::Vst3Processor>();
        if (!processor->initialize(
                node.pluginPath, node.audioInputs, node.audioOutputs,
                config.blockSize, config.sampleRate, error))
            return nullptr;
        return processor;
#else
        (void)config;
        error = "This UI build does not include VST3 hosting support";
        return nullptr;
#endif
    };
}

void stopRuntime(GraphView& view, const char* status = nullptr) {
    if (view.externalConnectionTimer) {
        g_source_remove(view.externalConnectionTimer);
        view.externalConnectionTimer = 0;
    }
    view.externalConnectionAttempts = 0;
    view.pendingInputConnections.fill(false);
    view.pendingOutputConnections.fill(false);
    view.pendingMidiConnections.clear();
#if defined(TRANSMISSION_UI_WITH_JACK) && defined(TRANSMISSION_UI_WITH_VST3)
    if (view.runtime) {
        const bool hadRuntime = view.runtime->running();
        view.runtime->stop();
        if (hadRuntime) {
            std::string stateError;
            const auto states = view.runtime->processorStates(stateError);
            if (stateError.empty()) {
                for (const auto& state : states) {
                    auto& target = view.pluginStates[state.nodeId];
                    if (!state.state.component.empty())
                        target.component = state.state.component;
                    if (!state.state.controller.empty())
                        target.controller = state.state.controller;
                }
            }
            else
                std::cerr << "VST3 state capture failed: "
                          << stateError << '\n';
        }
    }
#endif
    if (status) setStatus(view, status);
}

transmission::RuntimeGraphSnapshot runtimeSnapshot(const GraphView& view) {
    transmission::RuntimeGraphSnapshot snapshot;
    snapshot.nodes.reserve(view.nodes.size());
    std::size_t midiInputPort = 0;
    std::size_t midiOutputPort = 0;
    for (const auto& node : view.nodes) {
        const auto kind = static_cast<transmission::RuntimeNodeKind>(
            static_cast<int>(node.kind));
        std::size_t externalMidiPort = 0;
        if (node.kind == NodeKind::SystemInput || node.kind == NodeKind::MidiInput)
            externalMidiPort = midiInputPort++;
        else if (node.kind == NodeKind::MidiOutput)
            externalMidiPort = midiOutputPort++;
        snapshot.nodes.push_back({
            node.id, kind, node.pluginPath, externalMidiPort,
            node.audioInputs, node.audioOutputs, {}, {}, 0.0, {}});
        snapshot.nodes.back().gainDb = node.gainDb;
        const auto gainLane = std::find_if(
            view.gainLanes.begin(), view.gainLanes.end(),
            [&](const auto& lane) { return lane.targetNodeId == node.id; });
        if (gainLane != view.gainLanes.end())
            snapshot.nodes.back().gainEnvelope = gainLane->points;
        const auto parameters = view.parameterValues.find(node.id);
        if (parameters != view.parameterValues.end()) {
            auto& runtimeParameters = snapshot.nodes.back().parameters;
            runtimeParameters.reserve(parameters->second.size());
            for (const auto& [id, value] : parameters->second)
                runtimeParameters.push_back({id, value});
        }
        const auto state = view.pluginStates.find(node.id);
        if (state != view.pluginStates.end())
            snapshot.nodes.back().state = state->second;
    }
    snapshot.connections.reserve(view.edges.size());
    for (const auto& edge : view.edges) {
        if (edge.from >= view.nodes.size() || edge.to >= view.nodes.size()) continue;
        snapshot.connections.push_back({
            view.nodes[edge.from].id, view.nodes[edge.to].id,
            edge.kind == PortKind::Audio
                ? transmission::RuntimeConnectionKind::Audio
                : transmission::RuntimeConnectionKind::Midi,
            edge.fromPort, edge.toPort});
    }
    for (const auto& clip : view.midiClips) {
        for (const auto& note : clip.notes) {
            const auto status = static_cast<std::uint8_t>(0x90U | note.channel);
            const auto noteOff = static_cast<std::uint8_t>(0x80U | note.channel);
            snapshot.scheduledMidiEvents.push_back({
                clip.targetNodeId, clip.startBeat + note.startBeat,
                {status, note.pitch, note.velocity}});
            snapshot.scheduledMidiEvents.push_back({
                clip.targetNodeId,
                clip.startBeat + note.startBeat + note.durationBeats,
                {noteOff, note.pitch, 0}});
        }
    }
    return snapshot;
}

void cancelPointerInteraction(GtkWidget* canvas, GraphView& view) {
    view.dragging = static_cast<std::size_t>(-1);
    view.connectingFrom = static_cast<std::size_t>(-1);
    gtk_widget_queue_draw(canvas);
}

Node* nodeAt(GraphView& view, double x, double y) {
    for (auto it = view.nodes.rbegin(); it != view.nodes.rend(); ++it) {
        const auto ports = std::max(it->audioInputs + it->midiInputs,
                                    it->audioOutputs + it->midiOutputs);
        const auto height = std::max(
            minimumNodeHeight, static_cast<double>(ports + 1) * portSpacing);
        if (x >= it->x && x <= it->x + nodeWidth &&
            y >= it->y && y <= it->y + height) return &*it;
    }
    return nullptr;
}

struct PortHit {
    std::size_t node = static_cast<std::size_t>(-1);
    std::size_t port = 0;
    bool output = false;
    PortKind kind = PortKind::Audio;
};

double portY(const Node& node, PortKind kind, std::size_t port, bool output) {
    const auto audioPorts = output ? node.audioOutputs : node.audioInputs;
    const auto midiPorts = output ? node.midiOutputs : node.midiInputs;
    const auto total = audioPorts + midiPorts;
    const auto index = kind == PortKind::Audio ? port : audioPorts + port;
    const auto height = std::max(
        minimumNodeHeight, static_cast<double>(total + 1) * portSpacing);
    return node.y + height * static_cast<double>(index + 1) /
                        static_cast<double>(total + 1);
}

PortHit portAt(const GraphView& view, double x, double y) {
    constexpr double radius = 9.0;
    for (std::size_t index = 0; index < view.nodes.size(); ++index) {
        const auto& node = view.nodes[index];
        for (std::size_t port = 0; port < node.audioInputs; ++port) {
            if (std::hypot(x - node.x,
                           y - portY(node, PortKind::Audio, port, false)) <= radius)
                return {index, port, false, PortKind::Audio};
        }
        for (std::size_t port = 0; port < node.audioOutputs; ++port) {
            if (std::hypot(x - (node.x + nodeWidth),
                           y - portY(node, PortKind::Audio, port, true)) <= radius)
                return {index, port, true, PortKind::Audio};
        }
        for (std::size_t port = 0; port < node.midiInputs; ++port) {
            if (std::hypot(x - node.x,
                           y - portY(node, PortKind::Midi, port, false)) <= radius)
                return {index, port, false, PortKind::Midi};
        }
        for (std::size_t port = 0; port < node.midiOutputs; ++port) {
            if (std::hypot(x - (node.x + nodeWidth),
                           y - portY(node, PortKind::Midi, port, true)) <= radius)
                return {index, port, true, PortKind::Midi};
        }
    }
    return {};
}

double pointToSegmentDistance(double px, double py, double x1, double y1, double x2, double y2) {
    const double dx = x2 - x1;
    const double dy = y2 - y1;
    const double lengthSquared = dx * dx + dy * dy;
    const double projection = lengthSquared == 0.0
                                  ? 0.0
                                  : std::clamp(((px - x1) * dx + (py - y1) * dy) / lengthSquared,
                                               0.0, 1.0);
    return std::hypot(px - (x1 + projection * dx), py - (y1 + projection * dy));
}

std::size_t edgeAt(const GraphView& view, double x, double y) {
    constexpr double hitRadius = 9.0;
    constexpr int samples = 32;
    for (std::size_t edgeIndex = 0; edgeIndex < view.edges.size(); ++edgeIndex) {
        const auto& edge = view.edges[edgeIndex];
        const auto& from = view.nodes[edge.from];
        const auto& to = view.nodes[edge.to];
        const double x1 = from.x + nodeWidth;
        const double y1 = portY(from, edge.kind, edge.fromPort, true);
        const double x2 = to.x;
        const double y2 = portY(to, edge.kind, edge.toPort, false);
        const double control = std::max(40.0, (x2 - x1) * 0.45);
        const double c1x = x1 + control;
        const double c1y = y1;
        const double c2x = x2 - control;
        const double c2y = y2;
        double previousX = x1;
        double previousY = y1;
        for (int sample = 1; sample <= samples; ++sample) {
            const double t = static_cast<double>(sample) / samples;
            const double inverse = 1.0 - t;
            const double curveX = inverse * inverse * inverse * x1 +
                                  3.0 * inverse * inverse * t * c1x +
                                  3.0 * inverse * t * t * c2x + t * t * t * x2;
            const double curveY = inverse * inverse * inverse * y1 +
                                  3.0 * inverse * inverse * t * c1y +
                                  3.0 * inverse * t * t * c2y + t * t * t * y2;
            if (pointToSegmentDistance(x, y, previousX, previousY, curveX, curveY) <= hitRadius)
                return edgeIndex;
            previousX = curveX;
            previousY = curveY;
        }
    }
    return static_cast<std::size_t>(-1);
}

void drawPort(cairo_t* cr, double x, double y, bool output, PortKind kind) {
    if (kind == PortKind::Midi)
        cairo_set_source_rgb(cr, output ? 0.90 : 0.68, output ? 0.40 : 0.30, 0.78);
    else
        cairo_set_source_rgb(cr, output ? 0.42 : 0.28, output ? 0.74 : 0.62,
                             output ? 0.92 : 0.76);
    cairo_arc(cr, x, y, 6.0, 0.0, 2.0 * M_PI);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, 0.08, 0.10, 0.14);
    cairo_set_line_width(cr, 1.5);
    cairo_arc(cr, x, y, 6.0, 0.0, 2.0 * M_PI);
    cairo_stroke(cr);
}

std::string midiOutputLabel(const std::string& destination) {
    return destination == "No connection" ? "MIDI Output (none)" : destination;
}

void drawNodeLabel(cairo_t* cr, const std::string& label, double x, double y,
                   double availableWidth = nodeWidth - 30.0) {
    cairo_text_extents_t extents{};
    cairo_text_extents(cr, label.c_str(), &extents);
    if (extents.width <= availableWidth) {
        cairo_move_to(cr, x, y);
        cairo_show_text(cr, label.c_str());
        return;
    }
    std::string shortened = label;
    while (!shortened.empty()) {
        const auto candidate = shortened + "...";
        cairo_text_extents(cr, candidate.c_str(), &extents);
        if (extents.width <= availableWidth) {
            cairo_move_to(cr, x, y);
            cairo_show_text(cr, candidate.c_str());
            return;
        }
        shortened.pop_back();
    }
}

void addPluginFromDialog(GtkDialog*, gint response, gpointer data) {
    auto* context = static_cast<PluginDialogContext*>(data);
    if (response == GTK_RESPONSE_ACCEPT) {
        const auto selected = gtk_combo_box_get_active(GTK_COMBO_BOX(context->selector));
        if (selected >= 0 && static_cast<std::size_t>(selected) < context->view->pluginPaths.size()) {
            const auto& path = context->view->pluginPaths[static_cast<std::size_t>(selected)];
            const auto stem = std::filesystem::path(path).stem().string();
            const auto id = "plugin-" + std::to_string(context->view->nextPluginId++);
            const auto offset = static_cast<double>(context->view->nodes.size() % 3) * 35.0;
            transmission::Vst3PluginTopology topology;
            std::string error;
            if (!transmission::Vst3Inspector().inspectTopology(path, topology, error)) {
                setStatus(*context->view, "Unable to inspect " + stem + ": " + error, true);
                gtk_widget_destroy(context->dialog);
                delete context;
                return;
            }
            std::vector<std::string> inputLabels;
            std::vector<std::string> outputLabels;
            for (const auto& port : topology.audioInputs)
                inputLabels.push_back(port.name);
            for (const auto& port : topology.audioOutputs)
                outputLabels.push_back(port.name);
            stopRuntime(*context->view, "Graph changed — press Play to compile and start audio");
            context->view->nodes.push_back({
                id, topology.name.empty() ? stem : topology.name, NodeKind::Plugin,
                topology.audioInputs.size(), topology.audioOutputs.size(),
                topology.midiInputs, topology.midiOutputs, 300.0 + offset,
                300.0 + offset, path, "", std::move(inputLabels),
                std::move(outputLabels)});
            gtk_widget_queue_draw(context->canvas);
        }
    }
    gtk_widget_destroy(context->dialog);
    delete context;
}

void showPluginDialog(GtkWidget* canvas, GraphView& view) {
    auto* dialog = gtk_dialog_new_with_buttons(
        "Add VST3 Plugin", GTK_WINDOW(gtk_widget_get_toplevel(canvas)),
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        "_Cancel", GTK_RESPONSE_CANCEL, "_Add", GTK_RESPONSE_ACCEPT, nullptr);
    auto* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 16);
    GtkWidget* label = gtk_label_new("Choose a bundle from ~/.vst3:");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(content), label, FALSE, FALSE, 8);
    auto* selector = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
    for (const auto& path : view.pluginPaths)
        gtk_combo_box_text_append_text(selector, std::filesystem::path(path).stem().c_str());
    if (!view.pluginPaths.empty()) gtk_combo_box_set_active(GTK_COMBO_BOX(selector), 0);
    gtk_widget_set_size_request(GTK_WIDGET(selector), 320, -1);
    gtk_box_pack_start(GTK_BOX(content), GTK_WIDGET(selector), FALSE, FALSE, 0);
    auto* context = new PluginDialogContext{&view, canvas, dialog, selector};
    g_signal_connect(dialog, "response", G_CALLBACK(addPluginFromDialog), context);
    gtk_widget_show_all(dialog);
}

void addMidiNodeFromDialog(GtkDialog*, gint response, gpointer data) {
    auto* context = static_cast<MidiDialogContext*>(data);
    if (response == GTK_RESPONSE_ACCEPT) {
        if (auto* selected =
                gtk_combo_box_text_get_active_text(context->selector)) {
            stopRuntime(*context->view,
                        "Graph changed — press Play to compile and start audio");
            if (context->node < context->view->nodes.size()) {
                auto& node = context->view->nodes[context->node];
                node.externalPort = selected;
                if (node.kind == NodeKind::MidiOutput)
                    node.label = midiOutputLabel(node.externalPort);
            } else {
                auto& nextId = context->input ? context->view->nextMidiInputId
                                              : context->view->nextMidiOutputId;
                const auto id =
                    std::string(context->input ? "midi-input-" : "midi-output-") +
                    std::to_string(nextId++);
                const auto offset =
                    static_cast<double>(context->view->nodes.size() % 3) * 35.0;
                context->view->nodes.push_back({
                    id, context->input ? "MIDI Input"
                                       : midiOutputLabel(selected),
                    context->input ? NodeKind::MidiInput : NodeKind::MidiOutput,
                    0, 0, context->input ? 0U : 1U, context->input ? 1U : 0U,
                    300.0 + offset, 300.0 + offset, "", selected, {}, {}});
            }
            g_free(selected);
            gtk_widget_queue_draw(context->canvas);
        }
    }
    gtk_widget_destroy(context->dialog);
    delete context;
}

void showMidiDialog(GtkWidget* canvas, GraphView& view, bool input,
                    std::size_t node = static_cast<std::size_t>(-1)) {
    const bool editing = node < view.nodes.size();
    auto* dialog = gtk_dialog_new_with_buttons(
        editing ? (input ? "Edit MIDI Input" : "Edit MIDI Output")
                : (input ? "Add MIDI Input" : "Add MIDI Output"),
        GTK_WINDOW(gtk_widget_get_toplevel(canvas)),
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        "_Cancel", GTK_RESPONSE_CANCEL, editing ? "_Apply" : "_Add",
        GTK_RESPONSE_ACCEPT, nullptr);
    auto* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 16);
    auto* label = gtk_label_new(
        input ? "Choose the external MIDI source:"
              : "Choose the external MIDI destination:");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(content), label, FALSE, FALSE, 8);
    auto* selector = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
    auto ports = input ? view.jackConnections->midiInputSources()
                       : view.jackConnections->midiOutputDestinations();
    ports.erase(std::remove_if(ports.begin(), ports.end(), [](const auto& port) {
        return port.starts_with("transmission:");
    }), ports.end());
    if (editing && std::find(ports.begin(), ports.end(),
                             view.nodes[node].externalPort) == ports.end())
        ports.push_back(view.nodes[node].externalPort);
    if (std::find(ports.begin(), ports.end(), "No connection") == ports.end())
        ports.push_back("No connection");
    for (const auto& port : ports)
        gtk_combo_box_text_append_text(selector, port.c_str());
    const auto selected = editing
        ? std::find(ports.begin(), ports.end(), view.nodes[node].externalPort)
        : ports.begin();
    gtk_combo_box_set_active(
        GTK_COMBO_BOX(selector),
        selected == ports.end() ? 0 : static_cast<gint>(selected - ports.begin()));
    gtk_widget_set_size_request(GTK_WIDGET(selector), 420, -1);
    gtk_box_pack_start(GTK_BOX(content), GTK_WIDGET(selector), FALSE, FALSE, 0);
    auto* context =
        new MidiDialogContext{&view, canvas, dialog, selector, input, node};
    g_signal_connect(dialog, "response", G_CALLBACK(addMidiNodeFromDialog), context);
    gtk_widget_show_all(dialog);
}

void addPluginActivated(GtkMenuItem*, gpointer data) {
    auto* context = static_cast<AddNodeMenuContext*>(data);
    showPluginDialog(context->canvas, *context->view);
}

void addMidiInputActivated(GtkMenuItem*, gpointer data) {
    auto* context = static_cast<AddNodeMenuContext*>(data);
    showMidiDialog(context->canvas, *context->view, true);
}

void addMidiOutputActivated(GtkMenuItem*, gpointer data) {
    auto* context = static_cast<AddNodeMenuContext*>(data);
    showMidiDialog(context->canvas, *context->view, false);
}

void showAddNodeMenu(GtkWidget* canvas, GraphView& view,
                     GdkEventButton* event) {
    auto* menu = gtk_menu_new();
    auto* plugin = gtk_menu_item_new_with_label("Add VST3 Plugin…");
    auto* midiInput = gtk_menu_item_new_with_label("Add MIDI Input…");
    auto* midiOutput = gtk_menu_item_new_with_label("Add MIDI Output…");
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), plugin);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), midiInput);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), midiOutput);
    auto* context = new AddNodeMenuContext{&view, canvas};
    g_signal_connect(plugin, "activate", G_CALLBACK(addPluginActivated), context);
    g_signal_connect(midiInput, "activate", G_CALLBACK(addMidiInputActivated), context);
    g_signal_connect(midiOutput, "activate", G_CALLBACK(addMidiOutputActivated), context);
    g_signal_connect_data(
        menu, "destroy",
        G_CALLBACK(+[](GtkWidget*, gpointer data) {
            delete static_cast<AddNodeMenuContext*>(data);
        }),
        context, nullptr, static_cast<GConnectFlags>(0));
    gtk_widget_show_all(menu);
    gtk_menu_popup_at_pointer(GTK_MENU(menu), reinterpret_cast<GdkEvent*>(event));
}

bool systemChannelUsed(const GraphView& view, bool input, std::size_t channel) {
    return std::any_of(view.edges.begin(), view.edges.end(), [&](const auto& edge) {
        if (edge.kind != PortKind::Audio ||
            edge.from >= view.nodes.size() || edge.to >= view.nodes.size())
            return false;
        return input
            ? view.nodes[edge.from].id == "system-input" && edge.fromPort == channel
            : view.nodes[edge.to].id == "system-output" && edge.toPort == channel;
    });
}

bool midiEndpointUsed(const GraphView& view, std::size_t nodeIndex) {
    return std::any_of(view.edges.begin(), view.edges.end(), [&](const auto& edge) {
        if (edge.kind != PortKind::Midi) return false;
        return view.nodes[nodeIndex].kind == NodeKind::MidiInput
            ? edge.from == nodeIndex : edge.to == nodeIndex;
    });
}

bool applyExternalConnections(GraphView& view, std::string& error) {
    if (!view.jackConnections || !view.jackConnections->available()) {
        error = "JACK server is not available";
        return false;
    }
    bool applied = true;
    const auto appendError = [&](const std::string& message) {
        if (!error.empty()) error += "; ";
        error += message;
        applied = false;
    };
    for (std::size_t index = 0; index < view.systemInputConnections.size(); ++index) {
        if (!systemChannelUsed(view, true, index)) continue;
        std::string routeError;
        if (!view.jackConnections->connectInput(index, view.systemInputConnections[index],
                                                routeError))
            appendError("input " + std::to_string(index + 1) + ": " + routeError);
    }
    for (std::size_t index = 0; index < view.systemOutputConnections.size(); ++index) {
        if (!systemChannelUsed(view, false, index)) continue;
        std::string routeError;
        if (!view.jackConnections->connectOutput(index, view.systemOutputConnections[index],
                                                 routeError))
            appendError("output " + std::to_string(index + 1) + ": " + routeError);
    }
    return applied;
}

gboolean retryExternalConnections(gpointer data) {
    auto& view = *static_cast<GraphView*>(data);
    if (!runtimeRunning(view)) {
        view.externalConnectionTimer = 0;
        view.externalConnectionAttempts = 0;
        return G_SOURCE_REMOVE;
    }
    std::string error;
    const auto appendError = [&](const std::string& message) {
        if (!error.empty()) error += "; ";
        error += message;
    };
    for (std::size_t index = 0; index < view.pendingInputConnections.size(); ++index) {
        if (!view.pendingInputConnections[index]) continue;
        std::string routeError;
        if (view.jackConnections->connectInput(
                index, view.systemInputConnections[index], routeError))
            view.pendingInputConnections[index] = false;
        else
            appendError("input " + std::to_string(index + 1) + ": " + routeError);
    }
    for (std::size_t index = 0; index < view.pendingOutputConnections.size(); ++index) {
        if (!view.pendingOutputConnections[index]) continue;
        std::string routeError;
        if (view.jackConnections->connectOutput(
                index, view.systemOutputConnections[index], routeError))
            view.pendingOutputConnections[index] = false;
        else
            appendError("output " + std::to_string(index + 1) + ": " + routeError);
    }
    std::size_t midiInputPort = 0;
    std::size_t midiOutputPort = 0;
    for (std::size_t index = 0; index < view.nodes.size(); ++index) {
        const auto& node = view.nodes[index];
        if (node.kind == NodeKind::SystemInput) {
            ++midiInputPort;
            continue;
        }
        if (node.kind == NodeKind::MidiInput) {
            if (index < view.pendingMidiConnections.size() &&
                view.pendingMidiConnections[index]) {
                std::string routeError;
                if (view.jackConnections->connectMidiInput(
                        midiInputPort, node.externalPort, routeError))
                    view.pendingMidiConnections[index] = false;
                else
                    appendError(node.label + ": " + routeError);
            }
            ++midiInputPort;
        } else if (node.kind == NodeKind::MidiOutput) {
            if (index < view.pendingMidiConnections.size() &&
                view.pendingMidiConnections[index]) {
                std::string routeError;
                if (view.jackConnections->connectMidiOutput(
                        midiOutputPort, node.externalPort, routeError))
                    view.pendingMidiConnections[index] = false;
                else
                    appendError(node.label + ": " + routeError);
            }
            ++midiOutputPort;
        }
    }
    const auto pending =
        std::any_of(view.pendingInputConnections.begin(),
                    view.pendingInputConnections.end(), [](bool value) { return value; }) ||
        std::any_of(view.pendingOutputConnections.begin(),
                    view.pendingOutputConnections.end(), [](bool value) { return value; }) ||
        std::any_of(view.pendingMidiConnections.begin(),
                    view.pendingMidiConnections.end(), [](bool value) { return value; });
    if (!pending) {
        view.externalConnectionTimer = 0;
        view.externalConnectionAttempts = 0;
        return G_SOURCE_REMOVE;
    }
    ++view.externalConnectionAttempts;
    if (view.externalConnectionAttempts >= 20) {
        view.externalConnectionTimer = 0;
        view.externalConnectionAttempts = 0;
        setStatus(view, "Audio is running, but JACK routing failed: " + error, true);
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

void scheduleExternalConnections(GraphView& view) {
    if (view.externalConnectionTimer)
        g_source_remove(view.externalConnectionTimer);
    view.externalConnectionAttempts = 0;
    for (std::size_t index = 0; index < view.pendingInputConnections.size(); ++index)
        view.pendingInputConnections[index] = systemChannelUsed(view, true, index);
    for (std::size_t index = 0; index < view.pendingOutputConnections.size(); ++index)
        view.pendingOutputConnections[index] = systemChannelUsed(view, false, index);
    view.pendingMidiConnections.assign(view.nodes.size(), false);
    for (std::size_t index = 0; index < view.nodes.size(); ++index) {
        if ((view.nodes[index].kind == NodeKind::MidiInput ||
             view.nodes[index].kind == NodeKind::MidiOutput) &&
            midiEndpointUsed(view, index))
            view.pendingMidiConnections[index] = true;
    }
    view.externalConnectionTimer = g_timeout_add(50, retryExternalConnections, &view);
}

void systemDialogResponse(GtkDialog*, gint response, gpointer data) {
    auto* context = static_cast<SystemDialogContext*>(data);
    if (response == GTK_RESPONSE_ACCEPT) {
        auto& connections = context->input ? context->view->systemInputConnections
                                           : context->view->systemOutputConnections;
        for (std::size_t index = 0; index < connections.size(); ++index) {
            if (auto* selected = gtk_combo_box_text_get_active_text(context->selectors[index])) {
                connections[index] = selected;
                g_free(selected);
            }
        }
        if (runtimeRunning(*context->view)) {
            if (context->view->externalConnectionTimer) {
                g_source_remove(context->view->externalConnectionTimer);
                context->view->externalConnectionTimer = 0;
                context->view->externalConnectionAttempts = 0;
                context->view->pendingInputConnections.fill(false);
                context->view->pendingOutputConnections.fill(false);
                context->view->pendingMidiConnections.clear();
            }
            std::string error;
            if (!applyExternalConnections(*context->view, error))
                setStatus(*context->view, error, true);
            else
                setStatus(*context->view, "External JACK connections applied");
        } else {
            setStatus(*context->view,
                      "Connection choices saved — they will be applied when audio starts");
        }
    }
    gtk_widget_destroy(context->dialog);
    delete context;
}

void showSystemDialog(GtkWidget* canvas, GraphView& view, bool input) {
    auto* dialog = gtk_dialog_new_with_buttons(
        input ? "System Input Connections" : "System Output Connections",
        GTK_WINDOW(gtk_widget_get_toplevel(canvas)),
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        "_Cancel", GTK_RESPONSE_CANCEL, "_Apply", GTK_RESPONSE_ACCEPT, nullptr);
    auto* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 16);
    GtkWidget* description = gtk_label_new(
        input ? "Select the external source for each Transmission input channel."
              : "Select the external destination for each Transmission output channel.");
    gtk_label_set_xalign(GTK_LABEL(description), 0.0F);
    gtk_label_set_line_wrap(GTK_LABEL(description), TRUE);
    gtk_box_pack_start(GTK_BOX(content), description, FALSE, FALSE, 8);
    auto* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_box_pack_start(GTK_BOX(content), grid, FALSE, FALSE, 0);
    auto* context = new SystemDialogContext{&view, dialog, input, {}};
    const auto& connections = input ? view.systemInputConnections : view.systemOutputConnections;
    const auto jackPorts = input ? view.jackConnections->inputSources()
                                 : view.jackConnections->outputDestinations();
    for (std::size_t index = 0; index < connections.size(); ++index) {
        GtkWidget* label = gtk_label_new(("Channel " + std::to_string(index + 1)).c_str());
        gtk_widget_set_halign(label, GTK_ALIGN_START);
        gtk_grid_attach(GTK_GRID(grid), label, 0, static_cast<gint>(index), 1, 1);
        auto* selector = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
        std::vector<std::string> options = jackPorts;
        if (jackPorts.empty()) {
            const auto prefix = input ? "system:capture_" : "system:playback_";
            options.push_back(prefix + std::to_string(index + 1));
            options.push_back(prefix + std::to_string(index == 0 ? 2 : 1));
        }
        if (std::find(options.begin(), options.end(), connections[index]) == options.end())
            options.push_back(connections[index]);
        if (std::find(options.begin(), options.end(), "No connection") == options.end())
            options.push_back("No connection");
        for (const auto& option : options)
            gtk_combo_box_text_append_text(selector, option.c_str());
        const auto selected = std::find(options.begin(), options.end(), connections[index]);
        const auto active = selected == options.end()
            ? 0
            : static_cast<gint>(selected - options.begin());
        gtk_combo_box_set_active(GTK_COMBO_BOX(selector), active);
        gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(selector), 1, static_cast<gint>(index), 1, 1);
        context->selectors[index] = selector;
    }
    g_signal_connect(dialog, "response", G_CALLBACK(systemDialogResponse), context);
    gtk_widget_show_all(dialog);
}

void updateTransportDisplay(GraphView& view) {
    const auto running = runtimeRunning(view);
    if (view.playButton) {
        const auto* desired = running ? "Stop" : "Play";
        const auto* current = gtk_button_get_label(GTK_BUTTON(view.playButton));
        if (!current || std::string(current) != desired)
            gtk_button_set_label(GTK_BUTTON(view.playButton), desired);
    }
}

gboolean transportTick(gpointer data) {
    auto& view = *static_cast<GraphView*>(data);
    if (!runtimeRunning(view)) {
        view.transportTimer = 0;
        updateTransportDisplay(view);
        return G_SOURCE_REMOVE;
    }
    updateTransportDisplay(view);
    return G_SOURCE_CONTINUE;
}

void startTransportTimer(GraphView& view) {
    if (!view.transportTimer)
        view.transportTimer = g_timeout_add(30, transportTick, &view);
}

void playStopClicked(GtkButton*, gpointer data) {
    auto& view = *static_cast<GraphView*>(data);
    if (runtimeRunning(view)) {
        stopRuntime(view, "Audio stopped");
        updateTransportDisplay(view);
        return;
    }
#if defined(TRANSMISSION_UI_WITH_JACK) && defined(TRANSMISSION_UI_WITH_VST3)
    transmission::AudioDeviceConfig deviceConfig;
    deviceConfig.channels = 2;
    deviceConfig.midiInputs = static_cast<std::size_t>(std::count_if(
        view.nodes.begin(), view.nodes.end(), [](const auto& node) {
            return node.kind == NodeKind::SystemInput ||
                   node.kind == NodeKind::MidiInput;
        }));
    deviceConfig.midiOutputs = static_cast<std::size_t>(std::count_if(
        view.nodes.begin(), view.nodes.end(), [](const auto& node) {
            return node.kind == NodeKind::MidiOutput;
        }));
    std::string error;
    std::string bufferWarning;
    if (view.requestedBufferSize != 0) {
        if (!view.jackConnections->setBufferSize(
                view.requestedBufferSize, error)) {
            setStatus(view, error, true);
            updateTransportDisplay(view);
            return;
        }
        if (!error.empty()) {
            bufferWarning = std::move(error);
            error.clear();
            view.requestedBufferSize = 0;
        }
    }
    if (!view.jackConnections->deviceConfig(deviceConfig, error)) {
        setStatus(view, error, true);
        updateTransportDisplay(view);
        return;
    }
    const auto renderAheadBlocks =
        view.renderAheadMilliseconds == 0
        ? std::size_t{0}
        : static_cast<std::size_t>(std::ceil(
              static_cast<double>(view.renderAheadMilliseconds) *
              deviceConfig.sampleRate /
              (1000.0 * static_cast<double>(deviceConfig.blockSize))));
    if (!view.runtime->setRenderAheadBlocks(renderAheadBlocks) ||
        !view.runtime->setProcessingThreadCount(
            view.processingThreads)) {
        setStatus(view, "Unable to configure render-ahead buffering", true);
        updateTransportDisplay(view);
        return;
    }
    transmission::RuntimeTransportConfig transportConfig;
    transportConfig.tempo = gtk_spin_button_get_value(view.tempo);
    transportConfig.loopEnabled = view.loop && gtk_toggle_button_get_active(view.loop);
    transportConfig.loopEndBeat =
        std::max(1.0, gtk_spin_button_get_value(view.loopBars)) * 4.0;
    if (!view.runtime->start(runtimeSnapshot(view), *view.jackDevice, deviceConfig,
                             transportConfig, error)) {
        if (error == "unable to configure the audio device" &&
            !view.jackDevice->lastError().empty())
            error = view.jackDevice->lastError();
        setStatus(view, error, true);
        updateTransportDisplay(view);
        return;
    }
    scheduleExternalConnections(view);
    startTransportTimer(view);
    if (!bufferWarning.empty())
        setStatus(view, "Audio started, but " + bufferWarning, true);
#else
    setStatus(view, "This UI build requires both JACK and VST3 support", true);
#endif
    updateTransportDisplay(view);
}

void audioSettingsActivated(GtkMenuItem*, gpointer data) {
    auto& view = *static_cast<GraphView*>(data);
    auto* dialog = gtk_dialog_new_with_buttons(
        "Audio Settings", GTK_WINDOW(view.window),
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL |
                                   GTK_DIALOG_DESTROY_WITH_PARENT),
        "_Cancel", GTK_RESPONSE_CANCEL, "_Apply", GTK_RESPONSE_ACCEPT,
        nullptr);
    auto* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 16);
    gtk_box_set_spacing(GTK_BOX(content), 10);

    transmission::AudioDeviceConfig current;
    std::string configError;
    const bool haveCurrent =
        view.jackConnections &&
        view.jackConnections->deviceConfig(current, configError);
    const auto currentText = haveCurrent
        ? "Current JACK/PipeWire period: " +
              std::to_string(current.blockSize) + " frames (" +
              std::to_string(static_cast<int>(
                  1000.0 * static_cast<double>(current.blockSize) /
                  current.sampleRate)) + " ms at " +
              std::to_string(static_cast<int>(current.sampleRate)) + " Hz)"
        : "Current JACK/PipeWire period is unavailable.";
    auto* currentLabel = gtk_label_new(currentText.c_str());
    gtk_label_set_xalign(GTK_LABEL(currentLabel), 0.0F);
    gtk_box_pack_start(GTK_BOX(content), currentLabel, FALSE, FALSE, 0);

    auto* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    auto* label = gtk_label_new("JACK/PipeWire period");
    auto* periodSelector = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
    constexpr std::array<std::size_t, 6> sizes{
        0, 256, 512, 1024, 2048, 4096};
    gtk_combo_box_text_append_text(
        periodSelector, "Use current server setting");
    for (std::size_t index = 1; index < sizes.size(); ++index)
        gtk_combo_box_text_append_text(
            periodSelector,
            (std::to_string(sizes[index]) + " frames").c_str());
    const auto selected = std::find(
        sizes.begin(), sizes.end(), view.requestedBufferSize);
    gtk_combo_box_set_active(
        GTK_COMBO_BOX(periodSelector),
        selected == sizes.end()
            ? 0 : static_cast<gint>(selected - sizes.begin()));
    gtk_box_pack_start(GTK_BOX(row), label, FALSE, FALSE, 0);
    gtk_box_pack_start(
        GTK_BOX(row), GTK_WIDGET(periodSelector), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), row, FALSE, FALSE, 0);

    auto* renderRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    auto* renderLabel = gtk_label_new("Render ahead");
    auto* renderSelector = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
    constexpr std::array<std::size_t, 4> renderDurations{0, 50, 100, 200};
    gtk_combo_box_text_append_text(renderSelector, "Off");
    gtk_combo_box_text_append_text(renderSelector, "50 ms");
    gtk_combo_box_text_append_text(renderSelector, "100 ms");
    gtk_combo_box_text_append_text(renderSelector, "200 ms (recommended)");
    const auto renderSelected = std::find(
        renderDurations.begin(), renderDurations.end(),
        view.renderAheadMilliseconds);
    gtk_combo_box_set_active(
        GTK_COMBO_BOX(renderSelector),
        renderSelected == renderDurations.end()
            ? 0
            : static_cast<gint>(
                  renderSelected - renderDurations.begin()));
    gtk_box_pack_start(
        GTK_BOX(renderRow), renderLabel, FALSE, FALSE, 0);
    gtk_box_pack_start(
        GTK_BOX(renderRow), GTK_WIDGET(renderSelector), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), renderRow, FALSE, FALSE, 0);

    auto* threadRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    auto* threadLabel = gtk_label_new("Processing threads");
    auto* threadSelector = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
    const auto hardwareThreads =
        std::max(1U, std::thread::hardware_concurrency());
    gtk_combo_box_text_append_text(
        threadSelector,
        ("Automatic (up to " + std::to_string(hardwareThreads) + ")").c_str());
    for (std::size_t threads = 1; threads <= hardwareThreads; ++threads)
        gtk_combo_box_text_append_text(
            threadSelector,
            (std::to_string(threads) +
             (threads == 1 ? " thread" : " threads")).c_str());
    const auto selectedThreads =
        view.processingThreads <= hardwareThreads
        ? static_cast<gint>(view.processingThreads)
        : 0;
    gtk_combo_box_set_active(
        GTK_COMBO_BOX(threadSelector), selectedThreads);
    gtk_box_pack_start(
        GTK_BOX(threadRow), threadLabel, FALSE, FALSE, 0);
    gtk_box_pack_start(
        GTK_BOX(threadRow), GTK_WIDGET(threadSelector), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), threadRow, FALSE, FALSE, 0);

    auto* explanation = gtk_label_new(
        "Render ahead delays all paths equally and processes independent graph "
        "branches concurrently. Automatic threading uses the machine's "
        "available hardware but never creates more processing threads than "
        "the graph can use. The added audio and MIDI latency protects against "
        "CPU and scheduling spikes without changing the global audio server.");
    gtk_label_set_xalign(GTK_LABEL(explanation), 0.0F);
    gtk_label_set_line_wrap(GTK_LABEL(explanation), TRUE);
    gtk_widget_set_size_request(explanation, 460, -1);
    gtk_box_pack_start(GTK_BOX(content), explanation, FALSE, FALSE, 0);

#if defined(TRANSMISSION_UI_WITH_JACK) && defined(TRANSMISSION_UI_WITH_VST3)
    if (view.runtime) {
        const auto diagnostics = view.runtime->diagnostics();
        auto diagnosticText =
            "Audio xruns/underruns since application start: " +
            std::to_string(diagnostics.underruns) +
            "\nLate render blocks: " +
            std::to_string(diagnostics.renderLateBlocks) +
            "; queue drops: " +
            std::to_string(diagnostics.renderQueueDrops) +
            "\nGraph processing threads: " +
            std::to_string(diagnostics.processingThreads) +
            "; render-ahead blocks: " +
            std::to_string(diagnostics.renderAheadBlocks) +
            "\nWorker render time: average " +
            std::to_string(static_cast<int>(
                diagnostics.averageRenderMicroseconds)) +
            " µs; maximum " +
            std::to_string(static_cast<int>(
                diagnostics.maximumRenderMicroseconds)) + " µs";
        const auto processorTimings = view.runtime->processorTimings();
        const auto slowest = std::max_element(
            processorTimings.begin(), processorTimings.end(),
            [](const auto& left, const auto& right) {
                return left.maximumMicroseconds <
                       right.maximumMicroseconds;
            });
        if (slowest != processorTimings.end() && slowest->calls != 0)
            diagnosticText +=
                "\nSlowest node: " + slowest->nodeId +
                " (average " +
                std::to_string(static_cast<int>(
                    slowest->averageMicroseconds)) +
                " µs; maximum " +
                std::to_string(static_cast<int>(
                    slowest->maximumMicroseconds)) + " µs)";
        auto* diagnosticLabel = gtk_label_new(diagnosticText.c_str());
        gtk_label_set_xalign(GTK_LABEL(diagnosticLabel), 0.0F);
        gtk_box_pack_start(
            GTK_BOX(content), diagnosticLabel, FALSE, FALSE, 0);
    }
#endif

    gtk_widget_show_all(dialog);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        const auto active =
            gtk_combo_box_get_active(GTK_COMBO_BOX(periodSelector));
        const auto renderActive =
            gtk_combo_box_get_active(GTK_COMBO_BOX(renderSelector));
        const auto threadActive =
            gtk_combo_box_get_active(GTK_COMBO_BOX(threadSelector));
        if (active >= 0 &&
            static_cast<std::size_t>(active) < sizes.size() &&
            renderActive >= 0 &&
            static_cast<std::size_t>(renderActive) <
                renderDurations.size() &&
            threadActive >= 0 &&
            static_cast<std::size_t>(threadActive) <=
                hardwareThreads) {
            if (runtimeRunning(view))
                stopRuntime(view,
                            "Audio settings changed — press Play to restart");
            view.requestedBufferSize =
                sizes[static_cast<std::size_t>(active)];
            view.renderAheadMilliseconds =
                renderDurations[static_cast<std::size_t>(renderActive)];
            view.processingThreads =
                static_cast<std::size_t>(threadActive);
            updateTransportDisplay(view);
        }
    }
    gtk_widget_destroy(dialog);
}

void tempoChanged(GtkSpinButton* spin, gpointer data) {
    auto& view = *static_cast<GraphView*>(data);
    (void)spin;
    if (runtimeRunning(view))
        stopRuntime(view, "Tempo changed — press Play to restart audio");
    updateTransportDisplay(view);
}

void loopChanged(GtkWidget*, gpointer data) {
    auto& view = *static_cast<GraphView*>(data);
    if (runtimeRunning(view))
        stopRuntime(view, "Loop changed — press Play to restart audio");
    updateTransportDisplay(view);
}

void resetTransportClicked(GtkButton*, gpointer data) {
    auto& view = *static_cast<GraphView*>(data);
    stopRuntime(view, "Audio stopped and transport reset");
    updateTransportDisplay(view);
}

void destroyNodeMenuContext(GtkWidget*, gpointer data) {
    delete static_cast<NodeMenuContext*>(data);
}

void openPluginEditor(GraphView& view, const Node& node) {
    if (!view.editorHost || node.pluginPath.empty()) return;
    const auto nodeId = node.id;
    std::vector<std::pair<std::uint32_t, double>> parameters;
    if (const auto values = view.parameterValues.find(nodeId);
        values != view.parameterValues.end()) {
        parameters.reserve(values->second.size());
        for (const auto& [id, value] : values->second)
            parameters.emplace_back(id, value);
    }
    const auto state = view.pluginStates.contains(nodeId)
        ? view.pluginStates.at(nodeId)
        : transmission::ProcessorState{};
    view.editorHost->open(
        node.pluginPath, node.label,
        [&view, nodeId](std::uint32_t parameterId, double normalizedValue) {
            view.parameterValues[nodeId][parameterId] = normalizedValue;
#if defined(TRANSMISSION_UI_WITH_JACK) && defined(TRANSMISSION_UI_WITH_VST3)
            if (runtimeRunning(view) && view.runtime) {
                std::string error;
                if (!view.runtime->setParameter(
                        nodeId, parameterId, normalizedValue, error))
                    std::cerr << "VST3 editor parameter forwarding failed for "
                              << nodeId << ": " << error << "\n";
            }
#endif
        },
        [&view, nodeId](transmission::ProcessorState updatedState) {
            auto& target = view.pluginStates[nodeId];
            if (!updatedState.component.empty())
                target.component = std::move(updatedState.component);
            if (!updatedState.controller.empty())
                target.controller = std::move(updatedState.controller);
        },
        state, parameters);
}

void editNodeFromMenu(GtkMenuItem*, gpointer data) {
    auto* context = static_cast<NodeMenuContext*>(data);
    if (context->node >= context->view->nodes.size()) return;
    auto& node = context->view->nodes[context->node];
    if (node.kind == NodeKind::SystemInput || node.kind == NodeKind::SystemOutput) {
        showSystemDialog(context->canvas, *context->view, node.id == "system-input");
    } else if (node.kind == NodeKind::MidiInput ||
               node.kind == NodeKind::MidiOutput) {
        showMidiDialog(context->canvas, *context->view,
                       node.kind == NodeKind::MidiInput, context->node);
    } else if (!node.pluginPath.empty() && context->view->editorHost) {
        openPluginEditor(*context->view, node);
    }
}

void removeNodeFromMenu(GtkMenuItem*, gpointer data) {
    auto* context = static_cast<NodeMenuContext*>(data);
    auto& view = *context->view;
    if (context->node >= view.nodes.size()) return;
    stopRuntime(view, "Graph changed — press Play to compile and start audio");
    view.parameterValues.erase(view.nodes[context->node].id);
    view.pluginStates.erase(view.nodes[context->node].id);
    view.edges.erase(std::remove_if(view.edges.begin(), view.edges.end(), [&](const auto& edge) {
        return edge.from == context->node || edge.to == context->node;
    }), view.edges.end());
    view.nodes.erase(view.nodes.begin() + static_cast<std::ptrdiff_t>(context->node));
    for (auto& edge : view.edges) {
        if (edge.from > context->node) --edge.from;
        if (edge.to > context->node) --edge.to;
    }
    gtk_widget_queue_draw(context->canvas);
}

void showNodeMenu(GtkWidget* canvas, GraphView& view, std::size_t nodeIndex, GdkEventButton* event) {
    auto* menu = gtk_menu_new();
    auto* edit = gtk_menu_item_new_with_label("Edit");
    auto* remove = gtk_menu_item_new_with_label("Remove");
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), edit);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), remove);
    auto* context = new NodeMenuContext{&view, canvas, nodeIndex};
    g_signal_connect(edit, "activate", G_CALLBACK(editNodeFromMenu), context);
    g_signal_connect(remove, "activate", G_CALLBACK(removeNodeFromMenu), context);
    g_signal_connect_data(menu, "destroy", G_CALLBACK(destroyNodeMenuContext), context, nullptr,
                          static_cast<GConnectFlags>(0));
    gtk_widget_show_all(menu);
    gtk_menu_popup_at_pointer(GTK_MENU(menu), reinterpret_cast<GdkEvent*>(event));
}

void drawArrow(cairo_t* cr, double x1, double y1, double x2, double y2) {
    const double control = std::max(40.0, (x2 - x1) * 0.45);
    cairo_move_to(cr, x1, y1);
    cairo_curve_to(cr, x1 + control, y1, x2 - control, y2, x2, y2);
    cairo_stroke(cr);
    const double angle = std::atan2(y2 - y1, x2 - x1);
    cairo_move_to(cr, x2, y2);
    cairo_line_to(cr, x2 - 11.0 * std::cos(angle - 0.45), y2 - 11.0 * std::sin(angle - 0.45));
    cairo_move_to(cr, x2, y2);
    cairo_line_to(cr, x2 - 11.0 * std::cos(angle + 0.45), y2 - 11.0 * std::sin(angle + 0.45));
    cairo_stroke(cr);
}

gboolean drawGraph(GtkWidget*, cairo_t* cr, gpointer data) {
    auto& view = *static_cast<GraphView*>(data);
    cairo_set_source_rgb(cr, 0.075, 0.085, 0.11);
    cairo_paint(cr);

    cairo_set_line_width(cr, 2.5);
    cairo_set_source_rgb(cr, 0.32, 0.62, 0.86);
    for (const auto& edge : view.edges) {
        const auto& from = view.nodes[edge.from];
        const auto& to = view.nodes[edge.to];
        const double fromY = portY(from, edge.kind, edge.fromPort, true);
        const double toY = portY(to, edge.kind, edge.toPort, false);
        if (edge.kind == PortKind::Midi)
            cairo_set_source_rgb(cr, 0.86, 0.34, 0.76);
        else
            cairo_set_source_rgb(cr, 0.32, 0.62, 0.86);
        drawArrow(cr, from.x + nodeWidth, fromY, to.x, toY);
    }

    if (view.connectingFrom != static_cast<std::size_t>(-1)) {
        const auto& from = view.nodes[view.connectingFrom];
        cairo_set_source_rgb(cr, view.connectingKind == PortKind::Midi ? 0.94 : 0.74,
                             view.connectingKind == PortKind::Midi ? 0.48 : 0.80,
                             view.connectingKind == PortKind::Midi ? 0.84 : 0.94);
        cairo_set_line_width(cr, 2.0);
        const auto y = portY(from, view.connectingKind, view.connectingPort, true);
        drawArrow(cr, from.x + nodeWidth, y,
                  view.pointerX, view.pointerY);
    }

    for (const auto& node : view.nodes) {
        const bool endpoint = node.kind == NodeKind::SystemInput ||
                              node.kind == NodeKind::SystemOutput ||
                              node.kind == NodeKind::MidiInput ||
                              node.kind == NodeKind::MidiOutput;
        cairo_set_source_rgb(cr, endpoint ? 0.16 : 0.20, endpoint ? 0.34 : 0.23,
                             endpoint ? 0.28 : 0.38);
        const auto totalPorts = std::max(node.audioInputs + node.midiInputs,
                                         node.audioOutputs + node.midiOutputs);
        const auto height = std::max(
            minimumNodeHeight, static_cast<double>(totalPorts + 1) * portSpacing);
        cairo_rectangle(cr, node.x, node.y, nodeWidth, height);
        cairo_fill_preserve(cr);
        cairo_set_line_width(cr, 2.0);
        cairo_set_source_rgb(cr, endpoint ? 0.36 : 0.54, endpoint ? 0.72 : 0.58,
                             endpoint ? 0.78 : 0.90);
        cairo_stroke(cr);
        cairo_set_source_rgb(cr, 0.92, 0.94, 0.98);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 16.0);
        const bool hasAudioLabels =
            !node.audioInputLabels.empty() || !node.audioOutputLabels.empty();
        drawNodeLabel(cr, node.label, node.x + (hasAudioLabels ? 55.0 : 15.0),
                      node.y + 31.0, hasAudioLabels ? 80.0 : nodeWidth - 30.0);
        for (std::size_t port = 0; port < node.audioInputs; ++port)
            drawPort(cr, node.x, portY(node, PortKind::Audio, port, false),
                     false, PortKind::Audio);
        for (std::size_t port = 0; port < node.audioOutputs; ++port)
            drawPort(cr, node.x + nodeWidth,
                     portY(node, PortKind::Audio, port, true), true, PortKind::Audio);
        for (std::size_t port = 0; port < node.midiInputs; ++port)
            drawPort(cr, node.x, portY(node, PortKind::Midi, port, false),
                     false, PortKind::Midi);
        for (std::size_t port = 0; port < node.midiOutputs; ++port)
            drawPort(cr, node.x + nodeWidth,
                     portY(node, PortKind::Midi, port, true), true, PortKind::Midi);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 10.0);
        cairo_set_source_rgba(cr, 0.82, 0.87, 0.95, 0.9);
        for (std::size_t port = 0; port < node.audioInputLabels.size(); ++port) {
            cairo_move_to(cr, node.x + 11.0,
                          portY(node, PortKind::Audio, port, false) + 3.5);
            cairo_show_text(cr, node.audioInputLabels[port].c_str());
        }
        for (std::size_t port = 0; port < node.audioOutputLabels.size(); ++port) {
            cairo_text_extents_t extents{};
            cairo_text_extents(cr, node.audioOutputLabels[port].c_str(), &extents);
            cairo_move_to(cr, node.x + nodeWidth - 11.0 - extents.width,
                          portY(node, PortKind::Audio, port, true) + 3.5);
            cairo_show_text(cr, node.audioOutputLabels[port].c_str());
        }
    }
    return FALSE;
}

gboolean buttonPress(GtkWidget* widget, GdkEventButton* event, gpointer data) {
    auto& view = *static_cast<GraphView*>(data);
    if (event->button == GDK_BUTTON_SECONDARY) {
        const auto edge = edgeAt(view, event->x, event->y);
        if (edge != static_cast<std::size_t>(-1)) {
            stopRuntime(view, "Graph changed — press Play to compile and start audio");
            view.edges.erase(view.edges.begin() + static_cast<std::ptrdiff_t>(edge));
            gtk_widget_queue_draw(widget);
            return TRUE;
        }
        if (auto* node = nodeAt(view, event->x, event->y)) {
            showNodeMenu(widget, view, static_cast<std::size_t>(node - view.nodes.data()), event);
        } else if (portAt(view, event->x, event->y).node == static_cast<std::size_t>(-1)) {
            showAddNodeMenu(widget, view, event);
        }
        return TRUE;
    }
    if (event->button != GDK_BUTTON_PRIMARY) return FALSE;
    if (event->type == GDK_2BUTTON_PRESS) {
        if (auto* node = nodeAt(view, event->x, event->y);
            node && !node->pluginPath.empty() && view.editorHost) {
            cancelPointerInteraction(widget, view);
            openPluginEditor(view, *node);
            return TRUE;
        }
        if (auto* node = nodeAt(view, event->x, event->y);
            node && (node->kind == NodeKind::SystemInput ||
                     node->kind == NodeKind::SystemOutput)) {
            cancelPointerInteraction(widget, view);
            showSystemDialog(widget, view, node->id == "system-input");
            return TRUE;
        }
        if (auto* node = nodeAt(view, event->x, event->y);
            node && (node->kind == NodeKind::MidiInput ||
                     node->kind == NodeKind::MidiOutput)) {
            cancelPointerInteraction(widget, view);
            showMidiDialog(
                widget, view, node->kind == NodeKind::MidiInput,
                static_cast<std::size_t>(node - view.nodes.data()));
            return TRUE;
        }
    }
    const auto hit = portAt(view, event->x, event->y);
    if (hit.output) {
        view.connectingFrom = hit.node;
        view.connectingPort = hit.port;
        view.connectingKind = hit.kind;
        view.pointerX = event->x;
        view.pointerY = event->y;
        gtk_widget_queue_draw(widget);
        return TRUE;
    }
    auto* node = nodeAt(view, event->x, event->y);
    if (!node) return FALSE;
    view.dragging = static_cast<std::size_t>(node - view.nodes.data());
    view.dragX = event->x - node->x;
    view.dragY = event->y - node->y;
    gtk_widget_queue_draw(widget);
    return TRUE;
}

gboolean motion(GtkWidget* widget, GdkEventMotion* event, gpointer data) {
    auto& view = *static_cast<GraphView*>(data);
    view.pointerX = event->x;
    view.pointerY = event->y;
    if (view.connectingFrom != static_cast<std::size_t>(-1)) {
        gtk_widget_queue_draw(widget);
        return TRUE;
    }
    if (view.dragging == static_cast<std::size_t>(-1)) return FALSE;
    auto& node = view.nodes[view.dragging];
    node.x = std::max(20.0, event->x - view.dragX);
    node.y = std::max(70.0, event->y - view.dragY);
    gtk_widget_queue_draw(widget);
    return TRUE;
}

gboolean buttonRelease(GtkWidget* widget, GdkEventButton* event, gpointer data) {
    if (event->button != GDK_BUTTON_PRIMARY) return FALSE;
    auto& view = *static_cast<GraphView*>(data);
    if (view.connectingFrom != static_cast<std::size_t>(-1)) {
        const auto hit = portAt(view, event->x, event->y);
        if (!hit.output && hit.node != static_cast<std::size_t>(-1) && hit.node != view.connectingFrom &&
            hit.kind == view.connectingKind) {
            const auto duplicate = std::find_if(view.edges.begin(), view.edges.end(), [&](const auto& edge) {
                return edge.from == view.connectingFrom && edge.fromPort == view.connectingPort &&
                       edge.to == hit.node && edge.toPort == hit.port && edge.kind == view.connectingKind;
            });
            if (duplicate == view.edges.end()) {
                stopRuntime(view, "Graph changed — press Play to compile and start audio");
                view.edges.push_back({view.connectingFrom, hit.node, view.connectingPort, hit.port,
                                      view.connectingKind});
            }
        }
        view.connectingFrom = static_cast<std::size_t>(-1);
        gtk_widget_queue_draw(widget);
        return TRUE;
    }
    view.dragging = static_cast<std::size_t>(-1);
    return TRUE;
}

void updateWindowTitle(GraphView& view) {
    std::string title = "Transmission — Graph";
    if (!view.filePath.empty())
        title = "Transmission — " + std::filesystem::path(view.filePath).filename().string();
    gtk_window_set_title(GTK_WINDOW(view.window), title.c_str());
}

transmission::UiProject captureProject(const GraphView& view) {
    transmission::UiProject project;
    project.nodes.reserve(view.nodes.size());
    for (const auto& node : view.nodes) {
        const auto kind = static_cast<transmission::UiProjectNodeKind>(
            static_cast<int>(node.kind));
        project.nodes.push_back({
            node.id, node.label, kind,
            node.audioInputs, node.audioOutputs, node.midiInputs, node.midiOutputs,
            node.x, node.y, node.pluginPath, node.externalPort, {}, {}, {}, node.gainDb});
        auto& target = project.nodes.back();
        const auto parameters = view.parameterValues.find(node.id);
        if (parameters != view.parameterValues.end()) {
            target.parameters.reserve(parameters->second.size());
            for (const auto& [id, value] : parameters->second)
                target.parameters.push_back({id, value});
            std::sort(
                target.parameters.begin(), target.parameters.end(),
                [](const auto& left, const auto& right) {
                    return left.id < right.id;
                });
        }
        const auto state = view.pluginStates.find(node.id);
        if (state != view.pluginStates.end()) {
            target.componentState = state->second.component;
            target.controllerState = state->second.controller;
        }
    }
    project.connections.reserve(view.edges.size());
    for (const auto& edge : view.edges) {
        if (edge.from >= view.nodes.size() || edge.to >= view.nodes.size()) continue;
        project.connections.push_back({
            view.nodes[edge.from].id, view.nodes[edge.to].id,
            edge.kind == PortKind::Audio
                ? transmission::UiProjectConnectionKind::Audio
                : transmission::UiProjectConnectionKind::Midi,
            edge.fromPort, edge.toPort});
    }
    project.systemInputConnections = view.systemInputConnections;
    project.systemOutputConnections = view.systemOutputConnections;
    project.tempo = gtk_spin_button_get_value(view.tempo);
    project.loopBars = gtk_spin_button_get_value(view.loopBars);
    project.loopEnabled = gtk_toggle_button_get_active(view.loop);
    project.arrangementLengthBeats = view.arrangementLengthBeats;
    project.midiClips = view.midiClips;
    project.gainLanes = view.gainLanes;
    return project;
}

transmission::UiProject defaultProject() {
    transmission::UiProject project;
    project.nodes = {
        {"system-input", "System Input", transmission::UiProjectNodeKind::SystemInput,
         0, 2, 0, 1, 60.0, 150.0, "", "", {}, {}, {}},
        {"gain", "AGain / VST3", transmission::UiProjectNodeKind::PassThrough,
         2, 2, 1, 1, 340.0, 150.0, "", "", {}, {}, {}},
        {"system-output", "System Output", transmission::UiProjectNodeKind::SystemOutput,
         2, 0, 1, 0, 580.0, 150.0, "", "", {}, {}, {}}
    };
    project.connections = {
        {"system-input", "gain", transmission::UiProjectConnectionKind::Audio, 0, 0},
        {"gain", "system-output", transmission::UiProjectConnectionKind::Audio, 0, 0},
        {"system-input", "gain", transmission::UiProjectConnectionKind::Midi, 0, 0},
        {"gain", "system-output", transmission::UiProjectConnectionKind::Midi, 0, 0}
    };
    return project;
}

bool validateProject(const transmission::UiProject& project, std::string& error) {
    if (project.nodes.empty()) {
        error = "The project contains no nodes";
        return false;
    }
    std::unordered_map<std::string, const transmission::UiProjectNode*> nodes;
    for (const auto& node : project.nodes) {
        if (node.id.empty() || !nodes.emplace(node.id, &node).second) {
            error = "The project contains an empty or duplicate node identifier";
            return false;
        }
    }
    for (const auto& connection : project.connections) {
        const auto from = nodes.find(connection.from);
        const auto to = nodes.find(connection.to);
        if (from == nodes.end() || to == nodes.end()) {
            error = "A project connection refers to a missing node";
            return false;
        }
        const bool audio =
            connection.kind == transmission::UiProjectConnectionKind::Audio;
        const auto outputCount =
            audio ? from->second->audioOutputs : from->second->midiOutputs;
        const auto inputCount =
            audio ? to->second->audioInputs : to->second->midiInputs;
        if (connection.fromPort >= outputCount || connection.toPort >= inputCount) {
            error = "A project connection refers to a missing port";
            return false;
        }
    }
    std::unordered_set<std::string> clipIds;
    for (const auto& clip : project.midiClips) {
        if (!nodes.contains(clip.targetNodeId) || !clipIds.emplace(clip.id).second ||
            clip.startBeat + clip.lengthBeats > project.arrangementLengthBeats) {
            error = "The arrangement contains an invalid MIDI clip";
            return false;
        }
    }
    for (const auto& lane : project.gainLanes) {
        const auto target = nodes.find(lane.targetNodeId);
        if (target == nodes.end() || target->second->kind != transmission::UiProjectNodeKind::Gain) {
            error = "The arrangement contains an invalid gain lane";
            return false;
        }
    }
    return true;
}

bool applyProject(GraphView& view, const transmission::UiProject& project,
                  std::string& error) {
    auto normalized = project;
    std::unordered_map<std::string, std::vector<std::string>> inputLabels;
    std::unordered_map<std::string, std::vector<std::string>> outputLabels;
    for (auto& node : normalized.nodes) {
        if (node.kind != transmission::UiProjectNodeKind::Plugin ||
            node.pluginPath.empty()) continue;
        transmission::Vst3PluginTopology topology;
        if (!transmission::Vst3Inspector().inspectTopology(
                node.pluginPath, topology, error)) {
            error = "Unable to inspect " + node.pluginPath + ": " + error;
            return false;
        }
        node.audioInputs = topology.audioInputs.size();
        node.audioOutputs = topology.audioOutputs.size();
        node.midiInputs = topology.midiInputs;
        node.midiOutputs = topology.midiOutputs;
        if (!topology.name.empty()) node.label = topology.name;
        for (const auto& port : topology.audioInputs)
            inputLabels[node.id].push_back(port.name);
        for (const auto& port : topology.audioOutputs)
            outputLabels[node.id].push_back(port.name);
    }
    if (!validateProject(normalized, error)) return false;
    std::unordered_map<std::string, std::size_t> indices;
    std::vector<Node> nodes;
    nodes.reserve(normalized.nodes.size());
    for (const auto& source : normalized.nodes) {
        indices.emplace(source.id, nodes.size());
        const auto kind = static_cast<NodeKind>(static_cast<int>(source.kind));
        const auto label = kind == NodeKind::MidiOutput
            ? midiOutputLabel(source.externalPort) : source.label;
        nodes.push_back({
            source.id, label, kind,
            source.audioInputs, source.audioOutputs, source.midiInputs, source.midiOutputs,
            source.x, source.y, source.pluginPath, source.externalPort,
            inputLabels[source.id], outputLabels[source.id], source.gainDb});
    }
    std::vector<Edge> edges;
    edges.reserve(normalized.connections.size());
    for (const auto& source : normalized.connections) {
        edges.push_back({
            indices.at(source.from), indices.at(source.to), source.fromPort, source.toPort,
            source.kind == transmission::UiProjectConnectionKind::Audio
                ? PortKind::Audio : PortKind::Midi});
    }

    stopRuntime(view);
    view.nodes = std::move(nodes);
    view.edges = std::move(edges);
    view.parameterValues.clear();
    view.pluginStates.clear();
    for (const auto& source : normalized.nodes) {
        if (!source.parameters.empty()) {
            auto& parameters = view.parameterValues[source.id];
            for (const auto& parameter : source.parameters)
                parameters[parameter.id] = parameter.normalizedValue;
        }
        if (!source.componentState.empty() ||
            !source.controllerState.empty())
            view.pluginStates[source.id] = {
                source.componentState, source.controllerState};
    }
    view.systemInputConnections = normalized.systemInputConnections;
    view.systemOutputConnections = normalized.systemOutputConnections;
    view.arrangementLengthBeats = normalized.arrangementLengthBeats;
    view.midiClips = normalized.midiClips;
    view.gainLanes = normalized.gainLanes;
    view.dragging = static_cast<std::size_t>(-1);
    view.connectingFrom = static_cast<std::size_t>(-1);
    view.nextPluginId = 1;
    view.nextMidiInputId = 1;
    view.nextMidiOutputId = 1;
    std::unordered_set<std::string> identifiers;
    for (const auto& node : view.nodes) identifiers.insert(node.id);
    while (identifiers.contains("plugin-" + std::to_string(view.nextPluginId)))
        ++view.nextPluginId;
    while (identifiers.contains("midi-input-" + std::to_string(view.nextMidiInputId)))
        ++view.nextMidiInputId;
    while (identifiers.contains("midi-output-" + std::to_string(view.nextMidiOutputId)))
        ++view.nextMidiOutputId;
    gtk_spin_button_set_value(view.tempo, normalized.tempo);
    gtk_spin_button_set_value(view.loopBars, normalized.loopBars);
    gtk_toggle_button_set_active(view.loop, normalized.loopEnabled);
    updateTransportDisplay(view);
    gtk_widget_queue_draw(view.canvas);
    return true;
}

bool runProjectHelper(const GraphView& view, const char* command,
                      const std::string& path, const std::string& input,
                      std::string& output, std::string& error) {
    gchar* interchangePath = nullptr;
    if (!input.empty()) {
        GError* temporaryError = nullptr;
        const auto descriptor =
            g_file_open_tmp("transmission-project-XXXXXX", &interchangePath, &temporaryError);
        if (descriptor < 0) {
            error = temporaryError ? temporaryError->message
                                   : "Unable to create project interchange file";
            g_clear_error(&temporaryError);
            return false;
        }
        close(descriptor);
        if (!g_file_set_contents(interchangePath, input.data(),
                                 static_cast<gssize>(input.size()), &temporaryError)) {
            error = temporaryError ? temporaryError->message
                                   : "Unable to write project interchange file";
            g_clear_error(&temporaryError);
            g_unlink(interchangePath);
            g_free(interchangePath);
            return false;
        }
    }
    GError* subprocessError = nullptr;
    auto* process = g_subprocess_new(
        static_cast<GSubprocessFlags>(G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                                      G_SUBPROCESS_FLAGS_STDERR_PIPE),
        &subprocessError, "node", view.projectHelperPath.c_str(), command,
        path.c_str(), interchangePath, nullptr);
    if (!process) {
        error = subprocessError ? subprocessError->message : "Unable to start project helper";
        g_clear_error(&subprocessError);
        if (interchangePath) {
            g_unlink(interchangePath);
            g_free(interchangePath);
        }
        return false;
    }
    gchar* standardOutput = nullptr;
    gchar* standardError = nullptr;
    const gboolean communicated = g_subprocess_communicate_utf8(
        process, nullptr, nullptr, &standardOutput, &standardError, &subprocessError);
    if (standardOutput) output = standardOutput;
    if (!communicated || !g_subprocess_get_successful(process)) {
        error = standardError && *standardError
            ? standardError
            : subprocessError ? subprocessError->message : "Project helper failed";
        while (!error.empty() && (error.back() == '\n' || error.back() == '\r'))
            error.pop_back();
    }
    g_free(standardOutput);
    g_free(standardError);
    g_clear_error(&subprocessError);
    g_object_unref(process);
    if (interchangePath) {
        g_unlink(interchangePath);
        g_free(interchangePath);
    }
    return communicated && error.empty();
}

bool saveProject(GraphView& view, const std::string& path) {
    if (runtimeRunning(view))
        stopRuntime(view, "Audio stopped to capture plugin state");
    const auto snapshot = transmission::encodeUiProject(captureProject(view));
    std::string output;
    std::string error;
    if (!runProjectHelper(view, "save", path, snapshot,
                          output, error)) {
        setStatus(view, "Unable to save project: " + error, true);
        return false;
    }
    view.filePath = path;
    view.lastSavedSnapshot = snapshot;
    updateWindowTitle(view);
    return true;
}

bool saveProjectAs(GraphView& view) {
    bool saved = false;
    auto* dialog = gtk_file_chooser_dialog_new(
        "Save Transmission Project", GTK_WINDOW(view.window),
        GTK_FILE_CHOOSER_ACTION_SAVE, "_Cancel", GTK_RESPONSE_CANCEL,
        "_Save", GTK_RESPONSE_ACCEPT, nullptr);
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), "transmission.ttl");
    auto* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "RDF Turtle projects (*.ttl)");
    gtk_file_filter_add_pattern(filter, "*.ttl");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        gchar* selected = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        std::string path = selected ? selected : "";
        g_free(selected);
        if (!path.ends_with(".ttl")) path += ".ttl";
        saved = saveProject(view, path);
    }
    gtk_widget_destroy(dialog);
    return saved;
}

void newProjectActivated(GtkMenuItem*, gpointer data) {
    auto& view = *static_cast<GraphView*>(data);
    std::string error;
    if (!applyProject(view, defaultProject(), error)) {
        setStatus(view, error, true);
        return;
    }
    view.filePath.clear();
    view.lastSavedSnapshot.clear();
    updateWindowTitle(view);
}

void openProjectActivated(GtkMenuItem*, gpointer data) {
    auto& view = *static_cast<GraphView*>(data);
    auto* dialog = gtk_file_chooser_dialog_new(
        "Open Transmission Project", GTK_WINDOW(view.window),
        GTK_FILE_CHOOSER_ACTION_OPEN, "_Cancel", GTK_RESPONSE_CANCEL,
        "_Open", GTK_RESPONSE_ACCEPT, nullptr);
    auto* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "RDF Turtle projects (*.ttl)");
    gtk_file_filter_add_pattern(filter, "*.ttl");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        gchar* selected = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        const std::string path = selected ? selected : "";
        g_free(selected);
        std::string interchange;
        std::string error;
        if (!runProjectHelper(view, "load", path, "", interchange, error)) {
            setStatus(view, "Unable to open project: " + error, true);
        } else {
            transmission::UiProject project;
            if (!transmission::decodeUiProject(interchange, project, error) ||
                !applyProject(view, project, error)) {
                setStatus(view, "Unable to open project: " + error, true);
            } else {
                view.filePath = path;
                view.lastSavedSnapshot =
                    transmission::encodeUiProject(captureProject(view));
                updateWindowTitle(view);
            }
        }
    }
    gtk_widget_destroy(dialog);
}

void saveProjectActivated(GtkMenuItem*, gpointer data) {
    auto& view = *static_cast<GraphView*>(data);
    if (view.filePath.empty())
        saveProjectAs(view);
    else
        saveProject(view, view.filePath);
}

void saveProjectAsActivated(GtkMenuItem*, gpointer data) {
    saveProjectAs(*static_cast<GraphView*>(data));
}

struct RenderCompletion {
    GraphView* view = nullptr;
    bool success = false;
    std::string outputPath;
    std::string error;
    transmission::OfflineRenderResult result;
};

gboolean renderProgressTick(gpointer data) {
    auto& view = *static_cast<GraphView*>(data);
    if (!view.renderInProgress || !view.renderProgressBar) {
        view.renderProgressTimer = 0;
        return G_SOURCE_REMOVE;
    }
    gtk_progress_bar_set_fraction(
        view.renderProgressBar,
        std::clamp(view.renderProgress.load(std::memory_order_acquire),
                   0.0, 1.0));
    return G_SOURCE_CONTINUE;
}

void renderDialogResponse(GtkDialog* dialog, gint response, gpointer data) {
    if (response != GTK_RESPONSE_CANCEL &&
        response != GTK_RESPONSE_DELETE_EVENT)
        return;
    auto& view = *static_cast<GraphView*>(data);
    view.renderCancel.store(true, std::memory_order_release);
    gtk_window_set_title(GTK_WINDOW(dialog), "Cancelling Audio Render");
    gtk_dialog_set_response_sensitive(
        dialog, GTK_RESPONSE_CANCEL, FALSE);
}

void showRenderComplete(GraphView& view, const RenderCompletion& completion) {
    const auto type = completion.success
        ? GTK_MESSAGE_INFO : GTK_MESSAGE_ERROR;
    const auto message = completion.success
        ? "Audio render complete" : "Unable to render audio";
    auto* dialog = gtk_message_dialog_new(
        GTK_WINDOW(view.window),
        static_cast<GtkDialogFlags>(
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        type, GTK_BUTTONS_CLOSE, "%s", message);
    if (completion.success) {
        const auto detail =
            completion.outputPath + "\n" +
            std::to_string(completion.result.framesWritten) +
            " frames; peak " + std::to_string(completion.result.peak);
        gtk_message_dialog_format_secondary_text(
            GTK_MESSAGE_DIALOG(dialog), "%s", detail.c_str());
    } else {
        gtk_message_dialog_format_secondary_text(
            GTK_MESSAGE_DIALOG(dialog), "%s", completion.error.c_str());
    }
    g_signal_connect(
        dialog, "response",
        G_CALLBACK(+[](GtkDialog* current, gint, gpointer) {
            gtk_widget_destroy(GTK_WIDGET(current));
        }),
        nullptr);
    gtk_widget_show(dialog);
}

gboolean renderCompleted(gpointer data) {
    auto& completion = *static_cast<RenderCompletion*>(data);
    auto& view = *completion.view;
    {
        std::lock_guard lock(view.renderCompletionMutex);
        view.renderCompletionSource = 0;
    }
    if (view.renderThread.joinable()) view.renderThread.join();
    if (view.renderProgressTimer) {
        g_source_remove(view.renderProgressTimer);
        view.renderProgressTimer = 0;
    }
    if (view.renderDialog) {
        gtk_widget_destroy(view.renderDialog);
        view.renderDialog = nullptr;
    }
    view.renderProgressBar = nullptr;
    view.renderInProgress = false;
    showRenderComplete(view, completion);
    return G_SOURCE_REMOVE;
}

bool encodeMp3(const std::string& wavePath, const std::string& outputPath,
               std::string& error) {
    const auto temporaryPath = outputPath + ".transmission-part.mp3";
    GError* processError = nullptr;
    auto* process = g_subprocess_new(
        static_cast<GSubprocessFlags>(
            G_SUBPROCESS_FLAGS_STDOUT_SILENCE |
            G_SUBPROCESS_FLAGS_STDERR_PIPE),
        &processError, "ffmpeg", "-y", "-loglevel", "error",
        "-i", wavePath.c_str(), "-codec:a", "libmp3lame",
        "-q:a", "2", temporaryPath.c_str(), nullptr);
    if (!process) {
        error = processError
            ? processError->message
            : "Unable to start ffmpeg";
        g_clear_error(&processError);
        return false;
    }
    gchar* standardError = nullptr;
    const auto communicated = g_subprocess_communicate_utf8(
        process, nullptr, nullptr, nullptr, &standardError, &processError);
    const auto successful =
        communicated && g_subprocess_get_successful(process);
    if (!successful) {
        error = standardError && *standardError
            ? standardError
            : processError ? processError->message
                           : "ffmpeg was unable to encode the MP3 file";
        while (!error.empty() &&
               (error.back() == '\n' || error.back() == '\r'))
            error.pop_back();
    }
    g_free(standardError);
    g_clear_error(&processError);
    g_object_unref(process);
    if (!successful) {
        std::error_code ignored;
        std::filesystem::remove(temporaryPath, ignored);
        return false;
    }
    std::error_code fileError;
    std::filesystem::rename(temporaryPath, outputPath, fileError);
    if (fileError) {
        error = "Unable to replace the MP3 output file: " +
                fileError.message();
        std::filesystem::remove(temporaryPath, fileError);
        return false;
    }
    return true;
}

void beginAudioRender(GraphView& view, std::string outputPath, bool mp3,
                      double bars, double sampleRate) {
    if (view.renderInProgress) return;
    if (runtimeRunning(view))
        stopRuntime(view, "Audio stopped for offline rendering");
    const auto snapshot = runtimeSnapshot(view);
    const auto tempo = gtk_spin_button_get_value(view.tempo);
    transmission::OfflineRenderOptions options;
    options.outputPath = outputPath;
    options.channels = 2;
    options.blockSize = 1024;
    options.sampleRate = sampleRate;
    options.totalFrames = static_cast<std::uint64_t>(std::ceil(
        bars * 4.0 * 60.0 * sampleRate / tempo));
    options.tempo = tempo;
    options.loopStartBeat = 0.0;
    options.loopEndBeat =
        std::max(1.0, gtk_spin_button_get_value(view.loopBars)) * 4.0;
    options.loopEnabled =
        view.loop && gtk_toggle_button_get_active(view.loop);
    options.processingThreads = view.processingThreads;

    view.renderCancel.store(false, std::memory_order_release);
    view.renderProgress.store(0.0, std::memory_order_release);
    view.renderInProgress = true;
    auto* dialog = gtk_dialog_new_with_buttons(
        "Rendering Audio", GTK_WINDOW(view.window),
        static_cast<GtkDialogFlags>(
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        "_Cancel", GTK_RESPONSE_CANCEL, nullptr);
    auto* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 16);
    gtk_box_set_spacing(GTK_BOX(content), 10);
    auto* label = gtk_label_new(
        mp3 ? "Rendering WAV source and encoding MP3…"
            : "Rendering WAV…");
    auto* progress = GTK_PROGRESS_BAR(gtk_progress_bar_new());
    gtk_widget_set_size_request(GTK_WIDGET(progress), 380, -1);
    gtk_progress_bar_set_show_text(progress, TRUE);
    gtk_box_pack_start(GTK_BOX(content), label, FALSE, FALSE, 0);
    gtk_box_pack_start(
        GTK_BOX(content), GTK_WIDGET(progress), FALSE, FALSE, 0);
    view.renderDialog = dialog;
    view.renderProgressBar = progress;
    g_signal_connect(
        dialog, "response", G_CALLBACK(renderDialogResponse), &view);
    gtk_widget_show_all(dialog);
    view.renderProgressTimer = g_timeout_add(50, renderProgressTick, &view);

    view.renderThread = std::thread(
        [&view, snapshot, options, outputPath = std::move(outputPath), mp3]() mutable {
            auto completion = std::make_unique<RenderCompletion>();
            completion->view = &view;
            completion->outputPath = outputPath;
            auto renderOptions = options;
            std::string temporaryWave;
            if (mp3) {
                temporaryWave =
                    (std::filesystem::temp_directory_path() /
                     ("transmission-render-" +
                      std::to_string(
                          std::chrono::steady_clock::now()
                              .time_since_epoch().count()) +
                      ".wav")).string();
                renderOptions.outputPath = temporaryWave;
            }
            transmission::OfflineAudioRenderer renderer(
                uiProcessorFactory());
            completion->success = renderer.renderWave(
                snapshot, renderOptions, completion->result,
                completion->error,
                [&view, mp3](double progressValue) {
                    view.renderProgress.store(
                        mp3 ? progressValue * 0.9 : progressValue,
                        std::memory_order_release);
                    return !view.renderCancel.load(
                        std::memory_order_acquire);
                });
            if (completion->success && mp3) {
                if (view.renderCancel.load(std::memory_order_acquire)) {
                    completion->success = false;
                    completion->error = "Audio rendering was cancelled";
                } else {
                    completion->success =
                        encodeMp3(
                            temporaryWave, outputPath,
                            completion->error);
                }
            }
            if (!temporaryWave.empty()) {
                std::error_code ignored;
                std::filesystem::remove(temporaryWave, ignored);
            }
            if (completion->success)
                view.renderProgress.store(1.0, std::memory_order_release);
            {
                std::lock_guard lock(view.renderCompletionMutex);
                view.renderCompletionSource = g_idle_add_full(
                    G_PRIORITY_DEFAULT, renderCompleted,
                    completion.release(), +[](gpointer data) {
                        delete static_cast<RenderCompletion*>(data);
                    });
            }
        });
}

void renderAudioActivated(GtkMenuItem*, gpointer data) {
    auto& view = *static_cast<GraphView*>(data);
    if (view.renderInProgress) {
        setStatus(view, "An audio render is already in progress", true);
        return;
    }
    auto* dialog = gtk_file_chooser_dialog_new(
        "Render Transmission Audio", GTK_WINDOW(view.window),
        GTK_FILE_CHOOSER_ACTION_SAVE, "_Cancel", GTK_RESPONSE_CANCEL,
        "_Render", GTK_RESPONSE_ACCEPT, nullptr);
    gtk_file_chooser_set_do_overwrite_confirmation(
        GTK_FILE_CHOOSER(dialog), TRUE);
    gtk_file_chooser_set_current_name(
        GTK_FILE_CHOOSER(dialog), "transmission.wav");
    auto* waveFilter = gtk_file_filter_new();
    gtk_file_filter_set_name(waveFilter, "WAV audio (*.wav)");
    gtk_file_filter_add_pattern(waveFilter, "*.wav");
    auto* mp3Filter = gtk_file_filter_new();
    gtk_file_filter_set_name(mp3Filter, "MP3 audio (*.mp3)");
    gtk_file_filter_add_pattern(mp3Filter, "*.mp3");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), waveFilter);
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), mp3Filter);

    auto* optionsGrid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(optionsGrid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(optionsGrid), 10);
    auto* barsLabel = gtk_label_new("Length (bars)");
    auto* bars = GTK_SPIN_BUTTON(
        gtk_spin_button_new_with_range(1.0, 4096.0, 1.0));
    gtk_spin_button_set_value(
        bars, view.arrangementLengthBeats > 0.0
            ? view.arrangementLengthBeats / 4.0
            : std::max(1.0, gtk_spin_button_get_value(view.loopBars)));
    auto* rateLabel = gtk_label_new("Sample rate");
    auto* rate = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
    gtk_combo_box_text_append_text(rate, "44,100 Hz");
    gtk_combo_box_text_append_text(rate, "48,000 Hz");
    gtk_combo_box_text_append_text(rate, "96,000 Hz");
    gtk_combo_box_set_active(GTK_COMBO_BOX(rate), 1);
    gtk_grid_attach(GTK_GRID(optionsGrid), barsLabel, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(optionsGrid), GTK_WIDGET(bars), 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(optionsGrid), rateLabel, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(optionsGrid), GTK_WIDGET(rate), 1, 1, 1, 1);
    gtk_widget_show_all(optionsGrid);
    gtk_file_chooser_set_extra_widget(
        GTK_FILE_CHOOSER(dialog), optionsGrid);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        gchar* selected =
            gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        std::filesystem::path path = selected ? selected : "";
        g_free(selected);
        const auto selectedFilter =
            gtk_file_chooser_get_filter(GTK_FILE_CHOOSER(dialog));
        auto extension = path.extension().string();
        std::transform(
            extension.begin(), extension.end(), extension.begin(),
            [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
        const bool mp3 =
            selectedFilter == mp3Filter || extension == ".mp3";
        path.replace_extension(mp3 ? ".mp3" : ".wav");
        constexpr std::array<double, 3> rates{
            44100.0, 48000.0, 96000.0};
        const auto selectedRate =
            gtk_combo_box_get_active(GTK_COMBO_BOX(rate));
        beginAudioRender(
            view, path.string(), mp3,
            gtk_spin_button_get_value(bars),
            rates[selectedRate >= 0 &&
                          static_cast<std::size_t>(selectedRate) < rates.size()
                      ? static_cast<std::size_t>(selectedRate)
                      : 1]);
    }
    gtk_widget_destroy(dialog);
}

void quitActivated(GtkMenuItem*, gpointer data) {
    gtk_window_close(GTK_WINDOW(static_cast<GraphView*>(data)->window));
}

gboolean confirmClose(GtkWidget*, GdkEvent*, gpointer data) {
    auto& view = *static_cast<GraphView*>(data);
    if (view.renderInProgress) {
        view.renderCancel.store(true, std::memory_order_release);
        if (view.renderDialog)
            gtk_window_set_title(
                GTK_WINDOW(view.renderDialog), "Cancelling Audio Render");
        return TRUE;
    }
    if (runtimeRunning(view))
        stopRuntime(view, "Audio stopped to capture plugin state");
    const auto current = transmission::encodeUiProject(captureProject(view));
    if (!view.lastSavedSnapshot.empty() && current == view.lastSavedSnapshot)
        return FALSE;

    const auto name = view.filePath.empty()
        ? std::string("this project")
        : std::filesystem::path(view.filePath).filename().string();
    auto* dialog = gtk_message_dialog_new(
        GTK_WINDOW(view.window),
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        GTK_MESSAGE_WARNING, GTK_BUTTONS_NONE,
        "Save changes to %s before closing?", name.c_str());
    gtk_message_dialog_format_secondary_text(
        GTK_MESSAGE_DIALOG(dialog),
        "Unsaved changes will be lost if you close without saving.");
    gtk_dialog_add_button(GTK_DIALOG(dialog), "_Cancel", GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(dialog), "_Discard", GTK_RESPONSE_REJECT);
    gtk_dialog_add_button(
        GTK_DIALOG(dialog), view.filePath.empty() ? "_Save As…" : "_Save",
        GTK_RESPONSE_ACCEPT);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
    const auto response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    if (response == GTK_RESPONSE_REJECT) return FALSE;
    if (response != GTK_RESPONSE_ACCEPT) return TRUE;
    const bool saved = view.filePath.empty()
        ? saveProjectAs(view) : saveProject(view, view.filePath);
    return saved ? FALSE : TRUE;
}

void activate(GtkApplication* application, gpointer) {
    auto* view = new GraphView();
    view->jackConnections = std::make_unique<transmission::JackConnectionManager>();
    view->editorHost = std::make_unique<transmission::Vst3EditorHost>();
#if defined(TRANSMISSION_UI_WITH_JACK) && defined(TRANSMISSION_UI_WITH_VST3)
    view->jackDevice = std::make_unique<transmission::JackAudioDevice>();
    view->runtime = std::make_unique<transmission::GraphRuntimeController>(
        uiProcessorFactory());
#endif
    const char* home = std::getenv("HOME");
    const auto pluginRoot = std::filesystem::path(home ? home : ".") / ".vst3";
    if (std::filesystem::is_directory(pluginRoot)) {
        for (const auto& entry : std::filesystem::directory_iterator(pluginRoot)) {
            if (entry.is_directory() && entry.path().extension() == ".vst3")
                view->pluginPaths.push_back(entry.path().string());
        }
        std::sort(view->pluginPaths.begin(), view->pluginPaths.end());
    }
    GtkWidget* window = gtk_application_window_new(application);
    gtk_window_set_title(GTK_WINDOW(window), "Transmission — Graph");
    gtk_window_set_default_size(GTK_WINDOW(window), 900, 520);

    GtkWidget* frame = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    auto* menuBar = gtk_menu_bar_new();
    auto* fileItem = gtk_menu_item_new_with_mnemonic("_File");
    auto* fileMenu = gtk_menu_new();
    auto* newItem = gtk_menu_item_new_with_mnemonic("_New");
    auto* openItem = gtk_menu_item_new_with_mnemonic("_Open…");
    auto* saveItem = gtk_menu_item_new_with_mnemonic("_Save");
    auto* saveAsItem = gtk_menu_item_new_with_mnemonic("Save _As…");
    auto* renderItem =
        gtk_menu_item_new_with_mnemonic("_Render Audio…");
    auto* separator = gtk_separator_menu_item_new();
    auto* quitItem = gtk_menu_item_new_with_mnemonic("_Quit");
    gtk_menu_shell_append(GTK_MENU_SHELL(fileMenu), newItem);
    gtk_menu_shell_append(GTK_MENU_SHELL(fileMenu), openItem);
    gtk_menu_shell_append(GTK_MENU_SHELL(fileMenu), saveItem);
    gtk_menu_shell_append(GTK_MENU_SHELL(fileMenu), saveAsItem);
    gtk_menu_shell_append(GTK_MENU_SHELL(fileMenu), renderItem);
    gtk_menu_shell_append(GTK_MENU_SHELL(fileMenu), separator);
    gtk_menu_shell_append(GTK_MENU_SHELL(fileMenu), quitItem);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(fileItem), fileMenu);
    gtk_menu_shell_append(GTK_MENU_SHELL(menuBar), fileItem);
    auto* settingsItem = gtk_menu_item_new_with_mnemonic("_Settings");
    auto* settingsMenu = gtk_menu_new();
    auto* audioSettingsItem =
        gtk_menu_item_new_with_mnemonic("_Audio…");
    gtk_menu_shell_append(
        GTK_MENU_SHELL(settingsMenu), audioSettingsItem);
    gtk_menu_item_set_submenu(
        GTK_MENU_ITEM(settingsItem), settingsMenu);
    gtk_menu_shell_append(GTK_MENU_SHELL(menuBar), settingsItem);
    gtk_box_pack_start(GTK_BOX(frame), menuBar, FALSE, FALSE, 0);

    auto* accelerators = gtk_accel_group_new();
    gtk_window_add_accel_group(GTK_WINDOW(window), accelerators);
    gtk_widget_add_accelerator(newItem, "activate", accelerators, GDK_KEY_n,
                               GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    gtk_widget_add_accelerator(openItem, "activate", accelerators, GDK_KEY_o,
                               GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    gtk_widget_add_accelerator(saveItem, "activate", accelerators, GDK_KEY_s,
                               GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    gtk_widget_add_accelerator(
        saveAsItem, "activate", accelerators, GDK_KEY_s,
        static_cast<GdkModifierType>(GDK_CONTROL_MASK | GDK_SHIFT_MASK),
        GTK_ACCEL_VISIBLE);
    gtk_widget_add_accelerator(
        renderItem, "activate", accelerators, GDK_KEY_r,
        static_cast<GdkModifierType>(GDK_CONTROL_MASK | GDK_SHIFT_MASK),
        GTK_ACCEL_VISIBLE);
    gtk_widget_add_accelerator(quitItem, "activate", accelerators, GDK_KEY_q,
                               GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);

    GtkWidget* transportBar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_margin_start(transportBar, 8);
    gtk_widget_set_margin_end(transportBar, 8);
    gtk_widget_set_margin_top(transportBar, 8);
    gtk_widget_set_margin_bottom(transportBar, 8);
    auto* playButton = gtk_button_new_with_label("Play");
    gtk_widget_set_size_request(playButton, 70, -1);
    auto* resetButton = gtk_button_new_with_label("Reset");
    gtk_box_pack_start(GTK_BOX(transportBar), playButton, FALSE, FALSE, 4);
    gtk_box_pack_start(GTK_BOX(transportBar), resetButton, FALSE, FALSE, 4);
    GtkWidget* tempoLabel = gtk_label_new("Tempo");
    gtk_widget_set_margin_start(tempoLabel, 12);
    gtk_box_pack_start(GTK_BOX(transportBar), tempoLabel, FALSE, FALSE, 4);
    auto* tempo = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(20.0, 300.0, 1.0));
    gtk_spin_button_set_value(tempo, 120.0);
    gtk_spin_button_set_numeric(tempo, TRUE);
    gtk_box_pack_start(GTK_BOX(transportBar), GTK_WIDGET(tempo), FALSE, FALSE, 4);
    GtkWidget* loop = gtk_check_button_new_with_label("Loop");
    gtk_widget_set_margin_start(loop, 12);
    gtk_box_pack_start(GTK_BOX(transportBar), loop, FALSE, FALSE, 4);
    GtkWidget* loopLabel = gtk_label_new("Length (bars)");
    gtk_widget_set_margin_start(loopLabel, 4);
    gtk_box_pack_start(GTK_BOX(transportBar), loopLabel, FALSE, FALSE, 4);
    auto* loopBars = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(1.0, 64.0, 1.0));
    gtk_spin_button_set_value(loopBars, 4.0);
    gtk_spin_button_set_numeric(loopBars, TRUE);
    gtk_box_pack_start(GTK_BOX(transportBar), GTK_WIDGET(loopBars), FALSE, FALSE, 4);
    view->tempo = tempo;
    view->loopBars = loopBars;
    view->loop = GTK_TOGGLE_BUTTON(loop);
    view->window = window;
    view->playButton = playButton;

    GtkWidget* canvas = gtk_drawing_area_new();
    view->canvas = canvas;
    if (const char* root = std::getenv("TRANSMISSION_ROOT"))
        view->projectHelperPath =
            (std::filesystem::path(root) / "scripts/native-ui-project.js").string();
    else
        view->projectHelperPath =
            (std::filesystem::current_path() / "scripts/native-ui-project.js").string();

    loopChanged(nullptr, view);
    g_signal_connect(playButton, "clicked", G_CALLBACK(playStopClicked), view);
    g_signal_connect(resetButton, "clicked", G_CALLBACK(resetTransportClicked), view);
    g_signal_connect(tempo, "value-changed", G_CALLBACK(tempoChanged), view);
    g_signal_connect(loop, "toggled", G_CALLBACK(loopChanged), view);
    g_signal_connect(loopBars, "value-changed", G_CALLBACK(loopChanged), view);
    g_signal_connect(newItem, "activate", G_CALLBACK(newProjectActivated), view);
    g_signal_connect(openItem, "activate", G_CALLBACK(openProjectActivated), view);
    g_signal_connect(saveItem, "activate", G_CALLBACK(saveProjectActivated), view);
    g_signal_connect(saveAsItem, "activate", G_CALLBACK(saveProjectAsActivated), view);
    g_signal_connect(renderItem, "activate", G_CALLBACK(renderAudioActivated), view);
    g_signal_connect(audioSettingsItem, "activate",
                     G_CALLBACK(audioSettingsActivated), view);
    g_signal_connect(quitItem, "activate", G_CALLBACK(quitActivated), view);
    gtk_widget_set_hexpand(canvas, TRUE);
    gtk_widget_set_vexpand(canvas, TRUE);
    gtk_widget_set_size_request(canvas, 800, 400);
    gtk_widget_add_events(canvas, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK);
    g_signal_connect(canvas, "draw", G_CALLBACK(drawGraph), view);
    g_signal_connect(canvas, "button-press-event", G_CALLBACK(buttonPress), view);
    g_signal_connect(canvas, "motion-notify-event", G_CALLBACK(motion), view);
    g_signal_connect(canvas, "button-release-event", G_CALLBACK(buttonRelease), view);
    gtk_box_pack_start(GTK_BOX(frame), canvas, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(frame), transportBar, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(window), frame);
    g_signal_connect(window, "delete-event", G_CALLBACK(confirmClose), view);
    g_signal_connect(window, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer data) {
        auto* view = static_cast<GraphView*>(data);
        if (view->transportTimer) g_source_remove(view->transportTimer);
        if (view->externalConnectionTimer)
            g_source_remove(view->externalConnectionTimer);
        view->renderCancel.store(true, std::memory_order_release);
        if (view->renderThread.joinable()) view->renderThread.join();
        {
            std::lock_guard lock(view->renderCompletionMutex);
            if (view->renderCompletionSource)
                g_source_remove(view->renderCompletionSource);
            view->renderCompletionSource = 0;
        }
        if (view->renderProgressTimer)
            g_source_remove(view->renderProgressTimer);
        delete view;
    }), view);
    gtk_widget_show_all(window);
}
} // namespace

int main(int argc, char** argv) {
    auto* application = gtk_application_new("org.transmission.Graph", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(application, "activate", G_CALLBACK(activate), nullptr);
    const int status = g_application_run(G_APPLICATION(application), argc, argv);
    g_object_unref(application);
    return status;
}
