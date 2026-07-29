#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <cairo.h>
#include "transmission/AudioProcessor.h"
#include "transmission/GraphRuntimeController.h"
#include "transmission/JackConnectionManager.h"
#include "transmission/JackAudioDevice.h"
#include "transmission/UiProjectCodec.h"
#include "transmission/Vst3EditorHost.h"
#include "transmission/Vst3Processor.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <unistd.h>

namespace {
constexpr double nodeWidth = 190.0;
constexpr double nodeHeight = 70.0;

struct Node {
    std::string id;
    std::string label;
    bool system = false;
    std::size_t audioInputs = 0;
    std::size_t audioOutputs = 0;
    std::size_t midiInputs = 0;
    std::size_t midiOutputs = 0;
    double x = 0.0;
    double y = 0.0;
    std::string pluginPath;
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
        {"system-input", "System Input", true, 0, 2, 0, 1, 60.0, 150.0, ""},
        {"gain", "AGain / VST3", false, 2, 2, 1, 1, 340.0, 150.0, ""},
        {"system-output", "System Output", true, 2, 0, 1, 0, 580.0, 150.0, ""},
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
    std::size_t nextPluginId = 1;
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
    GtkSpinButton* tempo = nullptr;
    GtkSpinButton* loopBars = nullptr;
    GtkToggleButton* loop = nullptr;
    GtkWidget* window = nullptr;
    GtkWidget* canvas = nullptr;
    GtkWidget* playButton = nullptr;
    std::string filePath;
    std::string projectHelperPath;
};

struct PluginDialogContext {
    GraphView* view = nullptr;
    GtkWidget* canvas = nullptr;
    GtkWidget* dialog = nullptr;
    GtkComboBoxText* selector = nullptr;
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

void stopRuntime(GraphView& view, const char* status = nullptr) {
    if (view.externalConnectionTimer) {
        g_source_remove(view.externalConnectionTimer);
        view.externalConnectionTimer = 0;
    }
    view.externalConnectionAttempts = 0;
    view.pendingInputConnections.fill(false);
    view.pendingOutputConnections.fill(false);
#if defined(TRANSMISSION_UI_WITH_JACK) && defined(TRANSMISSION_UI_WITH_VST3)
    if (view.runtime) view.runtime->stop();
#endif
    if (status) setStatus(view, status);
}

transmission::RuntimeGraphSnapshot runtimeSnapshot(const GraphView& view) {
    transmission::RuntimeGraphSnapshot snapshot;
    snapshot.nodes.reserve(view.nodes.size());
    for (const auto& node : view.nodes) {
        auto kind = node.pluginPath.empty()
            ? transmission::RuntimeNodeKind::PassThrough
            : transmission::RuntimeNodeKind::Plugin;
        if (node.id == "system-input")
            kind = transmission::RuntimeNodeKind::SystemInput;
        else if (node.id == "system-output")
            kind = transmission::RuntimeNodeKind::SystemOutput;
        snapshot.nodes.push_back({node.id, kind, node.pluginPath});
    }
    snapshot.connections.reserve(view.edges.size());
    for (const auto& edge : view.edges) {
        if (edge.from >= view.nodes.size() || edge.to >= view.nodes.size()) continue;
        snapshot.connections.push_back({
            view.nodes[edge.from].id, view.nodes[edge.to].id,
            edge.kind == PortKind::Audio
                ? transmission::RuntimeConnectionKind::Audio
                : transmission::RuntimeConnectionKind::Midi});
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
        if (x >= it->x && x <= it->x + nodeWidth &&
            y >= it->y && y <= it->y + nodeHeight) return &*it;
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
    return node.y + nodeHeight * static_cast<double>(index + 1) /
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

void addPluginFromDialog(GtkDialog*, gint response, gpointer data) {
    auto* context = static_cast<PluginDialogContext*>(data);
    if (response == GTK_RESPONSE_ACCEPT) {
        const auto selected = gtk_combo_box_get_active(GTK_COMBO_BOX(context->selector));
        if (selected >= 0 && static_cast<std::size_t>(selected) < context->view->pluginPaths.size()) {
            const auto& path = context->view->pluginPaths[static_cast<std::size_t>(selected)];
            const auto stem = std::filesystem::path(path).stem().string();
            const auto id = "plugin-" + std::to_string(context->view->nextPluginId++);
            const auto offset = static_cast<double>(context->view->nodes.size() % 3) * 35.0;
            stopRuntime(*context->view, "Graph changed — press Play to compile and start audio");
            context->view->nodes.push_back({id, stem, false, 2, 2, 1, 1, 300.0 + offset,
                                            300.0 + offset, path});
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
    const auto pending =
        std::any_of(view.pendingInputConnections.begin(),
                    view.pendingInputConnections.end(), [](bool value) { return value; }) ||
        std::any_of(view.pendingOutputConnections.begin(),
                    view.pendingOutputConnections.end(), [](bool value) { return value; });
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
    deviceConfig.midiInputs = 1;
    std::string error;
    if (!view.jackConnections->deviceConfig(deviceConfig, error)) {
        setStatus(view, error, true);
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
#else
    setStatus(view, "This UI build requires both JACK and VST3 support", true);
#endif
    updateTransportDisplay(view);
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

void editNodeFromMenu(GtkMenuItem*, gpointer data) {
    auto* context = static_cast<NodeMenuContext*>(data);
    if (context->node >= context->view->nodes.size()) return;
    auto& node = context->view->nodes[context->node];
    if (node.system) {
        showSystemDialog(context->canvas, *context->view, node.id == "system-input");
    } else if (!node.pluginPath.empty() && context->view->editorHost) {
        context->view->editorHost->open(node.pluginPath, node.label);
    }
}

void removeNodeFromMenu(GtkMenuItem*, gpointer data) {
    auto* context = static_cast<NodeMenuContext*>(data);
    auto& view = *context->view;
    if (context->node >= view.nodes.size()) return;
    stopRuntime(view, "Graph changed — press Play to compile and start audio");
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
        cairo_set_source_rgb(cr, node.system ? 0.16 : 0.20, node.system ? 0.34 : 0.23,
                             node.system ? 0.28 : 0.38);
        cairo_rectangle(cr, node.x, node.y, nodeWidth, nodeHeight);
        cairo_fill_preserve(cr);
        cairo_set_line_width(cr, 2.0);
        cairo_set_source_rgb(cr, node.system ? 0.36 : 0.54, node.system ? 0.72 : 0.58,
                             node.system ? 0.78 : 0.90);
        cairo_stroke(cr);
        cairo_set_source_rgb(cr, 0.92, 0.94, 0.98);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 16.0);
        cairo_move_to(cr, node.x + 15.0, node.y + 31.0);
        cairo_show_text(cr, node.label.c_str());
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
            showPluginDialog(widget, view);
        }
        return TRUE;
    }
    if (event->button != GDK_BUTTON_PRIMARY) return FALSE;
    if (event->type == GDK_2BUTTON_PRESS) {
        if (auto* node = nodeAt(view, event->x, event->y);
            node && !node->pluginPath.empty() && view.editorHost) {
            cancelPointerInteraction(widget, view);
            view.editorHost->open(node->pluginPath, node->label);
            return TRUE;
        }
        if (auto* node = nodeAt(view, event->x, event->y);
            node && node->system) {
            cancelPointerInteraction(widget, view);
            showSystemDialog(widget, view, node->id == "system-input");
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
        auto kind = node.pluginPath.empty()
            ? transmission::UiProjectNodeKind::PassThrough
            : transmission::UiProjectNodeKind::Plugin;
        if (node.id == "system-input")
            kind = transmission::UiProjectNodeKind::SystemInput;
        else if (node.id == "system-output")
            kind = transmission::UiProjectNodeKind::SystemOutput;
        project.nodes.push_back({
            node.id, node.label, kind,
            node.audioInputs, node.audioOutputs, node.midiInputs, node.midiOutputs,
            node.x, node.y, node.pluginPath});
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
    return project;
}

transmission::UiProject defaultProject() {
    transmission::UiProject project;
    project.nodes = {
        {"system-input", "System Input", transmission::UiProjectNodeKind::SystemInput,
         0, 2, 0, 1, 60.0, 150.0, ""},
        {"gain", "AGain / VST3", transmission::UiProjectNodeKind::PassThrough,
         2, 2, 1, 1, 340.0, 150.0, ""},
        {"system-output", "System Output", transmission::UiProjectNodeKind::SystemOutput,
         2, 0, 1, 0, 580.0, 150.0, ""}
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
    return true;
}

bool applyProject(GraphView& view, const transmission::UiProject& project,
                  std::string& error) {
    if (!validateProject(project, error)) return false;
    std::unordered_map<std::string, std::size_t> indices;
    std::vector<Node> nodes;
    nodes.reserve(project.nodes.size());
    for (const auto& source : project.nodes) {
        indices.emplace(source.id, nodes.size());
        const bool system =
            source.kind == transmission::UiProjectNodeKind::SystemInput ||
            source.kind == transmission::UiProjectNodeKind::SystemOutput;
        nodes.push_back({
            source.id, source.label, system,
            source.audioInputs, source.audioOutputs, source.midiInputs, source.midiOutputs,
            source.x, source.y, source.pluginPath});
    }
    std::vector<Edge> edges;
    edges.reserve(project.connections.size());
    for (const auto& source : project.connections) {
        edges.push_back({
            indices.at(source.from), indices.at(source.to), source.fromPort, source.toPort,
            source.kind == transmission::UiProjectConnectionKind::Audio
                ? PortKind::Audio : PortKind::Midi});
    }

    stopRuntime(view);
    view.nodes = std::move(nodes);
    view.edges = std::move(edges);
    view.systemInputConnections = project.systemInputConnections;
    view.systemOutputConnections = project.systemOutputConnections;
    view.dragging = static_cast<std::size_t>(-1);
    view.connectingFrom = static_cast<std::size_t>(-1);
    view.nextPluginId = 1;
    std::unordered_set<std::string> identifiers;
    for (const auto& node : view.nodes) identifiers.insert(node.id);
    while (identifiers.contains("plugin-" + std::to_string(view.nextPluginId)))
        ++view.nextPluginId;
    gtk_spin_button_set_value(view.tempo, project.tempo);
    gtk_spin_button_set_value(view.loopBars, project.loopBars);
    gtk_toggle_button_set_active(view.loop, project.loopEnabled);
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
    std::string output;
    std::string error;
    if (!runProjectHelper(view, "save", path,
                          transmission::encodeUiProject(captureProject(view)),
                          output, error)) {
        setStatus(view, "Unable to save project: " + error, true);
        return false;
    }
    view.filePath = path;
    updateWindowTitle(view);
    return true;
}

void saveProjectAs(GraphView& view) {
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
        saveProject(view, path);
    }
    gtk_widget_destroy(dialog);
}

void newProjectActivated(GtkMenuItem*, gpointer data) {
    auto& view = *static_cast<GraphView*>(data);
    std::string error;
    if (!applyProject(view, defaultProject(), error)) {
        setStatus(view, error, true);
        return;
    }
    view.filePath.clear();
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

void quitActivated(GtkMenuItem*, gpointer data) {
    gtk_window_close(GTK_WINDOW(static_cast<GraphView*>(data)->window));
}

void activate(GtkApplication* application, gpointer) {
    auto* view = new GraphView();
    view->jackConnections = std::make_unique<transmission::JackConnectionManager>();
    view->editorHost = std::make_unique<transmission::Vst3EditorHost>();
#if defined(TRANSMISSION_UI_WITH_JACK) && defined(TRANSMISSION_UI_WITH_VST3)
    view->jackDevice = std::make_unique<transmission::JackAudioDevice>();
    view->runtime = std::make_unique<transmission::GraphRuntimeController>(
        [](const transmission::RuntimeGraphNode& node,
           const transmission::AudioDeviceConfig& config,
           std::string& error) -> std::unique_ptr<transmission::AudioProcessor> {
            if (node.kind != transmission::RuntimeNodeKind::Plugin)
                return std::make_unique<transmission::PassThroughProcessor>();
            auto processor = std::make_unique<transmission::Vst3Processor>();
            if (!processor->initialize(node.pluginPath, config.channels,
                                       config.blockSize, config.sampleRate, error))
                return nullptr;
            return processor;
        });
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
    auto* separator = gtk_separator_menu_item_new();
    auto* quitItem = gtk_menu_item_new_with_mnemonic("_Quit");
    gtk_menu_shell_append(GTK_MENU_SHELL(fileMenu), newItem);
    gtk_menu_shell_append(GTK_MENU_SHELL(fileMenu), openItem);
    gtk_menu_shell_append(GTK_MENU_SHELL(fileMenu), saveItem);
    gtk_menu_shell_append(GTK_MENU_SHELL(fileMenu), saveAsItem);
    gtk_menu_shell_append(GTK_MENU_SHELL(fileMenu), separator);
    gtk_menu_shell_append(GTK_MENU_SHELL(fileMenu), quitItem);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(fileItem), fileMenu);
    gtk_menu_shell_append(GTK_MENU_SHELL(menuBar), fileItem);
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
    g_signal_connect(window, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer data) {
        auto* view = static_cast<GraphView*>(data);
        if (view->transportTimer) g_source_remove(view->transportTimer);
        if (view->externalConnectionTimer)
            g_source_remove(view->externalConnectionTimer);
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
