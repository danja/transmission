#include <gtk/gtk.h>
#include <cairo.h>
#include "transmission/AudioProcessor.h"
#include "transmission/GraphRuntimeController.h"
#include "transmission/JackConnectionManager.h"
#include "transmission/JackAudioDevice.h"
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
#include <vector>

namespace {
constexpr double nodeWidth = 190.0;
constexpr double nodeHeight = 92.0;
constexpr double portSpacing = 24.0;

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
    GtkSpinButton* tempo = nullptr;
    GtkSpinButton* loopBars = nullptr;
    GtkToggleButton* loop = nullptr;
    GtkWidget* playButton = nullptr;
    GtkWidget* positionLabel = nullptr;
    GtkWidget* statusLabel = nullptr;
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
    if (!view.statusLabel) return;
    gtk_label_set_text(GTK_LABEL(view.statusLabel), message.c_str());
    auto* style = gtk_widget_get_style_context(view.statusLabel);
    if (error)
        gtk_style_context_add_class(style, "error");
    else
        gtk_style_context_remove_class(style, "error");
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

Node* nodeAt(GraphView& view, double x, double y) {
    for (auto it = view.nodes.rbegin(); it != view.nodes.rend(); ++it) {
        if (x >= it->x && x <= it->x + nodeWidth &&
            y >= it->y && y <= it->y + nodeHeight) return &*it;
    }
    return nullptr;
}

double portY(const Node& node, std::size_t port) {
    return node.y + nodeHeight / 2.0 + (static_cast<double>(port) - 0.5) * portSpacing;
}

struct PortHit {
    std::size_t node = static_cast<std::size_t>(-1);
    std::size_t port = 0;
    bool output = false;
    PortKind kind = PortKind::Audio;
};

double midiPortY(const Node& node, std::size_t port) {
    return node.y + nodeHeight - 14.0 - static_cast<double>(port) * portSpacing;
}

PortHit portAt(const GraphView& view, double x, double y) {
    constexpr double radius = 9.0;
    for (std::size_t index = 0; index < view.nodes.size(); ++index) {
        const auto& node = view.nodes[index];
        for (std::size_t port = 0; port < node.audioInputs; ++port) {
            if (std::hypot(x - node.x, y - portY(node, port)) <= radius)
                return {index, port, false, PortKind::Audio};
        }
        for (std::size_t port = 0; port < node.audioOutputs; ++port) {
            if (std::hypot(x - (node.x + nodeWidth), y - portY(node, port)) <= radius)
                return {index, port, true, PortKind::Audio};
        }
        for (std::size_t port = 0; port < node.midiInputs; ++port) {
            if (std::hypot(x - node.x, y - midiPortY(node, port)) <= radius)
                return {index, port, false, PortKind::Midi};
        }
        for (std::size_t port = 0; port < node.midiOutputs; ++port) {
            if (std::hypot(x - (node.x + nodeWidth), y - midiPortY(node, port)) <= radius)
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
        const double y1 = edge.kind == PortKind::Midi ? midiPortY(from, edge.fromPort)
                                                       : portY(from, edge.fromPort);
        const double x2 = to.x;
        const double y2 = edge.kind == PortKind::Midi ? midiPortY(to, edge.toPort)
                                                       : portY(to, edge.toPort);
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
        std::string routeError;
        if (!view.jackConnections->connectInput(index, view.systemInputConnections[index],
                                                routeError))
            appendError("input " + std::to_string(index + 1) + ": " + routeError);
    }
    for (std::size_t index = 0; index < view.systemOutputConnections.size(); ++index) {
        std::string routeError;
        if (!view.jackConnections->connectOutput(index, view.systemOutputConnections[index],
                                                 routeError))
            appendError("output " + std::to_string(index + 1) + ": " + routeError);
    }
    return applied;
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
    if (view.playButton)
        gtk_button_set_label(GTK_BUTTON(view.playButton), running ? "Stop" : "Play");
    if (view.positionLabel) {
        double position = 0.0;
#if defined(TRANSMISSION_UI_WITH_JACK) && defined(TRANSMISSION_UI_WITH_VST3)
        if (view.runtime) position = view.runtime->diagnostics().positionBeats;
#endif
        char text[96];
        g_snprintf(text, sizeof(text), "%s  •  beat %.2f", running ? "Playing" : "Stopped",
                   position);
        gtk_label_set_text(GTK_LABEL(view.positionLabel), text);
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
    if (!applyExternalConnections(view, error))
        setStatus(view, "Audio is running, but JACK routing failed: " + error, true);
    else
        setStatus(view, "Audio running through JACK");
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
        const double fromY = edge.kind == PortKind::Midi ? midiPortY(from, edge.fromPort)
                                                          : portY(from, edge.fromPort);
        const double toY = edge.kind == PortKind::Midi ? midiPortY(to, edge.toPort)
                                                        : portY(to, edge.toPort);
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
        const auto y = view.connectingKind == PortKind::Midi ? midiPortY(from, view.connectingPort)
                                                               : portY(from, view.connectingPort);
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
        cairo_set_font_size(cr, 11.0);
        cairo_set_source_rgb(cr, 0.70, 0.75, 0.82);
        cairo_move_to(cr, node.x + 15.0, node.y + 68.0);
        cairo_show_text(cr, node.id == "system-input" ? "audio + MIDI outputs"
                                                        : node.id == "system-output" ? "audio + MIDI inputs"
                                                                                       : "audio + MIDI processor");
        for (std::size_t port = 0; port < node.audioInputs; ++port)
            drawPort(cr, node.x, portY(node, port), false, PortKind::Audio);
        for (std::size_t port = 0; port < node.audioOutputs; ++port)
            drawPort(cr, node.x + nodeWidth, portY(node, port), true, PortKind::Audio);
        for (std::size_t port = 0; port < node.midiInputs; ++port)
            drawPort(cr, node.x, midiPortY(node, port), false, PortKind::Midi);
        for (std::size_t port = 0; port < node.midiOutputs; ++port)
            drawPort(cr, node.x + nodeWidth, midiPortY(node, port), true, PortKind::Midi);
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
            view.editorHost->open(node->pluginPath, node->label);
            return TRUE;
        }
        if (auto* node = nodeAt(view, event->x, event->y);
            node && node->system) {
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
    GtkWidget* title = gtk_label_new("  Transmission   •   right-click the graph background to add a VST3 plugin, or a connection to remove it");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_widget_set_margin_top(title, 12);
    gtk_widget_set_margin_bottom(title, 12);
    gtk_widget_set_margin_start(title, 8);
    gtk_box_pack_start(GTK_BOX(frame), title, FALSE, FALSE, 0);

    GtkWidget* transportBar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_margin_start(transportBar, 8);
    gtk_widget_set_margin_end(transportBar, 8);
    gtk_widget_set_margin_bottom(transportBar, 8);
    auto* playButton = gtk_button_new_with_label("Play");
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
    auto* positionLabel = gtk_label_new("Stopped  •  beat 0.00");
    gtk_widget_set_margin_start(positionLabel, 16);
    gtk_box_pack_start(GTK_BOX(transportBar), positionLabel, FALSE, FALSE, 4);
    gtk_box_pack_start(GTK_BOX(frame), transportBar, FALSE, FALSE, 0);
    auto* statusLabel = gtk_label_new(
        "Stopped — edit the graph and press Play to compile and start JACK audio");
    gtk_widget_set_halign(statusLabel, GTK_ALIGN_START);
    gtk_widget_set_margin_start(statusLabel, 12);
    gtk_widget_set_margin_bottom(statusLabel, 8);
    gtk_box_pack_start(GTK_BOX(frame), statusLabel, FALSE, FALSE, 0);

    view->tempo = tempo;
    view->loopBars = loopBars;
    view->loop = GTK_TOGGLE_BUTTON(loop);
    view->playButton = playButton;
    view->positionLabel = positionLabel;
    view->statusLabel = statusLabel;
    loopChanged(nullptr, view);
    g_signal_connect(playButton, "clicked", G_CALLBACK(playStopClicked), view);
    g_signal_connect(resetButton, "clicked", G_CALLBACK(resetTransportClicked), view);
    g_signal_connect(tempo, "value-changed", G_CALLBACK(tempoChanged), view);
    g_signal_connect(loop, "toggled", G_CALLBACK(loopChanged), view);
    g_signal_connect(loopBars, "value-changed", G_CALLBACK(loopChanged), view);

    GtkWidget* canvas = gtk_drawing_area_new();
    gtk_widget_set_hexpand(canvas, TRUE);
    gtk_widget_set_vexpand(canvas, TRUE);
    gtk_widget_set_size_request(canvas, 800, 400);
    gtk_widget_add_events(canvas, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK);
    g_signal_connect(canvas, "draw", G_CALLBACK(drawGraph), view);
    g_signal_connect(canvas, "button-press-event", G_CALLBACK(buttonPress), view);
    g_signal_connect(canvas, "motion-notify-event", G_CALLBACK(motion), view);
    g_signal_connect(canvas, "button-release-event", G_CALLBACK(buttonRelease), view);
    gtk_box_pack_start(GTK_BOX(frame), canvas, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(window), frame);
    g_signal_connect(window, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer data) {
        auto* view = static_cast<GraphView*>(data);
        if (view->transportTimer) g_source_remove(view->transportTimer);
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
