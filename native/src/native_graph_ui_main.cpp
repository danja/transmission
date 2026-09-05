#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <cairo.h>

// Forward-declare only — including <X11/Xlib.h> pollutes the translation unit
// with macros (None, Bool, Status, Window) that break GTK/GLib code.
extern "C" { int XInitThreads(); }
#include "transmission/AudioClipProcessor.h"
#include "transmission/AudioProcessor.h"
#include "transmission/GraphRuntimeController.h"
#include "transmission/MidiClipProcessor.h"
#include "transmission/SmfReader.h"
#include "transmission/JackConnectionManager.h"
#include "transmission/JackPortIdentity.h"
#include "transmission/JackAudioDevice.h"
#include "transmission/OfflineAudioRenderer.h"
#include "transmission/SmfWriter.h"
#include "transmission/UiProjectCodec.h"
#include "transmission/Vst3EditorHost.h"
#include "transmission/Vst3Inspector.h"
#include "transmission/Vst3Processor.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <ctime>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <signal.h>
#include <unistd.h>
#include <optional>
#if defined(TRANSMISSION_UI_WITH_LIVE_SERVER)
#include <curl/curl.h>
#endif

namespace {
constexpr double nodeWidth = 190.0;
constexpr double minimumNodeHeight = 70.0;
constexpr double portSpacing = 18.0;
constexpr std::int32_t vst3CanAutomateFlag = 1 << 0;

enum class NodeKind {
    SystemInput, SystemOutput, PassThrough, Plugin, MidiInput, MidiOutput, Gain,
    AudioClip, MidiClip
};

struct PluginCacheEntry {
    std::string path;
    std::string name;
    std::string vendor;
    std::string category;
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
    double pan = 0.0;
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
    std::vector<PluginCacheEntry> plugins;
    std::string pluginSearchPath{"~/"};
    std::unordered_map<
        std::string, std::unordered_map<std::uint32_t, double>> parameterValues;
    std::unordered_map<std::string, transmission::ProcessorState> pluginStates;
    double arrangementLengthBeats = 0.0;
    std::vector<transmission::UiProjectMidiClip> midiClips;
    std::vector<transmission::UiProjectGainLane> gainLanes;
    std::vector<transmission::UiProjectMidiParameterMapping> midiMappings;
    std::size_t nextPluginId = 1;
    std::size_t nextMidiInputId = 1;
    std::size_t nextMidiOutputId = 1;
    std::size_t nextGainId = 1;
    std::size_t nextAudioClipId = 1;
    std::size_t nextMidiClipId = 1;
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
    std::string lastConnectionError;
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
    std::string inspectBinaryPath;
    std::string lastSavedSnapshot;
    std::vector<std::string> recentFiles;
    GtkWidget* recentMenu = nullptr;
    GtkWidget* jackIndicator = nullptr;
    GtkWidget* mcpIndicator = nullptr;
#if defined(TRANSMISSION_UI_WITH_LIVE_SERVER)
    std::string liveServerUrl{"http://127.0.0.1:7878"};
    bool mcpServerEnabled = true;
    bool liveServerAvailable = false;
    int liveServerRevision = -1;
    int liveServerGeneration = -1;
    std::string liveServerFilePath;
    guint liveServerPollTimer = 0;
    GSubprocess* liveServerProcess = nullptr;
#endif
    std::atomic<float> outputPeakL{0.f};
    std::atomic<float> outputPeakR{0.f};
    guint peakMeterTimer = 0;
    bool startJackOnStartup = false;
    std::string jackStartCommand = "jackd -d alsa -r 48000 -p 1024";
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
    GtkWidget* consoleWindow = nullptr;
    GtkWidget* consoleTextView = nullptr;
    GtkWidget* consoleEntry = nullptr;
    guint connectionWatchTimer = 0;
    std::array<std::string, 2> lastWatchedConnections{};
    double zoomLevel = 1.0;
    GtkWidget* scrolledWindow = nullptr;
};

struct PluginScanJob;

struct PluginDialogContext {
    GraphView* view = nullptr;
    GtkWidget* canvas = nullptr;
    GtkWidget* dialog = nullptr;
    GtkTreeView* treeView = nullptr;
    PluginScanJob* job = nullptr;
};

struct MidiDialogContext {
    GraphView* view = nullptr;
    GtkWidget* canvas = nullptr;
    GtkWidget* dialog = nullptr;
    GtkComboBoxText* selector = nullptr;
    bool input = true;
    std::size_t node = static_cast<std::size_t>(-1);
};

struct PanDial {
    GtkWidget* widget = nullptr;
    double value = 0.0;
    double dragY = 0.0;
    double dragValue = 0.0;
    bool dragging = false;
};

struct GainDialogContext {
    GraphView* view = nullptr;
    GtkWidget* canvas = nullptr;
    GtkWidget* dialog = nullptr;
    GtkRange* gain = nullptr;
    PanDial pan;
    std::size_t node = static_cast<std::size_t>(-1);
};

struct MidiMappingParameter {
    std::uint32_t id = 0;
    std::string label;
};

struct MidiMappingDialogContext {
    GraphView* view = nullptr;
    GtkWidget* canvas = nullptr;
    GtkWidget* dialog = nullptr;
    GtkComboBoxText* parameter = nullptr;
    GtkComboBoxText* channel = nullptr;
    GtkSpinButton* controller = nullptr;
    GtkToggleButton* consume = nullptr;
    GtkListStore* store = nullptr;
    GtkTreeView* list = nullptr;
    std::size_t node = static_cast<std::size_t>(-1);
    std::vector<MidiMappingParameter> parameters;
    std::vector<transmission::UiProjectMidiParameterMapping> mappings;
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

bool runProjectHelper(const GraphView& view, const char* command,
                      const std::string& path, const std::string& input,
                      std::string& output, std::string& error);

void logConsole(GraphView& view, const std::string& message) {
    if (!view.consoleTextView) return;
    auto* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view.consoleTextView));
    const auto now = std::time(nullptr);
    char ts[12];
    std::strftime(ts, sizeof(ts), "[%H:%M:%S] ", std::localtime(&now));
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_buffer_insert(buffer, &end, (std::string(ts) + message + "\n").c_str(), -1);
    auto* mark = gtk_text_buffer_get_insert(buffer);
    gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(view.consoleTextView), mark);
}

void setStatus(GraphView& view, const std::string& message, bool error = false) {
    logConsole(view, error ? "[error] " + message : message);
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
        if (node.kind == transmission::RuntimeNodeKind::AudioClip) {
            auto proc = std::make_unique<transmission::AudioClipProcessor>();
            if (!proc->load(node.pluginPath, config.sampleRate, error)) return nullptr;
            return proc;
        }
        if (node.kind == transmission::RuntimeNodeKind::MidiClip) {
            auto proc = std::make_unique<transmission::MidiClipProcessor>();
            if (!proc->load(node.pluginPath, error)) return nullptr;
            return proc;
        }
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
        snapshot.nodes.back().pan = node.pan;
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
    snapshot.midiParameterMappings.reserve(view.midiMappings.size());
    for (const auto& mapping : view.midiMappings)
        snapshot.midiParameterMappings.push_back({
            mapping.targetNodeId, mapping.parameterId, mapping.channel,
            mapping.controller, mapping.consume});
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

struct InspectResult { bool ok = false; std::string name, vendor, category; };

InspectResult safeInspect(const std::string& binaryPath, const std::string& pluginPath) {
    if (binaryPath.empty()) return {};
    GSubprocessFlags flags = static_cast<GSubprocessFlags>(
        G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE);
    GError* err = nullptr;
    GSubprocess* proc = g_subprocess_new(flags, &err,
        binaryPath.c_str(), pluginPath.c_str(), nullptr);
    if (!proc) { g_error_free(err); return {}; }
    gchar* output = nullptr;
    gboolean ok = g_subprocess_communicate_utf8(proc, nullptr, nullptr, &output, nullptr, &err);
    const bool success = ok && g_subprocess_get_exit_status(proc) == 0;
    g_object_unref(proc);
    if (err) g_error_free(err);
    InspectResult result;
    if (!success || !output) { g_free(output); return result; }
    std::istringstream ss(output);
    g_free(output);
    std::string line;
    while (std::getline(ss, line)) {
        if (line.rfind("name=", 0) == 0)     result.name     = line.substr(5);
        else if (line.rfind("vendor=", 0) == 0)   result.vendor   = line.substr(7);
        else if (line.rfind("category=", 0) == 0) result.category = line.substr(9);
    }
    result.ok = true;
    return result;
}

struct PluginScanJob {
    GraphView* view = nullptr;
    std::string binaryPath;
    std::vector<PluginCacheEntry> toScan;
    GtkListStore* store = nullptr;
    GtkLabel* statusLabel = nullptr;
    int total = 0;
    std::atomic<bool> cancelled{false};
    std::vector<PluginCacheEntry> good;
};

struct PluginScanResult {
    PluginScanJob* job;
    PluginCacheEntry entry;
    bool ok;
    int index;
};

gboolean pluginScanResultIdle(gpointer data) {
    auto* result = static_cast<PluginScanResult*>(data);
    auto* job = result->job;
    if (!job->cancelled.load()) {
        if (result->ok) {
            GtkTreeIter it;
            gtk_list_store_append(job->store, &it);
            gtk_list_store_set(job->store, &it,
                0, result->entry.name.c_str(),
                1, result->entry.vendor.c_str(),
                2, result->entry.category.c_str(),
                3, result->entry.path.c_str(), -1);
            job->good.push_back(result->entry);
        }
        const int done = result->index + 1;
        std::string msg = "Inspecting " + std::to_string(done) +
            " / " + std::to_string(job->total) + "…";
        gtk_label_set_text(job->statusLabel, msg.c_str());
    }
    delete result;
    return G_SOURCE_REMOVE;
}

gboolean pluginScanCompleteIdle(gpointer data) {
    auto* job = static_cast<PluginScanJob*>(data);
    if (!job->cancelled.load()) {
        const int n = static_cast<int>(job->good.size());
        gtk_label_set_text(job->statusLabel,
            (std::to_string(n) + " plugin(s) found").c_str());
        std::sort(job->good.begin(), job->good.end(), [](const auto& a, const auto& b) {
            if (a.category != b.category) return a.category < b.category;
            return a.name < b.name;
        });
        job->view->plugins = std::move(job->good);
    }
    g_object_unref(job->store);
    delete job;
    return G_SOURCE_REMOVE;
}

void pluginScanThreadFunc(PluginScanJob* job) {
    for (int i = 0; i < static_cast<int>(job->toScan.size()); ++i) {
        if (job->cancelled.load()) break;
        auto& p = job->toScan[static_cast<std::size_t>(i)];
        auto r = safeInspect(job->binaryPath, p.path);
        if (r.ok && !r.name.empty()) p.name = r.name;
        if (r.ok) { p.vendor = r.vendor; p.category = r.category; }
        auto* result = new PluginScanResult{job, p, r.ok, i};
        g_idle_add(pluginScanResultIdle, result);
    }
    g_idle_add(pluginScanCompleteIdle, job);
}

struct ConsoleScanJob {
    GraphView* view;
    std::string binaryPath;
    std::vector<PluginCacheEntry> toScan;
};

struct ConsoleScanLog {
    GraphView* view;
    std::string message;
};

struct ConsoleScanComplete {
    GraphView* view;
    std::vector<PluginCacheEntry> good;
    int nOk = 0;
    int nFail = 0;
};

gboolean consoleScanLogIdle(gpointer data) {
    auto* ev = static_cast<ConsoleScanLog*>(data);
    logConsole(*ev->view, ev->message);
    delete ev;
    return G_SOURCE_REMOVE;
}

gboolean consoleScanCompleteIdle(gpointer data) {
    auto* ev = static_cast<ConsoleScanComplete*>(data);
    std::sort(ev->good.begin(), ev->good.end(), [](const auto& a, const auto& b) {
        if (a.category != b.category) return a.category < b.category;
        return a.name < b.name;
    });
    ev->view->plugins = std::move(ev->good);
    logConsole(*ev->view, "Scan complete: " + std::to_string(ev->nOk) + " OK, " +
        std::to_string(ev->nFail) + " failed (excluded)");
    delete ev;
    return G_SOURCE_REMOVE;
}

void consoleScanThreadFunc(ConsoleScanJob* job) {
    auto* complete = new ConsoleScanComplete{job->view, {}, 0, 0};
    for (auto& plugin : job->toScan) {
        auto r = safeInspect(job->binaryPath, plugin.path);
        if (r.ok) {
            if (!r.name.empty()) plugin.name = r.name;
            plugin.vendor = r.vendor;
            plugin.category = r.category;
            std::string msg = "  OK  " + plugin.name +
                (plugin.vendor.empty() ? "" : "  [" + plugin.vendor + "]") +
                (plugin.category.empty() ? "" : "  " + plugin.category);
            g_idle_add(consoleScanLogIdle, new ConsoleScanLog{job->view, std::move(msg)});
            complete->good.push_back(std::move(plugin));
            ++complete->nOk;
        } else {
            g_idle_add(consoleScanLogIdle, new ConsoleScanLog{job->view, "  FAIL  " + plugin.path});
            ++complete->nFail;
        }
    }
    g_idle_add(consoleScanCompleteIdle, complete);
    delete job;
}

gboolean pluginVisibleFunc(GtkTreeModel* model, GtkTreeIter* iter, gpointer data) {
    const char* text = gtk_entry_get_text(GTK_ENTRY(data));
    if (!text || !*text) return TRUE;
    std::string q(text);
    for (auto& c : q) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    auto ciContains = [&](const char* s) {
        if (!s || !*s) return false;
        std::string lower(s);
        for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return lower.find(q) != std::string::npos;
    };
    gchar* name = nullptr; gchar* vendor = nullptr; gchar* category = nullptr;
    gtk_tree_model_get(model, iter, 0, &name, 1, &vendor, 2, &category, -1);
    const bool visible = ciContains(name) || ciContains(vendor) || ciContains(category);
    g_free(name); g_free(vendor); g_free(category);
    return visible ? TRUE : FALSE;
}

void pluginSearchChanged(GtkEntry*, gpointer data) {
    gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(data));
}

void pluginRowActivated(GtkTreeView*, GtkTreePath*, GtkTreeViewColumn*, gpointer data) {
    gtk_dialog_response(GTK_DIALOG(data), GTK_RESPONSE_ACCEPT);
}

void pluginSelectionChanged(GtkTreeSelection* sel, gpointer data) {
    gtk_widget_set_sensitive(GTK_WIDGET(data),
        gtk_tree_selection_count_selected_rows(sel) > 0);
}

void addPluginFromDialog(GtkDialog*, gint response, gpointer data) {
    auto* context = static_cast<PluginDialogContext*>(data);
    if (response == GTK_RESPONSE_ACCEPT) {
        GtkTreeModel* model = nullptr;
        GtkTreeIter iter;
        if (gtk_tree_selection_get_selected(
                gtk_tree_view_get_selection(context->treeView), &model, &iter)) {
            gchar* pathStr = nullptr;
            gtk_tree_model_get(model, &iter, 3, &pathStr, -1);
            if (pathStr) {
                const std::string path(pathStr);
                g_free(pathStr);
                const auto stem = std::filesystem::path(path).stem().string();
                const auto id = "plugin-" + std::to_string(context->view->nextPluginId++);
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
                    std::max<std::size_t>(1, topology.midiInputs),
                    topology.midiOutputs, context->view->pointerX,
                    context->view->pointerY, path, "", std::move(inputLabels),
                    std::move(outputLabels)});
                gtk_widget_queue_draw(context->canvas);
            }
        }
    }
    if (context->job) {
        context->job->cancelled = true;
        context->job = nullptr;
    }
    gtk_widget_destroy(context->dialog);
    delete context;
}


void showPluginDialog(GtkWidget* canvas, GraphView& view) {
    auto* dialog = gtk_dialog_new_with_buttons(
        "Add VST3 Plugin", GTK_WINDOW(gtk_widget_get_toplevel(canvas)),
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        "_Cancel", GTK_RESPONSE_CANCEL, "_Add", GTK_RESPONSE_ACCEPT, nullptr);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 580, 420);
    auto* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 12);
    gtk_box_set_spacing(GTK_BOX(content), 8);

    auto* searchEntry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(searchEntry),
        "Search by name, vendor, or category…");
    gtk_box_pack_start(GTK_BOX(content), searchEntry, FALSE, FALSE, 0);

    enum { COL_NAME, COL_VENDOR, COL_CATEGORY, COL_PATH };
    auto* store = gtk_list_store_new(4,
        G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);

    const bool needsScan = std::any_of(view.plugins.begin(), view.plugins.end(),
        [](const auto& p) { return p.vendor.empty() && p.category.empty(); });

    if (!needsScan) {
        for (const auto& plugin : view.plugins) {
            GtkTreeIter it;
            gtk_list_store_append(store, &it);
            gtk_list_store_set(store, &it,
                COL_NAME, plugin.name.c_str(), COL_VENDOR, plugin.vendor.c_str(),
                COL_CATEGORY, plugin.category.c_str(), COL_PATH, plugin.path.c_str(), -1);
        }
    }

    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(store),
        COL_CATEGORY, GTK_SORT_ASCENDING);

    auto* filter = gtk_tree_model_filter_new(GTK_TREE_MODEL(store), nullptr);
    gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(filter),
        pluginVisibleFunc, searchEntry, nullptr);
    g_signal_connect(searchEntry, "changed", G_CALLBACK(pluginSearchChanged), filter);

    auto* treeView = gtk_tree_view_new_with_model(filter);
    g_object_unref(filter);
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(treeView), TRUE);
    gtk_tree_view_set_enable_search(GTK_TREE_VIEW(treeView), FALSE);

    struct ColDef { const char* title; int col; int minW; };
    for (const auto& cd : { ColDef{"Name", COL_NAME, 180},
                             ColDef{"Vendor", COL_VENDOR, 120},
                             ColDef{"Category", COL_CATEGORY, 140} }) {
        auto* renderer = gtk_cell_renderer_text_new();
        g_object_set(renderer, "ellipsize", PANGO_ELLIPSIZE_END, nullptr);
        auto* column = gtk_tree_view_column_new_with_attributes(
            cd.title, renderer, "text", cd.col, nullptr);
        gtk_tree_view_column_set_resizable(GTK_TREE_VIEW_COLUMN(column), TRUE);
        gtk_tree_view_column_set_min_width(GTK_TREE_VIEW_COLUMN(column), cd.minW);
        gtk_tree_view_column_set_sort_column_id(GTK_TREE_VIEW_COLUMN(column), cd.col);
        gtk_tree_view_append_column(GTK_TREE_VIEW(treeView), column);
    }

    auto* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
        GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled), GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(scrolled), treeView);
    gtk_box_pack_start(GTK_BOX(content), scrolled, TRUE, TRUE, 0);

    auto* statusLabel = gtk_label_new(needsScan ? "Inspecting plugins…" : "");
    gtk_label_set_xalign(GTK_LABEL(statusLabel), 0.0F);
    gtk_box_pack_start(GTK_BOX(content), statusLabel, FALSE, FALSE, 0);

    auto* addButton = gtk_dialog_get_widget_for_response(
        GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
    gtk_widget_set_sensitive(addButton, FALSE);
    auto* sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeView));
    g_signal_connect(sel, "changed", G_CALLBACK(pluginSelectionChanged), addButton);
    g_signal_connect(treeView, "row-activated", G_CALLBACK(pluginRowActivated), dialog);

    PluginScanJob* job = nullptr;
    if (needsScan) {
        job = new PluginScanJob{};
        job->view = &view;
        job->binaryPath = view.inspectBinaryPath;
        job->toScan = view.plugins;
        job->store = GTK_LIST_STORE(g_object_ref(store));
        job->statusLabel = GTK_LABEL(statusLabel);
        job->total = static_cast<int>(view.plugins.size());
        std::thread(pluginScanThreadFunc, job).detach();
    }
    g_object_unref(store);

    auto* context = new PluginDialogContext{&view, canvas, dialog, GTK_TREE_VIEW(treeView), job};
    g_signal_connect(dialog, "response", G_CALLBACK(addPluginFromDialog), context);
    gtk_widget_show_all(dialog);
    gtk_widget_grab_focus(searchEntry);
}

void setPanDialValue(PanDial& dial, double value) {
    dial.value = std::clamp(value, -1.0, 1.0);
    if (dial.widget) gtk_widget_queue_draw(dial.widget);
}

gboolean drawPanDial(GtkWidget* widget, cairo_t* cr, gpointer data) {
    const auto& dial = *static_cast<PanDial*>(data);
    const auto width = gtk_widget_get_allocated_width(widget);
    const auto height = gtk_widget_get_allocated_height(widget);
    const auto centerX = static_cast<double>(width) * 0.5;
    const auto centerY = static_cast<double>(height) * 0.46;
    const auto radius = std::min(width, height) * 0.28;
    const auto start = 0.75 * M_PI;
    const auto end = 2.25 * M_PI;
    const auto angle = start + (dial.value + 1.0) * 0.5 * (end - start);

    cairo_set_line_width(cr, 5.0);
    cairo_set_source_rgb(cr, 0.22, 0.24, 0.29);
    cairo_arc(cr, centerX, centerY, radius + 8.0, start, end);
    cairo_stroke(cr);
    cairo_set_source_rgb(cr, 0.28, 0.62, 0.88);
    cairo_arc(cr, centerX, centerY, radius + 8.0, start, angle);
    cairo_stroke(cr);

    cairo_set_source_rgb(cr, 0.17, 0.18, 0.22);
    cairo_arc(cr, centerX, centerY, radius, 0.0, 2.0 * M_PI);
    cairo_fill_preserve(cr);
    cairo_set_line_width(cr, 2.0);
    cairo_set_source_rgb(cr, 0.62, 0.66, 0.74);
    cairo_stroke(cr);
    cairo_set_line_width(cr, 3.0);
    cairo_set_source_rgb(cr, 0.92, 0.95, 1.0);
    cairo_move_to(cr, centerX, centerY);
    cairo_line_to(cr, centerX + std::cos(angle) * radius * 0.72,
                  centerY + std::sin(angle) * radius * 0.72);
    cairo_stroke(cr);

    const auto valueText = std::fabs(dial.value) < 0.005
        ? std::string("C")
        : (dial.value < 0.0 ? "L " : "R ") +
            std::to_string(static_cast<int>(
                std::lround(std::fabs(dial.value) * 100.0)));
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 12.0);
    cairo_text_extents_t extents{};
    cairo_text_extents(cr, valueText.c_str(), &extents);
    cairo_move_to(cr, centerX - extents.width * 0.5,
                  static_cast<double>(height) - 8.0);
    cairo_show_text(cr, valueText.c_str());
    return FALSE;
}

gboolean panDialButtonPress(GtkWidget*, GdkEventButton* event, gpointer data) {
    if (event->button != GDK_BUTTON_PRIMARY) return FALSE;
    auto& dial = *static_cast<PanDial*>(data);
    dial.dragging = true;
    dial.dragY = event->y;
    dial.dragValue = dial.value;
    return TRUE;
}

gboolean panDialMotion(GtkWidget*, GdkEventMotion* event, gpointer data) {
    auto& dial = *static_cast<PanDial*>(data);
    if (!dial.dragging) return FALSE;
    setPanDialValue(dial, dial.dragValue + (dial.dragY - event->y) / 80.0);
    return TRUE;
}

gboolean panDialButtonRelease(GtkWidget*, GdkEventButton* event,
                              gpointer data) {
    if (event->button != GDK_BUTTON_PRIMARY) return FALSE;
    static_cast<PanDial*>(data)->dragging = false;
    return TRUE;
}

gboolean panDialScroll(GtkWidget*, GdkEventScroll* event, gpointer data) {
    auto& dial = *static_cast<PanDial*>(data);
    const auto amount = event->direction == GDK_SCROLL_UP ? 0.02
        : event->direction == GDK_SCROLL_DOWN ? -0.02 : 0.0;
    if (amount == 0.0) return FALSE;
    setPanDialValue(dial, dial.value + amount);
    return TRUE;
}

void editGainFromDialog(GtkDialog*, gint response, gpointer data) {
    auto* context = static_cast<GainDialogContext*>(data);
    if (response == GTK_RESPONSE_ACCEPT &&
        context->node < context->view->nodes.size()) {
        auto& node = context->view->nodes[context->node];
        node.gainDb = gtk_range_get_value(context->gain);
        node.pan = context->pan.value;
#if defined(TRANSMISSION_UI_WITH_JACK) && defined(TRANSMISSION_UI_WITH_VST3)
        if (runtimeRunning(*context->view) && context->view->runtime) {
            std::string error;
            const auto gainNormalized =
                (node.gainDb - transmission::GainProcessor::minimumGainDb) /
                (transmission::GainProcessor::maximumGainDb -
                 transmission::GainProcessor::minimumGainDb);
            if (!context->view->runtime->setParameter(
                    node.id, transmission::GainProcessor::gainParameterId,
                    gainNormalized, error) ||
                !context->view->runtime->setParameter(
                    node.id, transmission::GainProcessor::panParameterId,
                    (node.pan + 1.0) * 0.5, error))
                setStatus(*context->view, error, true);
        }
#endif
        gtk_widget_queue_draw(context->canvas);
    }
    gtk_widget_destroy(context->dialog);
    delete context;
}

void showGainDialog(GtkWidget* canvas, GraphView& view,
                    std::size_t nodeIndex) {
    if (nodeIndex >= view.nodes.size() ||
        view.nodes[nodeIndex].kind != NodeKind::Gain)
        return;
    const auto& node = view.nodes[nodeIndex];
    auto* dialog = gtk_dialog_new_with_buttons(
        "Edit Gain / Pan", GTK_WINDOW(gtk_widget_get_toplevel(canvas)),
        static_cast<GtkDialogFlags>(
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        "_Cancel", GTK_RESPONSE_CANCEL, "_Apply", GTK_RESPONSE_ACCEPT,
        nullptr);
    auto* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 16);
    gtk_box_set_spacing(GTK_BOX(content), 12);

    auto* gainLabel = gtk_label_new("Gain");
    gtk_widget_set_halign(gainLabel, GTK_ALIGN_START);
    auto* gain = GTK_RANGE(gtk_scale_new_with_range(
        GTK_ORIENTATION_HORIZONTAL,
        transmission::GainProcessor::minimumGainDb,
        transmission::GainProcessor::maximumGainDb, 0.1));
    gtk_scale_set_digits(GTK_SCALE(gain), 1);
    gtk_scale_set_value_pos(GTK_SCALE(gain), GTK_POS_RIGHT);
    gtk_range_set_value(gain, node.gainDb);
    gtk_widget_set_size_request(GTK_WIDGET(gain), 420, -1);
    gtk_box_pack_start(GTK_BOX(content), gainLabel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), GTK_WIDGET(gain), FALSE, FALSE, 0);

    auto* panLabel = gtk_label_new("Pan / stereo balance");
    gtk_widget_set_halign(panLabel, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(content), panLabel, FALSE, FALSE, 0);
    auto* context = new GainDialogContext{
        &view, canvas, dialog, gain, {}, nodeIndex};
    context->pan.widget = gtk_drawing_area_new();
    setPanDialValue(context->pan, node.pan);
    gtk_widget_set_size_request(context->pan.widget, 130, 120);
    gtk_widget_set_halign(context->pan.widget, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(
        context->pan.widget,
        "Drag vertically or use the mouse wheel; center preserves stereo balance");
    gtk_widget_add_events(
        context->pan.widget,
        GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
        GDK_POINTER_MOTION_MASK | GDK_SCROLL_MASK);
    g_signal_connect(context->pan.widget, "draw",
                     G_CALLBACK(drawPanDial), &context->pan);
    g_signal_connect(context->pan.widget, "button-press-event",
                     G_CALLBACK(panDialButtonPress), &context->pan);
    g_signal_connect(context->pan.widget, "motion-notify-event",
                     G_CALLBACK(panDialMotion), &context->pan);
    g_signal_connect(context->pan.widget, "button-release-event",
                     G_CALLBACK(panDialButtonRelease), &context->pan);
    g_signal_connect(context->pan.widget, "scroll-event",
                     G_CALLBACK(panDialScroll), &context->pan);
    gtk_box_pack_start(GTK_BOX(content), context->pan.widget,
                       FALSE, FALSE, 0);
    g_signal_connect(dialog, "response", G_CALLBACK(editGainFromDialog),
                     context);
    gtk_widget_show_all(dialog);
}

std::string midiMappingParameterLabel(
    const MidiMappingDialogContext& context, std::uint32_t parameterId) {
    const auto parameter = std::find_if(
        context.parameters.begin(), context.parameters.end(),
        [&](const auto& candidate) { return candidate.id == parameterId; });
    return parameter == context.parameters.end()
        ? "Parameter " + std::to_string(parameterId) : parameter->label;
}

void refreshMidiMappingList(MidiMappingDialogContext& context) {
    gtk_list_store_clear(context.store);
    for (std::size_t index = 0; index < context.mappings.size(); ++index) {
        const auto& mapping = context.mappings[index];
        GtkTreeIter row;
        gtk_list_store_append(context.store, &row);
        const auto parameter = midiMappingParameterLabel(
            context, mapping.parameterId);
        const auto channel = mapping.channel < 0
            ? std::string("Any") : std::to_string(mapping.channel + 1);
        gtk_list_store_set(
            context.store, &row, 0, parameter.c_str(), 1, channel.c_str(),
            2, static_cast<guint>(mapping.controller), 3, mapping.consume,
            4, static_cast<guint>(index), -1);
    }
}

void addMidiMapping(GtkButton*, gpointer data) {
    auto& context = *static_cast<MidiMappingDialogContext*>(data);
    const auto parameterIndex = gtk_combo_box_get_active(
        GTK_COMBO_BOX(context.parameter));
    const auto channelIndex = gtk_combo_box_get_active(
        GTK_COMBO_BOX(context.channel));
    if (parameterIndex < 0 || channelIndex < 0 ||
        static_cast<std::size_t>(parameterIndex) >= context.parameters.size())
        return;
    const auto& node = context.view->nodes[context.node];
    transmission::UiProjectMidiParameterMapping mapping{
        node.id, context.parameters[static_cast<std::size_t>(parameterIndex)].id,
        channelIndex - 1,
        static_cast<std::uint8_t>(gtk_spin_button_get_value_as_int(
            context.controller)),
        gtk_toggle_button_get_active(context.consume) != FALSE};
    const auto existing = std::find_if(
        context.mappings.begin(), context.mappings.end(),
        [&](const auto& candidate) {
            return candidate.parameterId == mapping.parameterId &&
                   candidate.channel == mapping.channel &&
                   candidate.controller == mapping.controller;
        });
    if (existing == context.mappings.end())
        context.mappings.push_back(std::move(mapping));
    else
        existing->consume = mapping.consume;
    refreshMidiMappingList(context);
}

void removeMidiMapping(GtkButton*, gpointer data) {
    auto& context = *static_cast<MidiMappingDialogContext*>(data);
    GtkTreeModel* model = nullptr;
    GtkTreeIter row;
    if (!gtk_tree_selection_get_selected(
            gtk_tree_view_get_selection(context.list), &model, &row))
        return;
    guint index = 0;
    gtk_tree_model_get(model, &row, 4, &index, -1);
    if (index < context.mappings.size())
        context.mappings.erase(context.mappings.begin() + index);
    refreshMidiMappingList(context);
}

void applyMidiMappings(GtkDialog*, gint response, gpointer data) {
    auto* context = static_cast<MidiMappingDialogContext*>(data);
    if (response == GTK_RESPONSE_ACCEPT &&
        context->node < context->view->nodes.size()) {
        const auto targetId = context->view->nodes[context->node].id;
        stopRuntime(*context->view,
                    "MIDI mappings changed — press Play to compile and start audio");
        auto& mappings = context->view->midiMappings;
        mappings.erase(
            std::remove_if(mappings.begin(), mappings.end(),
                           [&](const auto& mapping) {
                               return mapping.targetNodeId == targetId;
                           }),
            mappings.end());
        mappings.insert(mappings.end(), context->mappings.begin(),
                        context->mappings.end());
        gtk_widget_queue_draw(context->canvas);
    }
    gtk_widget_destroy(context->dialog);
    delete context;
}

void showMidiMappingDialog(GtkWidget* canvas, GraphView& view,
                           std::size_t nodeIndex) {
    if (nodeIndex >= view.nodes.size()) return;
    const auto& node = view.nodes[nodeIndex];
    if (node.kind != NodeKind::Gain && node.kind != NodeKind::Plugin) return;

    std::vector<MidiMappingParameter> parameters;
    if (node.kind == NodeKind::Gain) {
        parameters = {
            {transmission::GainProcessor::gainParameterId, "Gain"},
            {transmission::GainProcessor::panParameterId,
             "Pan / stereo balance"}};
    } else {
        transmission::Vst3PluginTopology topology;
        std::string error;
        if (!transmission::Vst3Inspector().inspectTopology(
                node.pluginPath, topology, error)) {
            setStatus(view, "Unable to inspect " + node.label + ": " + error,
                      true);
            return;
        }
        for (const auto& parameter : topology.parameters) {
            if ((parameter.flags & vst3CanAutomateFlag) == 0) continue;
            const auto label = !parameter.title.empty()
                ? parameter.title
                : (!parameter.shortTitle.empty()
                       ? parameter.shortTitle
                       : "Parameter " + std::to_string(parameter.id));
            parameters.push_back({parameter.id, label});
        }
    }
    if (parameters.empty()) {
        setStatus(view, node.label + " has no automatable parameters", true);
        return;
    }

    auto* dialog = gtk_dialog_new_with_buttons(
        ("MIDI mappings — " + node.label).c_str(),
        GTK_WINDOW(gtk_widget_get_toplevel(canvas)),
        static_cast<GtkDialogFlags>(
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        "_Cancel", GTK_RESPONSE_CANCEL, "_Apply", GTK_RESPONSE_ACCEPT,
        nullptr);
    auto* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 16);
    gtk_box_set_spacing(GTK_BOX(content), 10);
    auto* hint = gtk_label_new(
        "Connect a MIDI Input to this node, then map its channel and CC.");
    gtk_widget_set_halign(hint, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(content), hint, FALSE, FALSE, 0);

    auto* controls = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(controls), 8);
    gtk_grid_set_row_spacing(GTK_GRID(controls), 6);
    auto* parameter = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
    for (const auto& item : parameters)
        gtk_combo_box_text_append_text(parameter, item.label.c_str());
    gtk_combo_box_set_active(GTK_COMBO_BOX(parameter), 0);
    auto* channel = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
    gtk_combo_box_text_append_text(channel, "Any channel");
    for (int current = 1; current <= 16; ++current)
        gtk_combo_box_text_append_text(
            channel, ("Channel " + std::to_string(current)).c_str());
    gtk_combo_box_set_active(GTK_COMBO_BOX(channel), 0);
    auto* controller = GTK_SPIN_BUTTON(
        gtk_spin_button_new_with_range(0, 127, 1));
    auto* consume = GTK_TOGGLE_BUTTON(gtk_check_button_new_with_label(
        "Consume mapped CC (do not also send it to the plugin)"));
    gtk_toggle_button_set_active(consume, TRUE);
    gtk_grid_attach(GTK_GRID(controls), gtk_label_new("Parameter"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(controls), GTK_WIDGET(parameter), 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(controls), gtk_label_new("MIDI channel"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(controls), GTK_WIDGET(channel), 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(controls), gtk_label_new("CC number"), 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(controls), GTK_WIDGET(controller), 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(controls), GTK_WIDGET(consume), 0, 3, 2, 1);
    gtk_box_pack_start(GTK_BOX(content), controls, FALSE, FALSE, 0);

    auto* store = gtk_list_store_new(
        5, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_BOOLEAN,
        G_TYPE_UINT);
    auto* list = GTK_TREE_VIEW(
        gtk_tree_view_new_with_model(GTK_TREE_MODEL(store)));
    const std::array<std::pair<const char*, int>, 4> columns{{
        {"Parameter", 0}, {"Channel", 1}, {"CC", 2}, {"Consume", 3}}};
    for (const auto& [title, column] : columns) {
        auto* renderer = column == 3 ? gtk_cell_renderer_toggle_new()
                                     : gtk_cell_renderer_text_new();
        gtk_tree_view_append_column(
            list, gtk_tree_view_column_new_with_attributes(
                      title, renderer, column == 3 ? "active" : "text",
                      column, nullptr));
    }
    gtk_widget_set_size_request(GTK_WIDGET(list), 520, 180);
    gtk_box_pack_start(GTK_BOX(content), GTK_WIDGET(list), TRUE, TRUE, 0);
    auto* buttons = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(buttons), GTK_BUTTONBOX_END);
    auto* add = gtk_button_new_with_label("Add mapping");
    auto* remove = gtk_button_new_with_label("Remove selected");
    gtk_container_add(GTK_CONTAINER(buttons), add);
    gtk_container_add(GTK_CONTAINER(buttons), remove);
    gtk_box_pack_start(GTK_BOX(content), buttons, FALSE, FALSE, 0);

    auto* context = new MidiMappingDialogContext{
        &view, canvas, dialog, parameter, channel, controller, consume, store,
        list, nodeIndex, std::move(parameters), {}};
    for (const auto& mapping : view.midiMappings) {
        if (mapping.targetNodeId == node.id)
            context->mappings.push_back(mapping);
    }
    refreshMidiMappingList(*context);
    g_signal_connect(add, "clicked", G_CALLBACK(addMidiMapping), context);
    g_signal_connect(remove, "clicked", G_CALLBACK(removeMidiMapping), context);
    g_signal_connect(dialog, "response", G_CALLBACK(applyMidiMappings), context);
    g_object_unref(store);
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
                context->view->nodes.push_back({
                    id, context->input ? "MIDI Input"
                                       : midiOutputLabel(selected),
                    context->input ? NodeKind::MidiInput : NodeKind::MidiOutput,
                    0, 0, context->input ? 0U : 1U, context->input ? 1U : 0U,
                    context->view->pointerX, context->view->pointerY,
                    "", selected, {}, {}});
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

void addGainActivated(GtkMenuItem*, gpointer data) {
    auto* context = static_cast<AddNodeMenuContext*>(data);
    auto& view = *context->view;
    stopRuntime(view, "Graph changed — press Play to compile and start audio");
    const auto id = "gain-" + std::to_string(view.nextGainId++);
    view.nodes.push_back({
        id, "Gain / Pan", NodeKind::Gain, 2, 2, 1, 0,
        view.pointerX, view.pointerY, "", "", {}, {}, 0.0, 0.0});
    gtk_widget_queue_draw(context->canvas);
}

void addMidiInputActivated(GtkMenuItem*, gpointer data) {
    auto* context = static_cast<AddNodeMenuContext*>(data);
    showMidiDialog(context->canvas, *context->view, true);
}

void addMidiOutputActivated(GtkMenuItem*, gpointer data) {
    auto* context = static_cast<AddNodeMenuContext*>(data);
    showMidiDialog(context->canvas, *context->view, false);
}

void pickClipFile(GtkWidget* canvas, GraphView& view, bool audio) {
    const char* title = audio ? "Choose Audio File (WAV)" : "Choose MIDI File";
    auto* dialog = gtk_file_chooser_dialog_new(
        title, GTK_WINDOW(gtk_widget_get_toplevel(canvas)),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Open", GTK_RESPONSE_ACCEPT, nullptr);
    auto* filter = gtk_file_filter_new();
    if (audio) {
        gtk_file_filter_set_name(filter, "WAV files (*.wav)");
        gtk_file_filter_add_pattern(filter, "*.wav");
        gtk_file_filter_add_pattern(filter, "*.WAV");
    } else {
        gtk_file_filter_set_name(filter, "MIDI files (*.mid)");
        gtk_file_filter_add_pattern(filter, "*.mid");
        gtk_file_filter_add_pattern(filter, "*.midi");
        gtk_file_filter_add_pattern(filter, "*.MID");
    }
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        gchar* selected = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        if (selected) {
            const std::string filePath(selected);
            g_free(selected);
            const auto stem = std::filesystem::path(filePath).stem().string();
            stopRuntime(view, "Graph changed — press Play to compile and start audio");
            if (audio) {
                const auto id = "audio-clip-" + std::to_string(view.nextAudioClipId++);
                view.nodes.push_back({
                    id, stem.empty() ? "Audio Clip" : stem,
                    NodeKind::AudioClip, 0, 2, 0, 0,
                    view.pointerX, view.pointerY, filePath, "", {}, {}});
            } else {
                // Parse MIDI file to count tracks
                std::string smfError;
                auto smf = transmission::loadSmf(filePath);
                const std::size_t trackCount = smf.error.empty() && smf.trackCount > 0
                    ? smf.trackCount : 1;
                const auto id = "midi-clip-" + std::to_string(view.nextMidiClipId++);
                view.nodes.push_back({
                    id, stem.empty() ? "MIDI Clip" : stem,
                    NodeKind::MidiClip, 0, 0, 0, static_cast<std::size_t>(trackCount),
                    view.pointerX, view.pointerY, filePath, "", {}, {}});
            }
            gtk_widget_queue_draw(canvas);
        }
    }
    gtk_widget_destroy(dialog);
}

void addAudioClipActivated(GtkMenuItem*, gpointer data) {
    auto* context = static_cast<AddNodeMenuContext*>(data);
    pickClipFile(context->canvas, *context->view, true);
}

void addMidiClipActivated(GtkMenuItem*, gpointer data) {
    auto* context = static_cast<AddNodeMenuContext*>(data);
    pickClipFile(context->canvas, *context->view, false);
}

void showAddNodeMenu(GtkWidget* canvas, GraphView& view,
                     GdkEventButton* event) {
    auto* menu = gtk_menu_new();
    auto* plugin = gtk_menu_item_new_with_label("Add VST3 Plugin…");
    auto* gain = gtk_menu_item_new_with_label("Add Gain / Pan");
    auto* midiInput = gtk_menu_item_new_with_label("Add MIDI Input…");
    auto* midiOutput = gtk_menu_item_new_with_label("Add MIDI Output…");
    auto* audioClip = gtk_menu_item_new_with_label("Add Audio Clip…");
    auto* midiClip = gtk_menu_item_new_with_label("Add MIDI Clip…");
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), plugin);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gain);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), midiInput);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), midiOutput);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), audioClip);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), midiClip);
    auto* context = new AddNodeMenuContext{&view, canvas};
    g_signal_connect(plugin, "activate", G_CALLBACK(addPluginActivated), context);
    g_signal_connect(gain, "activate", G_CALLBACK(addGainActivated), context);
    g_signal_connect(midiInput, "activate", G_CALLBACK(addMidiInputActivated), context);
    g_signal_connect(midiOutput, "activate", G_CALLBACK(addMidiOutputActivated), context);
    g_signal_connect(audioClip, "activate", G_CALLBACK(addAudioClipActivated), context);
    g_signal_connect(midiClip, "activate", G_CALLBACK(addMidiClipActivated), context);
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
            ? view.nodes[edge.from].kind == NodeKind::SystemInput && edge.fromPort == channel
            : view.nodes[edge.to].kind == NodeKind::SystemOutput && edge.toPort == channel;
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
        const auto port = "transmission:out_" + std::to_string(index + 1);
        if (!view.jackConnections->connectOutput(index, view.systemOutputConnections[index],
                                                 routeError)) {
            appendError("output " + std::to_string(index + 1) + ": " + routeError);
        } else {
            const auto actual = view.jackConnections->portConnections(port);
            logConsole(view, port + " → " +
                (actual.empty() ? "(none)" : actual.front()));
        }
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
        view.lastConnectionError.clear();
        logConsole(view, "JACK connections established");
        return G_SOURCE_REMOVE;
    }
    ++view.externalConnectionAttempts;
    if (!error.empty() && view.externalConnectionAttempts == 1)
        logConsole(view, "[warn] JACK routing pending: " + error);
    if (view.externalConnectionAttempts >= 20) {
        view.externalConnectionTimer = 0;
        view.externalConnectionAttempts = 0;
        std::string detail = error;
        if (view.jackConnections) {
            const auto outputs = view.jackConnections->outputDestinations();
            if (!outputs.empty()) {
                detail += " | available outputs:";
                for (const auto& p : outputs) detail += " " + p;
            }
        }
        view.lastConnectionError = detail;
        setStatus(view, "Audio is running, but JACK routing failed: " + detail, true);
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
    if (!jackPorts.empty()) {
        std::string resolvedText = "Resolved JACK ports:";
        for (std::size_t index = 0; index < connections.size(); ++index) {
            const auto resolved = transmission::resolveJackPortName(
                connections[index], jackPorts);
            resolvedText += "\n  Ch " + std::to_string(index + 1) + ": ";
            if (resolved == connections[index])
                resolvedText += resolved;
            else
                resolvedText += connections[index] + "  →  " + resolved;
        }
        auto* resolvedLabel = gtk_label_new(resolvedText.c_str());
        gtk_label_set_xalign(GTK_LABEL(resolvedLabel), 0.0F);
        gtk_label_set_line_wrap(GTK_LABEL(resolvedLabel), TRUE);
        gtk_widget_set_margin_top(resolvedLabel, 8);
        gtk_box_pack_start(GTK_BOX(content), resolvedLabel, FALSE, FALSE, 0);
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
    logConsole(view, "Audio started — waiting for JACK connections");
    startTransportTimer(view);
    if (!bufferWarning.empty())
        setStatus(view, "Audio started, but " + bufferWarning, true);
#else
    setStatus(view, "This UI build requires both JACK and VST3 support", true);
#endif
    updateTransportDisplay(view);
}

gboolean connectionWatchTick(gpointer data) {
    auto& view = *static_cast<GraphView*>(data);
    if (!view.jackConnections || !view.jackConnections->available()) return G_SOURCE_CONTINUE;
    for (std::size_t i = 0; i < 2; ++i) {
        const auto port = "transmission:out_" + std::to_string(i + 1);
        const auto conns = view.jackConnections->portConnections(port);
        const auto current = conns.empty() ? "(none)" : conns.front();
        if (current != view.lastWatchedConnections[i]) {
            logConsole(view, "[watch] " + port + ": " +
                (view.lastWatchedConnections[i].empty() ? "?" : view.lastWatchedConnections[i]) +
                " → " + current);
            view.lastWatchedConnections[i] = current;
        }
    }
    return G_SOURCE_CONTINUE;
}

std::size_t scanPlugins(GraphView&);

void consoleCommandActivated(GtkEntry* entry, gpointer data) {
    auto& view = *static_cast<GraphView*>(data);
    const gchar* raw = gtk_entry_get_text(entry);
    std::string cmd(raw ? raw : "");
    gtk_entry_set_text(entry, "");
    // Trim leading/trailing whitespace
    const auto start = cmd.find_first_not_of(" \t");
    if (start == std::string::npos) return;
    const auto end = cmd.find_last_not_of(" \t");
    cmd = cmd.substr(start, end - start + 1);
    logConsole(view, "> " + cmd);
    if (cmd == "help" || cmd == "?") {
        logConsole(view, "Commands: status  diag  lsp  connections  peaks  watch  unwatch  reconnect  scan  parse [path]  clear  help");
    } else if (cmd == "clear") {
        if (view.consoleTextView) {
            auto* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view.consoleTextView));
            gtk_text_buffer_set_text(buffer, "", 0);
        }
    } else if (cmd == "status") {
        const bool jackOk = view.jackConnections && view.jackConnections->available();
        logConsole(view, std::string("JACK: ") + (jackOk ? "available" : "not available"));
        logConsole(view, std::string("Runtime: ") + (runtimeRunning(view) ? "running" : "stopped"));
        logConsole(view, "Output ports: " + view.systemOutputConnections[0] +
                         ", " + view.systemOutputConnections[1]);
        logConsole(view, "Input ports: " + view.systemInputConnections[0] +
                         ", " + view.systemInputConnections[1]);
        if (!view.lastConnectionError.empty())
            logConsole(view, "[error] Last connection error: " + view.lastConnectionError);
        else if (runtimeRunning(view))
            logConsole(view, "Connection status: OK");
    } else if (cmd == "lsp") {
        if (!view.jackConnections || !view.jackConnections->available()) {
            logConsole(view, "JACK not available");
        } else {
            logConsole(view, "Output destinations:");
            for (const auto& dest : view.jackConnections->outputDestinations())
                logConsole(view, "  " + dest);
            logConsole(view, "Input sources:");
            for (const auto& src : view.jackConnections->inputSources())
                logConsole(view, "  " + src);
        }
    } else if (cmd == "connections") {
        if (!view.jackConnections || !view.jackConnections->available()) {
            logConsole(view, "JACK not available");
        } else {
            for (std::size_t i = 1; i <= 2; ++i) {
                const auto port = "transmission:out_" + std::to_string(i);
                const auto connected = view.jackConnections->portConnections(port);
                if (connected.empty())
                    logConsole(view, port + " → (nothing)");
                else
                    for (const auto& dest : connected)
                        logConsole(view, port + " → " + dest);
            }
        }
    } else if (cmd == "watch") {
        if (!view.connectionWatchTimer) {
            view.lastWatchedConnections.fill("");
            view.connectionWatchTimer = g_timeout_add(2000, connectionWatchTick, &view);
            logConsole(view, "Watching output connections every 2 s. Type unwatch to stop.");
        } else {
            logConsole(view, "Already watching.");
        }
    } else if (cmd == "unwatch") {
        if (view.connectionWatchTimer) {
            g_source_remove(view.connectionWatchTimer);
            view.connectionWatchTimer = 0;
            logConsole(view, "Watch stopped.");
        } else {
            logConsole(view, "Not watching.");
        }
    } else if (cmd == "diag") {
#if defined(TRANSMISSION_UI_WITH_JACK) && defined(TRANSMISSION_UI_WITH_VST3)
        if (!view.runtime) {
            logConsole(view, "Runtime not created");
        } else {
            const auto d = view.runtime->diagnostics();
            logConsole(view, std::string("running: ") + (d.running ? "yes" : "no"));
            logConsole(view, "processedBlocks: " + std::to_string(d.processedBlocks));
            logConsole(view, "positionBeats: " + std::to_string(d.positionBeats));
            logConsole(view, "midiEvents: " + std::to_string(d.midiEvents));
            logConsole(view, "underruns: " + std::to_string(d.underruns));
            logConsole(view, "renderAheadBlocks: " + std::to_string(d.renderAheadBlocks));
            logConsole(view, "graphFrames: " + std::to_string(d.graphFrames) +
                "  graphChannels: " + std::to_string(d.graphChannels) +
                "  lastCallbackFrames: " + std::to_string(d.lastCallbackFrames));
            logConsole(view, "avgRender: " + std::to_string(d.averageRenderMicroseconds) + " µs");
            for (const auto& t : view.runtime->processorTimings())
                logConsole(view, "  " + t.nodeId + ": " + std::to_string(t.calls) + " calls, " +
                    std::to_string(t.averageMicroseconds) + " µs avg");
        }
        if (!view.lastConnectionError.empty())
            logConsole(view, "[error] Connection error: " + view.lastConnectionError);
#else
        logConsole(view, "Not available in this build");
#endif
    } else if (cmd == "peaks") {
        const float l = view.outputPeakL.load(std::memory_order_relaxed);
        const float r = view.outputPeakR.load(std::memory_order_relaxed);
        logConsole(view, "Output peaks: L=" + std::to_string(l) + "  R=" + std::to_string(r) +
            (l < 0.001f && r < 0.001f ? "  (silence — check connections)" : ""));
    } else if (cmd == "reconnect") {
        if (!runtimeRunning(view)) {
            logConsole(view, "Reconnect: audio is not running");
        } else {
            std::string error;
            if (!applyExternalConnections(view, error))
                logConsole(view, "[error] " + error);
            else
                logConsole(view, "JACK ports reconnected");
        }
    } else if (cmd == "scan") {
        logConsole(view, "Scanning plugin paths...");
        scanPlugins(view);
        const int total = static_cast<int>(view.plugins.size());
        logConsole(view, std::to_string(total) + " bundle(s) found. Inspecting in background...");
        auto* job = new ConsoleScanJob{&view, view.inspectBinaryPath, view.plugins};
        std::thread(consoleScanThreadFunc, job).detach();
    } else if (cmd.starts_with("parse")) {
        const std::string arg = cmd.size() > 5 ? cmd.substr(6) : "";
        const std::string parseTarget = arg.empty() ? view.filePath : arg;
        if (parseTarget.empty()) {
            logConsole(view, "No project loaded. Usage: parse [path.ttl]");
        } else if (view.projectHelperPath.empty()) {
            logConsole(view, "Project helper not available");
        } else {
            logConsole(view, "Checking " + parseTarget + " ...");
            std::string output, error;
            if (runProjectHelper(view, "check", parseTarget, "", output, error)) {
                while (!output.empty() && (output.back() == '\n' || output.back() == '\r'))
                    output.pop_back();
                logConsole(view, output.empty() ? "OK" : output);
            } else {
                logConsole(view, "[error] " + error);
            }
        }
    } else {
        logConsole(view, "Unknown command. Type help for a list.");
    }
}

void showConsoleWindow(GraphView& view) {
    if (!view.consoleWindow) {
        auto* win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        gtk_window_set_title(GTK_WINDOW(win), "Transmission — Console");
        gtk_window_set_default_size(GTK_WINDOW(win), 700, 400);
        gtk_window_set_transient_for(GTK_WINDOW(win), GTK_WINDOW(view.window));

        auto* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        gtk_container_set_border_width(GTK_CONTAINER(vbox), 4);
        gtk_container_add(GTK_CONTAINER(win), vbox);

        // Scrolled text view for log output
        auto* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                       GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
        gtk_widget_set_vexpand(scrolled, TRUE);

        auto* textView = gtk_text_view_new();
        gtk_text_view_set_editable(GTK_TEXT_VIEW(textView), FALSE);
        gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(textView), FALSE);
        gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(textView), GTK_WRAP_WORD_CHAR);
        gtk_text_view_set_monospace(GTK_TEXT_VIEW(textView), TRUE);
        gtk_text_view_set_left_margin(GTK_TEXT_VIEW(textView), 4);
        gtk_text_view_set_right_margin(GTK_TEXT_VIEW(textView), 4);
        gtk_text_view_set_top_margin(GTK_TEXT_VIEW(textView), 4);
        gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(textView), 4);
        gtk_container_add(GTK_CONTAINER(scrolled), textView);
        gtk_box_pack_start(GTK_BOX(vbox), scrolled, TRUE, TRUE, 0);

        // Bottom input row: prompt label, entry, send button
        auto* hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        auto* promptLabel = gtk_label_new(">");
        gtk_box_pack_start(GTK_BOX(hbox), promptLabel, FALSE, FALSE, 0);

        auto* entry = gtk_entry_new();
        gtk_widget_set_hexpand(entry, TRUE);
        gtk_box_pack_start(GTK_BOX(hbox), entry, TRUE, TRUE, 0);

        auto* sendButton = gtk_button_new_with_label("Send");
        gtk_box_pack_start(GTK_BOX(hbox), sendButton, FALSE, FALSE, 0);

        gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

        view.consoleTextView = textView;
        view.consoleEntry = entry;
        view.consoleWindow = win;

        g_signal_connect(entry, "activate", G_CALLBACK(consoleCommandActivated), &view);
        g_signal_connect(sendButton, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
            auto* v = static_cast<GraphView*>(data);
            consoleCommandActivated(GTK_ENTRY(v->consoleEntry), data);
        }), &view);
        g_signal_connect(win, "delete-event", G_CALLBACK(+[](GtkWidget* w, GdkEvent*, gpointer) -> gboolean {
            gtk_widget_hide(w);
            return TRUE;
        }), nullptr);

        gtk_widget_show_all(win);
        logConsole(view, "Console ready. Commands: status  lsp  reconnect  parse [path]  clear  help");
    } else {
        gtk_window_present(GTK_WINDOW(view.consoleWindow));
    }
}

void showConsoleActivated(GtkMenuItem*, gpointer data) {
    showConsoleWindow(*static_cast<GraphView*>(data));
}

bool hasUnpositionedNodes(const GraphView& view) {
    for (const auto& node : view.nodes)
        if (node.x == 0.0 && node.y == 0.0) return true;
    return false;
}

void runAutolayout(GraphView& view) {
    const auto n = view.nodes.size();
    if (n == 0) return;

    // Assign each node a layer = longest path from any root
    std::vector<int> layer(n, 0);
    std::vector<int> inDegree(n, 0);
    std::vector<std::vector<std::size_t>> children(n);

    // Deduplicate edges per (from,to) pair for degree counting
    std::unordered_set<std::size_t> seen;
    for (const auto& edge : view.edges) {
        if (edge.from >= n || edge.to >= n) continue;
        const auto key = edge.from * 100000 + edge.to;
        if (seen.insert(key).second) {
            children[edge.from].push_back(edge.to);
            inDegree[edge.to]++;
        }
    }

    // Kahn-style layering: process sources first, push deepest layer to each child
    std::vector<std::size_t> queue;
    for (std::size_t i = 0; i < n; ++i)
        if (inDegree[i] == 0) queue.push_back(i);

    std::size_t head = 0;
    while (head < queue.size()) {
        const auto u = queue[head++];
        for (const auto v : children[u]) {
            if (layer[v] < layer[u] + 1) layer[v] = layer[u] + 1;
            if (--inDegree[v] == 0) queue.push_back(v);
        }
    }

    // Group by layer
    int maxLayer = *std::max_element(layer.begin(), layer.end());
    std::vector<std::vector<std::size_t>> columns(maxLayer + 1);
    for (std::size_t i = 0; i < n; ++i)
        columns[layer[i]].push_back(i);

    constexpr double xStart = 40.0;
    constexpr double yStart = 80.0;
    constexpr double xStep = nodeWidth + 40.0;
    constexpr double yGap = 30.0;
    constexpr double rowGap = 60.0;

    // Wrap columns into rows so layout fits within the visible canvas width
    const int canvasW = std::max(800, gtk_widget_get_allocated_width(view.canvas));
    const int colsPerRow = std::max(1, static_cast<int>((canvasW - xStart) / xStep));

    // Compute per-column height so we know the row band height
    std::vector<double> colHeight(columns.size());
    for (std::size_t c = 0; c < columns.size(); ++c)
        colHeight[c] = static_cast<double>(columns[c].size()) * (minimumNodeHeight + yGap);

    // Max height in each row band
    const int numRows = (static_cast<int>(columns.size()) + colsPerRow - 1) / colsPerRow;
    std::vector<double> rowBandHeight(numRows, 0.0);
    for (int c = 0; c < static_cast<int>(columns.size()); ++c)
        rowBandHeight[c / colsPerRow] = std::max(rowBandHeight[c / colsPerRow], colHeight[c]);

    for (int c = 0; c < static_cast<int>(columns.size()); ++c) {
        const int gridCol = c % colsPerRow;
        const int gridRow = c / colsPerRow;
        double yBase = yStart;
        for (int r = 0; r < gridRow; ++r)
            yBase += rowBandHeight[r] + rowGap;
        const double x = xStart + gridCol * xStep;
        double y = yBase;
        for (const auto idx : columns[c]) {
            view.nodes[idx].x = x;
            view.nodes[idx].y = y;
            y += minimumNodeHeight + yGap;
        }
    }

    gtk_widget_queue_draw(view.canvas);
}

void autolayoutActivated(GtkMenuItem*, gpointer data) {
    runAutolayout(*static_cast<GraphView*>(data));
}

void reconnectJackActivated(GtkMenuItem*, gpointer data) {
    auto& view = *static_cast<GraphView*>(data);
    if (!runtimeRunning(view)) {
        setStatus(view, "Reconnect JACK ports: audio is not running");
        return;
    }
    std::string error;
    if (!applyExternalConnections(view, error))
        setStatus(view, error, true);
    else
        setStatus(view, "JACK ports reconnected");
}

std::filesystem::path configFilePath() {
    const char* xdgConfig = std::getenv("XDG_CONFIG_HOME");
    const char* home = std::getenv("HOME");
    std::filesystem::path base = xdgConfig
        ? std::filesystem::path(xdgConfig)
        : std::filesystem::path(home ? home : ".") / ".config";
    return base / "transmission" / "config.ttl";
}

std::string expandHome(const std::string& path) {
    if (path.empty() || path[0] != '~') return path;
    const char* home = std::getenv("HOME");
    if (!home) return path;
    return std::string(home) + path.substr(1);
}

void loadConfig(GraphView& view) {
    std::ifstream f(configFilePath());
    std::string line;
    std::string paths;
#if defined(TRANSMISSION_UI_WITH_LIVE_SERVER)
    std::optional<bool> mcpServerEnabled;
#endif
    while (std::getline(f, line)) {
        if (line.rfind("PLUGIN_PATH\t", 0) == 0) {
            if (!paths.empty()) paths += '\n';
            paths += line.substr(12);
            continue;
        }
#if defined(TRANSMISSION_UI_WITH_LIVE_SERVER)
        if (line == "MCP_SERVER\ton") {
            mcpServerEnabled = true;
            continue;
        }
        if (line == "MCP_SERVER\toff") {
            mcpServerEnabled = false;
            continue;
        }
#endif
        if (line == "JACK_AUTOSTART\ton") {
            view.startJackOnStartup = true;
            continue;
        }
        if (line == "JACK_AUTOSTART\toff") {
            view.startJackOnStartup = false;
            continue;
        }
        if (line.rfind("JACK_START_CMD\t", 0) == 0) {
            view.jackStartCommand = line.substr(15);
            continue;
        }
    }
    if (!paths.empty()) view.pluginSearchPath = paths;
#if defined(TRANSMISSION_UI_WITH_LIVE_SERVER)
    if (mcpServerEnabled.has_value()) view.mcpServerEnabled = *mcpServerEnabled;
#endif
}

void saveConfig(const GraphView& view) {
    const auto path = configFilePath();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream f(path);
    std::istringstream ss(view.pluginSearchPath);
    std::string line;
    while (std::getline(ss, line))
        if (!line.empty()) f << "PLUGIN_PATH\t" << line << '\n';
#if defined(TRANSMISSION_UI_WITH_LIVE_SERVER)
    f << "MCP_SERVER\t" << (view.mcpServerEnabled ? "on" : "off") << '\n';
#endif
    f << "JACK_AUTOSTART\t" << (view.startJackOnStartup ? "on" : "off") << '\n';
    f << "JACK_START_CMD\t" << view.jackStartCommand << '\n';
}

std::size_t scanPlugins(GraphView& view) {
    view.plugins.clear();
    std::istringstream ss(view.pluginSearchPath);
    std::string searchLine;
    std::error_code ec;
    while (std::getline(ss, searchLine)) {
        if (searchLine.empty()) continue;
        const auto root = std::filesystem::path(expandHome(searchLine));
        if (!std::filesystem::is_directory(root)) continue;
        auto it = std::filesystem::recursive_directory_iterator(
            root, std::filesystem::directory_options::skip_permission_denied, ec);
        const decltype(it) end{};
        while (it != end) {
            if (it->is_directory(ec) && it->path().extension() == ".vst3") {
                const auto& p = it->path();
                view.plugins.push_back({p.string(), p.stem().string(), "", ""});
                it.disable_recursion_pending();
            }
            it.increment(ec);
        }
    }
    std::sort(view.plugins.begin(), view.plugins.end(),
        [](const auto& a, const auto& b) { return a.path < b.path; });
    view.plugins.erase(
        std::unique(view.plugins.begin(), view.plugins.end(),
            [](const auto& a, const auto& b) { return a.path == b.path; }),
        view.plugins.end());
    return view.plugins.size();
}


void pluginPathActivated(GtkMenuItem*, gpointer data) {
    auto& view = *static_cast<GraphView*>(data);
    constexpr gint kRescan = 1;
    auto* dialog = gtk_dialog_new_with_buttons(
        "Plugin Paths", GTK_WINDOW(view.window),
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Rescan", kRescan,
        "_OK", GTK_RESPONSE_ACCEPT,
        nullptr);
    auto* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 16);
    gtk_box_set_spacing(GTK_BOX(content), 8);
    auto* label = gtk_label_new("Directories to scan for VST3 plugins (one per line):");
    gtk_label_set_xalign(GTK_LABEL(label), 0.0F);
    gtk_box_pack_start(GTK_BOX(content), label, FALSE, FALSE, 0);
    auto* textView = gtk_text_view_new();
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(textView), TRUE);
    gtk_widget_set_size_request(textView, 480, 120);
    auto* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textView));
    gtk_text_buffer_set_text(buffer, view.pluginSearchPath.c_str(), -1);
    auto* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
        GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled), GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(scrolled), textView);
    gtk_box_pack_start(GTK_BOX(content), scrolled, TRUE, TRUE, 0);
    auto* countLabel = gtk_label_new(
        (std::to_string(view.plugins.size()) + " plugin(s) found").c_str());
    gtk_label_set_xalign(GTK_LABEL(countLabel), 0.0F);
    gtk_box_pack_start(GTK_BOX(content), countLabel, FALSE, FALSE, 0);
    gtk_widget_show_all(dialog);

    auto getTextPaths = [&]() -> std::string {
        GtkTextIter start, end;
        gtk_text_buffer_get_bounds(buffer, &start, &end);
        gchar* text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
        std::string result(text);
        g_free(text);
        return result;
    };

    while (true) {
        const gint response = gtk_dialog_run(GTK_DIALOG(dialog));
        if (response == GTK_RESPONSE_ACCEPT || response == kRescan) {
            view.pluginSearchPath = getTextPaths();
            const std::size_t found = scanPlugins(view);
            gtk_label_set_text(GTK_LABEL(countLabel),
                (std::to_string(found) + " plugin(s) found").c_str());
            if (response == GTK_RESPONSE_ACCEPT) {
                saveConfig(view);
                break;
            }
        } else {
            break;
        }
    }
    gtk_widget_destroy(dialog);
}

#if defined(TRANSMISSION_UI_WITH_LIVE_SERVER)
static bool pingLiveServer(GraphView& view);
static void launchLiveServer(GraphView& view);
void updateWindowTitle(GraphView& view);

void mcpServerSettingsActivated(GtkMenuItem*, gpointer data) {
    auto& view = *static_cast<GraphView*>(data);
    auto* dialog = gtk_dialog_new_with_buttons(
        "MCP Server", GTK_WINDOW(view.window),
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL |
                                   GTK_DIALOG_DESTROY_WITH_PARENT),
        "_Cancel", GTK_RESPONSE_CANCEL, "_Apply", GTK_RESPONSE_ACCEPT,
        nullptr);
    auto* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 16);
    gtk_box_set_spacing(GTK_BOX(content), 10);

    auto* enabled = GTK_TOGGLE_BUTTON(
        gtk_check_button_new_with_label("Enable live MCP server"));
    gtk_toggle_button_set_active(enabled, view.mcpServerEnabled);
    gtk_box_pack_start(GTK_BOX(content), GTK_WIDGET(enabled), FALSE, FALSE, 0);

    auto* explanation = gtk_label_new(
        "When enabled, Transmission connects to the live HTTP server used by the "
        "MCP bridge and launches it automatically if needed. Disable this to keep "
        "the GTK editor offline.");
    gtk_label_set_xalign(GTK_LABEL(explanation), 0.0F);
    gtk_label_set_line_wrap(GTK_LABEL(explanation), TRUE);
    gtk_widget_set_size_request(explanation, 460, -1);
    gtk_box_pack_start(GTK_BOX(content), explanation, FALSE, FALSE, 0);

    gtk_widget_show_all(dialog);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        const bool nextEnabled = gtk_toggle_button_get_active(enabled);
        if (nextEnabled != view.mcpServerEnabled) {
            view.mcpServerEnabled = nextEnabled;
            saveConfig(view);
            if (!view.mcpServerEnabled) {
                view.liveServerAvailable = false;
                view.liveServerRevision = -1;
                view.liveServerGeneration = -1;
                view.liveServerFilePath.clear();
                if (view.liveServerProcess) {
                    g_subprocess_send_signal(view.liveServerProcess, SIGTERM);
                    g_object_unref(view.liveServerProcess);
                    view.liveServerProcess = nullptr;
                }
                updateWindowTitle(view);
                logConsole(view, "Live MCP server disabled");
            } else {
                if (pingLiveServer(view)) {
                    logConsole(view, "Connected to live server at " + view.liveServerUrl);
                    updateWindowTitle(view);
                } else {
                    launchLiveServer(view);
                }
            }
        }
    }
    gtk_widget_destroy(dialog);
}
#endif

void jackStartupSettingsActivated(GtkMenuItem*, gpointer data) {
    auto& view = *static_cast<GraphView*>(data);
    auto* dialog = gtk_dialog_new_with_buttons(
        "JACK Startup", GTK_WINDOW(view.window),
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL |
                                   GTK_DIALOG_DESTROY_WITH_PARENT),
        "_Cancel", GTK_RESPONSE_CANCEL, "_Apply", GTK_RESPONSE_ACCEPT,
        nullptr);
    auto* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 16);
    gtk_box_set_spacing(GTK_BOX(content), 10);

    auto* enableCheck = GTK_TOGGLE_BUTTON(
        gtk_check_button_new_with_label("Start JACK automatically on startup"));
    gtk_toggle_button_set_active(enableCheck, view.startJackOnStartup);
    gtk_box_pack_start(GTK_BOX(content), GTK_WIDGET(enableCheck), FALSE, FALSE, 0);

    auto* cmdRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    auto* cmdLabel = gtk_label_new("Command:");
    auto* cmdEntry = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_text(cmdEntry, view.jackStartCommand.c_str());
    gtk_widget_set_size_request(GTK_WIDGET(cmdEntry), 320, -1);
    gtk_box_pack_start(GTK_BOX(cmdRow), cmdLabel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(cmdRow), GTK_WIDGET(cmdEntry), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(content), cmdRow, FALSE, FALSE, 0);

    auto* note = gtk_label_new(
        "The command is run in the background at startup when JACK is not already\n"
        "running. Transmission waits 2 seconds for the server to become available.");
    gtk_label_set_xalign(GTK_LABEL(note), 0.0F);
    gtk_box_pack_start(GTK_BOX(content), note, FALSE, FALSE, 0);

    gtk_widget_show_all(dialog);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        view.startJackOnStartup =
            gtk_toggle_button_get_active(enableCheck);
        const gchar* cmd = gtk_entry_get_text(cmdEntry);
        if (cmd && *cmd) view.jackStartCommand = cmd;
        saveConfig(view);
    }
    gtk_widget_destroy(dialog);
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
    constexpr std::array<std::size_t, 5> renderDurations{0, 50, 100, 200, 500};
    gtk_combo_box_text_append_text(renderSelector, "Off");
    gtk_combo_box_text_append_text(renderSelector, "50 ms");
    gtk_combo_box_text_append_text(renderSelector, "100 ms");
    gtk_combo_box_text_append_text(renderSelector, "200 ms (recommended)");
    gtk_combo_box_text_append_text(renderSelector, "500 ms (high-load playback)");
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
        state, parameters,
        [&view, nodeId](const transmission::ProcessorState& liveState) {
            if (!liveState.component.empty())
                view.pluginStates[nodeId].component = liveState.component;
#if defined(TRANSMISSION_UI_WITH_JACK) && defined(TRANSMISSION_UI_WITH_VST3)
            if (runtimeRunning(view) && view.runtime) {
                std::string error;
                if (!view.runtime->setPluginState(nodeId, liveState, error))
                    std::cerr << "VST3 live state forwarding failed for "
                              << nodeId << ": " << error << "\n";
            }
#endif
        });
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
    } else if (node.kind == NodeKind::Gain) {
        showGainDialog(context->canvas, *context->view, context->node);
    } else if (node.kind == NodeKind::AudioClip || node.kind == NodeKind::MidiClip) {
        const bool isAudio = node.kind == NodeKind::AudioClip;
        const char* title = isAudio ? "Replace Audio File (WAV)" : "Replace MIDI File";
        auto* dialog = gtk_file_chooser_dialog_new(
            title, GTK_WINDOW(gtk_widget_get_toplevel(context->canvas)),
            GTK_FILE_CHOOSER_ACTION_OPEN,
            "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, nullptr);
        auto* filter = gtk_file_filter_new();
        if (isAudio) {
            gtk_file_filter_set_name(filter, "WAV files (*.wav)");
            gtk_file_filter_add_pattern(filter, "*.wav");
            gtk_file_filter_add_pattern(filter, "*.WAV");
        } else {
            gtk_file_filter_set_name(filter, "MIDI files (*.mid)");
            gtk_file_filter_add_pattern(filter, "*.mid");
            gtk_file_filter_add_pattern(filter, "*.midi");
            gtk_file_filter_add_pattern(filter, "*.MID");
        }
        gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
        if (!node.pluginPath.empty())
            gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dialog), node.pluginPath.c_str());
        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
            gchar* selected = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
            if (selected) {
                const std::string filePath(selected);
                g_free(selected);
                const auto stem = std::filesystem::path(filePath).stem().string();
                stopRuntime(*context->view,
                            "Graph changed — press Play to compile and start audio");
                node.pluginPath = filePath;
                node.label = stem.empty() ? node.label : stem;
                if (!isAudio) {
                    auto smf = transmission::loadSmf(filePath);
                    if (smf.error.empty() && smf.trackCount > 0)
                        node.midiOutputs = smf.trackCount;
                }
                gtk_widget_queue_draw(context->canvas);
            }
        }
        gtk_widget_destroy(dialog);
    } else if (!node.pluginPath.empty() && context->view->editorHost) {
        openPluginEditor(*context->view, node);
    }
}

void editMidiMappingsFromMenu(GtkMenuItem*, gpointer data) {
    auto* context = static_cast<NodeMenuContext*>(data);
    showMidiMappingDialog(context->canvas, *context->view, context->node);
}

void removeNodeFromMenu(GtkMenuItem*, gpointer data) {
    auto* context = static_cast<NodeMenuContext*>(data);
    auto& view = *context->view;
    if (context->node >= view.nodes.size()) return;
    stopRuntime(view, "Graph changed — press Play to compile and start audio");
    const auto removedId = view.nodes[context->node].id;
    view.parameterValues.erase(removedId);
    view.pluginStates.erase(removedId);
    view.gainLanes.erase(
        std::remove_if(
            view.gainLanes.begin(), view.gainLanes.end(),
            [&](const auto& lane) { return lane.targetNodeId == removedId; }),
        view.gainLanes.end());
    view.midiMappings.erase(
        std::remove_if(
            view.midiMappings.begin(), view.midiMappings.end(),
            [&](const auto& mapping) {
                return mapping.targetNodeId == removedId;
            }),
        view.midiMappings.end());
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
    GtkWidget* midiMappings = nullptr;
    if (nodeIndex < view.nodes.size() &&
        (view.nodes[nodeIndex].kind == NodeKind::Gain ||
         view.nodes[nodeIndex].kind == NodeKind::Plugin)) {
        midiMappings = gtk_menu_item_new_with_label("MIDI mappings…");
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), midiMappings);
    }
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), remove);
    auto* context = new NodeMenuContext{&view, canvas, nodeIndex};
    g_signal_connect(edit, "activate", G_CALLBACK(editNodeFromMenu), context);
    if (midiMappings)
        g_signal_connect(midiMappings, "activate",
                         G_CALLBACK(editMidiMappingsFromMenu), context);
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

static void updateCanvasSize(GraphView& view) {
    double maxX = 800.0, maxY = 400.0;
    for (const auto& node : view.nodes) {
        const auto ports = std::max(node.audioInputs + node.midiInputs,
                                    node.audioOutputs + node.midiOutputs);
        const auto h = std::max(minimumNodeHeight,
                                static_cast<double>(ports + 1) * portSpacing);
        maxX = std::max(maxX, node.x + nodeWidth + 120.0);
        maxY = std::max(maxY, node.y + h + 80.0);
    }
    gtk_widget_set_size_request(view.canvas,
        static_cast<int>(std::ceil(maxX * view.zoomLevel)),
        static_cast<int>(std::ceil(maxY * view.zoomLevel)));
}

gboolean scrollEvent(GtkWidget* widget, GdkEventScroll* event, gpointer data) {
    auto& view = *static_cast<GraphView*>(data);
    if (!(event->state & GDK_CONTROL_MASK)) return FALSE;
    double factor = 1.0;
    if (event->direction == GDK_SCROLL_UP)
        factor = 1.1;
    else if (event->direction == GDK_SCROLL_DOWN)
        factor = 1.0 / 1.1;
    else if (event->direction == GDK_SCROLL_SMOOTH)
        factor = event->delta_y < 0 ? 1.1 : 1.0 / 1.1;
    if (factor == 1.0) return FALSE;
    view.zoomLevel = std::clamp(view.zoomLevel * factor, 0.2, 4.0);
    updateCanvasSize(view);
    gtk_widget_queue_draw(widget);
    return TRUE;
}

gboolean keyPress(GtkWidget*, GdkEventKey* event, gpointer data) {
    auto& view = *static_cast<GraphView*>(data);
    if (!(event->state & GDK_CONTROL_MASK)) return FALSE;
    double newZoom = view.zoomLevel;
    if (event->keyval == GDK_KEY_equal || event->keyval == GDK_KEY_plus)
        newZoom = std::min(4.0, view.zoomLevel * 1.2);
    else if (event->keyval == GDK_KEY_minus)
        newZoom = std::max(0.2, view.zoomLevel / 1.2);
    else if (event->keyval == GDK_KEY_0)
        newZoom = 1.0;
    else return FALSE;
    view.zoomLevel = newZoom;
    updateCanvasSize(view);
    gtk_widget_queue_draw(view.canvas);
    return TRUE;
}

gboolean drawGraph(GtkWidget*, cairo_t* cr, gpointer data) {
    auto& view = *static_cast<GraphView*>(data);
    cairo_set_source_rgb(cr, 0.075, 0.085, 0.11);
    cairo_paint(cr);
    cairo_scale(cr, view.zoomLevel, view.zoomLevel);

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
        if (node.kind == NodeKind::Gain) {
            char details[64]{};
            const auto panText = std::fabs(node.pan) < 0.005
                ? std::string("C")
                : (node.pan < 0.0 ? "L " : "R ") +
                    std::to_string(static_cast<int>(
                        std::lround(std::fabs(node.pan) * 100.0)));
            g_snprintf(details, sizeof(details), "%.1f dB   Pan %s",
                       node.gainDb, panText.c_str());
            cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                                   CAIRO_FONT_WEIGHT_NORMAL);
            cairo_set_font_size(cr, 11.0);
            cairo_move_to(cr, node.x + 15.0, node.y + 52.0);
            cairo_show_text(cr, details);
        }
        if (node.kind == NodeKind::SystemOutput) {
            const float peakL = view.outputPeakL.load(std::memory_order_relaxed);
            const float peakR = view.outputPeakR.load(std::memory_order_relaxed);
            const double meterTop    = node.y + 42.0;
            const double meterBottom = node.y + height - 8.0;
            const double meterH      = meterBottom - meterTop;
            const double barW = 16.0;
            const double gapX = 6.0;
            const double originX = node.x + (nodeWidth - 2.0 * barW - gapX) / 2.0;
            const auto drawMeter = [&](double x, float peak) {
                // Background track
                cairo_set_source_rgba(cr, 0.08, 0.08, 0.10, 0.9);
                cairo_rectangle(cr, x, meterTop, barW, meterH);
                cairo_fill(cr);
                if (peak <= 0.001f) return;
                const double fillH = std::min(static_cast<double>(peak), 1.0) * meterH;
                const double fillY = meterBottom - fillH;
                // Colour: green → yellow → red
                double r, g, b;
                if (peak < 0.7f)      { r = 0.15; g = 0.85; b = 0.25; }
                else if (peak < 0.9f) { r = 0.95; g = 0.80; b = 0.10; }
                else                  { r = 0.95; g = 0.18; b = 0.12; }
                cairo_set_source_rgb(cr, r, g, b);
                cairo_rectangle(cr, x, fillY, barW, fillH);
                cairo_fill(cr);
                // Subtle border
                cairo_set_source_rgba(cr, 1, 1, 1, 0.15);
                cairo_set_line_width(cr, 0.75);
                cairo_rectangle(cr, x, meterTop, barW, meterH);
                cairo_stroke(cr);
            };
            drawMeter(originX,             peakL);
            drawMeter(originX + barW + gapX, peakR);
            // Labels
            cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
            cairo_set_font_size(cr, 9.0);
            cairo_set_source_rgba(cr, 0.7, 0.7, 0.7, 0.9);
            cairo_move_to(cr, originX + 4.0, meterTop - 3.0);
            cairo_show_text(cr, "L");
            cairo_move_to(cr, originX + barW + gapX + 4.0, meterTop - 3.0);
            cairo_show_text(cr, "R");
        }
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
    const double cx = event->x / view.zoomLevel;
    const double cy = event->y / view.zoomLevel;
    if (event->button == GDK_BUTTON_SECONDARY) {
        const auto edge = edgeAt(view, cx, cy);
        if (edge != static_cast<std::size_t>(-1)) {
            stopRuntime(view, "Graph changed — press Play to compile and start audio");
            view.edges.erase(view.edges.begin() + static_cast<std::ptrdiff_t>(edge));
            gtk_widget_queue_draw(widget);
            return TRUE;
        }
        if (auto* node = nodeAt(view, cx, cy)) {
            showNodeMenu(widget, view, static_cast<std::size_t>(node - view.nodes.data()), event);
        } else if (portAt(view, cx, cy).node == static_cast<std::size_t>(-1)) {
            showAddNodeMenu(widget, view, event);
        }
        return TRUE;
    }
    if (event->button != GDK_BUTTON_PRIMARY) return FALSE;
    if (event->type == GDK_2BUTTON_PRESS) {
        if (auto* node = nodeAt(view, cx, cy);
            node && !node->pluginPath.empty() && view.editorHost) {
            cancelPointerInteraction(widget, view);
            openPluginEditor(view, *node);
            return TRUE;
        }
        if (auto* node = nodeAt(view, cx, cy);
            node && (node->kind == NodeKind::SystemInput ||
                     node->kind == NodeKind::SystemOutput)) {
            cancelPointerInteraction(widget, view);
            showSystemDialog(widget, view, node->id == "system-input");
            return TRUE;
        }
        if (auto* node = nodeAt(view, cx, cy);
            node && (node->kind == NodeKind::MidiInput ||
                     node->kind == NodeKind::MidiOutput)) {
            cancelPointerInteraction(widget, view);
            showMidiDialog(
                widget, view, node->kind == NodeKind::MidiInput,
                static_cast<std::size_t>(node - view.nodes.data()));
            return TRUE;
        }
        if (auto* node = nodeAt(view, cx, cy);
            node && node->kind == NodeKind::Gain) {
            cancelPointerInteraction(widget, view);
            showGainDialog(
                widget, view,
                static_cast<std::size_t>(node - view.nodes.data()));
            return TRUE;
        }
    }
    const auto hit = portAt(view, cx, cy);
    if (hit.output) {
        view.connectingFrom = hit.node;
        view.connectingPort = hit.port;
        view.connectingKind = hit.kind;
        view.pointerX = cx;
        view.pointerY = cy;
        gtk_widget_queue_draw(widget);
        return TRUE;
    }
    auto* node = nodeAt(view, cx, cy);
    if (!node) return FALSE;
    view.dragging = static_cast<std::size_t>(node - view.nodes.data());
    view.dragX = cx - node->x;
    view.dragY = cy - node->y;
    gtk_widget_queue_draw(widget);
    return TRUE;
}

gboolean motion(GtkWidget* widget, GdkEventMotion* event, gpointer data) {
    auto& view = *static_cast<GraphView*>(data);
    view.pointerX = event->x / view.zoomLevel;
    view.pointerY = event->y / view.zoomLevel;
    if (view.connectingFrom != static_cast<std::size_t>(-1)) {
        gtk_widget_queue_draw(widget);
        return TRUE;
    }
    if (view.dragging == static_cast<std::size_t>(-1)) return FALSE;
    auto& node = view.nodes[view.dragging];
    node.x = std::max(20.0, event->x / view.zoomLevel - view.dragX);
    node.y = std::max(70.0, event->y / view.zoomLevel - view.dragY);
    updateCanvasSize(view);
    gtk_widget_queue_draw(widget);
    return TRUE;
}

gboolean buttonRelease(GtkWidget* widget, GdkEventButton* event, gpointer data) {
    if (event->button != GDK_BUTTON_PRIMARY) return FALSE;
    auto& view = *static_cast<GraphView*>(data);
    if (view.connectingFrom != static_cast<std::size_t>(-1)) {
        const auto hit = portAt(view, event->x / view.zoomLevel, event->y / view.zoomLevel);
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
#if defined(TRANSMISSION_UI_WITH_LIVE_SERVER)
    title += view.liveServerAvailable ? " [live]" : " [offline]";
#endif
    gtk_window_set_title(GTK_WINDOW(view.window), title.c_str());
}

#if defined(TRANSMISSION_UI_WITH_LIVE_SERVER)
static std::size_t curlWriteCallback(char* ptr, std::size_t size, std::size_t nmemb, void* userdata) {
    static_cast<std::string*>(userdata)->append(ptr, size * nmemb);
    return size * nmemb;
}

static std::string httpGet(const std::string& url) {
    auto* curl = curl_easy_init();
    if (!curl) return {};
    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 2000L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 500L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    const auto result = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    return result == CURLE_OK ? response : std::string{};
}

static bool httpPost(const std::string& url, const std::string& body,
                     const std::string& contentType, std::string& response) {
    auto* curl = curl_easy_init();
    if (!curl) return false;
    struct curl_slist* headers = nullptr;
    const auto ctHeader = "Content-Type: " + contentType;
    headers = curl_slist_append(headers, ctHeader.c_str());
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 5000L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 500L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    const auto result = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return result == CURLE_OK;
}

static int parseRevisionFromTurtle(const std::string& turtle) {
    const std::string marker = "trn:revision ";
    const auto pos = turtle.find(marker);
    if (pos == std::string::npos) return -1;
    try { return std::stoi(turtle.substr(pos + marker.size())); } catch (...) { return -1; }
}

static int parseGenerationFromTurtle(const std::string& turtle) {
    const std::string marker = "trn:generation ";
    const auto pos = turtle.find(marker);
    if (pos == std::string::npos) return -1;
    try { return std::stoi(turtle.substr(pos + marker.size())); } catch (...) { return -1; }
}

static std::string parseFilePathFromTurtle(const std::string& turtle) {
    const std::string key = "trn:filePath \"";
    const auto pos = turtle.find(key);
    if (pos == std::string::npos) return {};
    const auto start = pos + key.size();
    const auto end = turtle.find('"', start);
    if (end == std::string::npos) return {};
    return turtle.substr(start, end - start);
}

static int parseRevisionFromJson(const std::string& json) {
    const std::string marker = "\"revision\":";
    const auto pos = json.find(marker);
    if (pos == std::string::npos) return -1;
    try { return std::stoi(json.substr(pos + marker.size())); } catch (...) { return -1; }
}

static bool pingLiveServer(GraphView& view) {
    const auto status = httpGet(view.liveServerUrl + "/status");
    view.liveServerAvailable = !status.empty();
    // Leave liveServerRevision = -1 so the first poll tick triggers an initial project load.
    return view.liveServerAvailable;
}

static void syncToLiveServer(GraphView& view, const std::string& path) {
    if (!view.liveServerAvailable) return;
    const std::string body =
        "@prefix trn: <http://purl.org/stuff/transmissions/> .\n"
        "[] a trn:OpenProject ; trn:filePath \"" + path + "\" .\n";
    std::string response;
    if (httpPost(view.liveServerUrl + "/projects/open", body, "text/turtle", response)) {
        const int rev = parseRevisionFromJson(response);
        if (rev >= 0) view.liveServerRevision = rev;
        view.liveServerFilePath = path;
        logConsole(view, "Live server synced: " + path);
    } else {
        logConsole(view, "Warning: live server sync failed — server may have stopped");
        view.liveServerAvailable = false;
    }
    updateWindowTitle(view);
}

bool applyProject(GraphView&, const transmission::UiProject&, std::string&);
transmission::UiProject captureProject(const GraphView&);
void updateWindowTitle(GraphView& view);

static void launchLiveServer(GraphView& view) {
    // Resolve script path: prefer projectHelperPath, fall back to binary-relative location.
    std::string scriptPath;
    if (!view.projectHelperPath.empty()) {
        const auto candidate = (std::filesystem::path(view.projectHelperPath).parent_path()
                               / "transmission-live.js").string();
        if (std::filesystem::exists(candidate))
            scriptPath = candidate;
    }
    if (scriptPath.empty()) {
        char selfBuf[4096] = {};
        const ssize_t selfLen = readlink("/proc/self/exe", selfBuf, sizeof(selfBuf) - 1);
        if (selfLen > 0) {
            // binary is at <repo>/native/<build-dir>/transmission_graph_ui
            const auto candidate = (std::filesystem::path(selfBuf)
                                    .parent_path().parent_path().parent_path()
                                    / "scripts/transmission-live.js").string();
            if (std::filesystem::exists(candidate))
                scriptPath = candidate;
        }
    }
    if (scriptPath.empty()) {
        logConsole(view, "Live server script not found — set TRANSMISSION_ROOT or run from repo root");
        return;
    }
    // Resolve 'node' via PATH so nvm-managed installs are found even when PATH
    // is not fully inherited by the subprocess.
    gchar* nodeBin = g_find_program_in_path("node");
    if (!nodeBin) {
        logConsole(view, "Cannot find 'node' in PATH — live server unavailable");
        return;
    }
    GError* err = nullptr;
    // scripts/ is one level below the repo root; addon lives in native/build-napi-jack-vst3/
    const auto repoRoot = std::filesystem::path(scriptPath).parent_path().parent_path();
    const auto addonPath = (repoRoot / "native/build-napi-jack-vst3/transmission_native.node").string();
    const bool addonExists = std::filesystem::exists(addonPath);
    if (addonExists) {
        view.liveServerProcess = g_subprocess_new(
            G_SUBPROCESS_FLAGS_NONE, &err,
            nodeBin, scriptPath.c_str(),
            "--native-addon", addonPath.c_str(),
            "--jack", "--auto-connect", nullptr);
    } else {
        view.liveServerProcess = g_subprocess_new(
            G_SUBPROCESS_FLAGS_NONE, &err,
            nodeBin, scriptPath.c_str(), "--jack", "--auto-connect", nullptr);
    }
    g_free(nodeBin);
    if (!view.liveServerProcess) {
        logConsole(view, std::string("Could not launch live server: ") +
                   (err ? err->message : "unknown error"));
        g_clear_error(&err);
    } else {
        logConsole(view, "Live server launched (" + scriptPath + ")");
    }
}

static void reloadFromLiveServer(GraphView& view, const std::string& serverFilePath) {
    const auto turtle = httpGet(view.liveServerUrl + "/graph");
    if (turtle.empty() || turtle.find("trn:NoProject") != std::string::npos) return;
    const auto tempPath = (std::filesystem::temp_directory_path() /
        ("transmission-reload-" + std::to_string(::getpid()) + ".ttl")).string();
    GError* writeErr = nullptr;
    if (!g_file_set_contents(tempPath.c_str(), turtle.data(),
                              static_cast<gssize>(turtle.size()), &writeErr)) {
        logConsole(view, "Reload: failed to write temp file");
        g_clear_error(&writeErr);
        return;
    }
    std::string interchange, error;
    const bool ok = runProjectHelper(view, "load", tempPath, "", interchange, error);
    g_unlink(tempPath.c_str());
    if (!ok) { logConsole(view, "Reload: parse failed — " + error); return; }
    transmission::UiProject project;
    if (!transmission::decodeUiProject(interchange, project, error) ||
        !applyProject(view, project, error)) {
        logConsole(view, "Reload: apply failed — " + error);
        return;
    }
    if (hasUnpositionedNodes(view)) runAutolayout(view);
    if (!serverFilePath.empty()) view.filePath = serverFilePath;
    view.lastSavedSnapshot = transmission::encodeUiProject(captureProject(view));
    updateWindowTitle(view);
    logConsole(view, "Graph reloaded from live server");
}

static void updateJackIndicator(GraphView& view) {
    if (!view.jackIndicator) return;
    const bool ok = view.jackConnections && view.jackConnections->available();
    gtk_label_set_markup(GTK_LABEL(view.jackIndicator),
        ok ? "<span color=\"#44cc44\">● JACK</span>"
           : "<span color=\"#cc4444\">● JACK</span>");
}

static void updateMcpIndicator(GraphView& view) {
    if (!view.mcpIndicator) return;
#if defined(TRANSMISSION_UI_WITH_LIVE_SERVER)
    const bool ok = view.liveServerAvailable;
#else
    const bool ok = false;
#endif
    gtk_label_set_markup(GTK_LABEL(view.mcpIndicator),
        ok ? "<span color=\"#44cc44\">● MCP</span>"
           : "<span color=\"#cc4444\">● MCP</span>");
}

static gboolean liveServerPollTick(gpointer data) {
    auto& view = *static_cast<GraphView*>(data);
    if (!view.mcpServerEnabled) {
        if (view.liveServerAvailable) {
            view.liveServerAvailable = false;
            updateWindowTitle(view);
        }
        return G_SOURCE_CONTINUE;
    }
    const auto status = httpGet(view.liveServerUrl + "/status");
    if (status.empty()) {
        if (view.liveServerAvailable) {
            view.liveServerAvailable = false;
            updateWindowTitle(view);
            updateMcpIndicator(view);
            logConsole(view, "Live server disconnected");
        }
        // Auto-restart if the subprocess we own has exited (or was never spawned).
        const bool processGone = !view.liveServerProcess ||
            g_subprocess_get_if_exited(view.liveServerProcess);
        if (processGone) {
            if (view.liveServerProcess) {
                g_object_unref(view.liveServerProcess);
                view.liveServerProcess = nullptr;
            }
            launchLiveServer(view);
        }
        return G_SOURCE_CONTINUE;
    }
    if (!view.liveServerAvailable) {
        view.liveServerAvailable = true;
        updateWindowTitle(view);
        updateMcpIndicator(view);
        logConsole(view, "Live server reconnected at " + view.liveServerUrl);
    }
    const int serverRevision = parseRevisionFromTurtle(status);
    const int serverGeneration = parseGenerationFromTurtle(status);
    const std::string serverFilePath = parseFilePathFromTurtle(status);
    const bool generationChanged = serverGeneration >= 0 && serverGeneration != view.liveServerGeneration;
    const bool revisionChanged = serverRevision >= 0 && serverRevision != view.liveServerRevision;
    const bool projectChanged = !serverFilePath.empty() && serverFilePath != view.liveServerFilePath;
    if (generationChanged || revisionChanged || projectChanged) {
        if (view.liveServerRevision >= 0 && (generationChanged || revisionChanged))
            logConsole(view, "External edit detected (generation " +
                       std::to_string(view.liveServerGeneration) + " → " +
                       std::to_string(serverGeneration) + ", revision " +
                       std::to_string(view.liveServerRevision) + " → " +
                       std::to_string(serverRevision) + ")");
        reloadFromLiveServer(view, serverFilePath);
        view.liveServerRevision = serverRevision;
        view.liveServerGeneration = serverGeneration;
        view.liveServerFilePath = serverFilePath;
        updateWindowTitle(view);
    }
    return G_SOURCE_CONTINUE;
}
#endif

transmission::UiProject captureProject(const GraphView& view) {
    transmission::UiProject project;
    project.nodes.reserve(view.nodes.size());
    for (const auto& node : view.nodes) {
        const auto kind = static_cast<transmission::UiProjectNodeKind>(
            static_cast<int>(node.kind));
        project.nodes.push_back({
            node.id, node.label, kind,
            node.audioInputs, node.audioOutputs, node.midiInputs, node.midiOutputs,
            node.x, node.y, node.pluginPath, node.externalPort, {}, {}, {},
            node.gainDb, node.pan});
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
    project.renderAheadMilliseconds = view.renderAheadMilliseconds;
    project.requestedBufferSize = view.requestedBufferSize;
    project.processingThreads = view.processingThreads;
    project.tempo = gtk_spin_button_get_value(view.tempo);
    project.loopBars = gtk_spin_button_get_value(view.loopBars);
    project.loopEnabled = gtk_toggle_button_get_active(view.loop);
    project.arrangementLengthBeats = view.arrangementLengthBeats;
    project.midiClips = view.midiClips;
    project.gainLanes = view.gainLanes;
    project.midiMappings = view.midiMappings;
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
    for (const auto& mapping : project.midiMappings) {
        const auto target = nodes.find(mapping.targetNodeId);
        if (target == nodes.end() ||
            (target->second->kind != transmission::UiProjectNodeKind::Gain &&
             target->second->kind != transmission::UiProjectNodeKind::Plugin) ||
            mapping.channel < -1 || mapping.channel > 15 ||
            mapping.controller > 127) {
            error = "The project contains an invalid MIDI parameter mapping";
            return false;
        }
        if (target->second->kind == transmission::UiProjectNodeKind::Gain &&
            mapping.parameterId != transmission::GainProcessor::gainParameterId &&
            mapping.parameterId != transmission::GainProcessor::panParameterId) {
            error = "The project maps MIDI to an unknown Gain/Pan parameter";
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
        if (node.kind == transmission::UiProjectNodeKind::Gain) {
            node.midiInputs = std::max<std::size_t>(1, node.midiInputs);
            continue;
        }
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
        node.midiInputs = std::max<std::size_t>(1, topology.midiInputs);
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
            inputLabels[source.id], outputLabels[source.id], source.gainDb,
            source.pan});
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
    view.renderAheadMilliseconds = normalized.renderAheadMilliseconds;
    view.requestedBufferSize = normalized.requestedBufferSize;
    view.processingThreads = normalized.processingThreads;
    view.arrangementLengthBeats = normalized.arrangementLengthBeats;
    view.midiClips = normalized.midiClips;
    view.gainLanes = normalized.gainLanes;
    view.midiMappings = normalized.midiMappings;
    view.dragging = static_cast<std::size_t>(-1);
    view.connectingFrom = static_cast<std::size_t>(-1);
    view.nextPluginId = 1;
    view.nextMidiInputId = 1;
    view.nextMidiOutputId = 1;
    view.nextGainId = 1;
    view.nextAudioClipId = 1;
    view.nextMidiClipId = 1;
    std::unordered_set<std::string> identifiers;
    for (const auto& node : view.nodes) identifiers.insert(node.id);
    while (identifiers.contains("plugin-" + std::to_string(view.nextPluginId)))
        ++view.nextPluginId;
    while (identifiers.contains("midi-input-" + std::to_string(view.nextMidiInputId)))
        ++view.nextMidiInputId;
    while (identifiers.contains("midi-output-" + std::to_string(view.nextMidiOutputId)))
        ++view.nextMidiOutputId;
    while (identifiers.contains("gain-" + std::to_string(view.nextGainId)))
        ++view.nextGainId;
    while (identifiers.contains("audio-clip-" + std::to_string(view.nextAudioClipId)))
        ++view.nextAudioClipId;
    while (identifiers.contains("midi-clip-" + std::to_string(view.nextMidiClipId)))
        ++view.nextMidiClipId;
    gtk_spin_button_set_value(view.tempo, normalized.tempo);
    gtk_spin_button_set_value(view.loopBars, normalized.loopBars);
    gtk_toggle_button_set_active(view.loop, normalized.loopEnabled);
    updateTransportDisplay(view);
    updateCanvasSize(view);
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

// --- Recent files ---
static constexpr std::size_t kMaxRecentFiles = 5;

std::filesystem::path recentFilesPath() {
    const char* xdgConfig = std::getenv("XDG_CONFIG_HOME");
    const char* home = std::getenv("HOME");
    std::filesystem::path base = xdgConfig
        ? std::filesystem::path(xdgConfig)
        : std::filesystem::path(home ? home : ".") / ".config";
    return base / "transmission" / "recent-files.txt";
}

std::vector<std::string> loadRecentFiles() {
    std::vector<std::string> files;
    std::ifstream f(recentFilesPath());
    std::string line;
    while (std::getline(f, line) && files.size() < kMaxRecentFiles)
        if (!line.empty() && std::filesystem::exists(line))
            files.push_back(line);
    return files;
}

void saveRecentFiles(const std::vector<std::string>& files) {
    const auto path = recentFilesPath();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream f(path);
    for (const auto& file : files)
        f << file << '\n';
}

void addToRecentFiles(GraphView& view, const std::string& path) {
    auto& files = view.recentFiles;
    files.erase(std::remove(files.begin(), files.end(), path), files.end());
    files.insert(files.begin(), path);
    if (files.size() > kMaxRecentFiles)
        files.resize(kMaxRecentFiles);
    saveRecentFiles(files);
}

struct RecentItemData {
    GraphView* view;
    std::string path;
};

void rebuildRecentMenu(GraphView& view); // forward declaration

void openRecentFileActivated(GtkMenuItem*, gpointer data) {
    auto* rid = static_cast<RecentItemData*>(data);
    GraphView& view = *rid->view;
    const std::string path = rid->path;
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
            if (hasUnpositionedNodes(view)) runAutolayout(view);
            view.filePath = path;
            view.lastSavedSnapshot = transmission::encodeUiProject(captureProject(view));
            updateWindowTitle(view);
            addToRecentFiles(view, path);
            rebuildRecentMenu(view);
#if defined(TRANSMISSION_UI_WITH_LIVE_SERVER)
            syncToLiveServer(view, path);
#endif
        }
    }
}

void clearRecentFilesActivated(GtkMenuItem*, gpointer data) {
    auto& view = *static_cast<GraphView*>(data);
    view.recentFiles.clear();
    saveRecentFiles(view.recentFiles);
    rebuildRecentMenu(view);
}

void rebuildRecentMenu(GraphView& view) {
    gtk_container_foreach(GTK_CONTAINER(view.recentMenu),
        [](GtkWidget* child, gpointer) { gtk_widget_destroy(child); }, nullptr);
    if (view.recentFiles.empty()) {
        auto* item = gtk_menu_item_new_with_label("(No recent files)");
        gtk_widget_set_sensitive(item, FALSE);
        gtk_menu_shell_append(GTK_MENU_SHELL(view.recentMenu), item);
    } else {
        for (const auto& filePath : view.recentFiles) {
            const auto label = std::filesystem::path(filePath).filename().string();
            auto* item = gtk_menu_item_new_with_label(label.c_str());
            auto* rid = new RecentItemData{&view, filePath};
            g_signal_connect_data(item, "activate", G_CALLBACK(openRecentFileActivated), rid,
                [](gpointer d, GClosure*) { delete static_cast<RecentItemData*>(d); },
                static_cast<GConnectFlags>(0));
            gtk_menu_shell_append(GTK_MENU_SHELL(view.recentMenu), item);
        }
        auto* sep = gtk_separator_menu_item_new();
        gtk_menu_shell_append(GTK_MENU_SHELL(view.recentMenu), sep);
        auto* clearItem = gtk_menu_item_new_with_label("Clear Recent");
        g_signal_connect(clearItem, "activate", G_CALLBACK(clearRecentFilesActivated), &view);
        gtk_menu_shell_append(GTK_MENU_SHELL(view.recentMenu), clearItem);
    }
    gtk_widget_show_all(view.recentMenu);
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
    addToRecentFiles(view, path);
    rebuildRecentMenu(view);
#if defined(TRANSMISSION_UI_WITH_LIVE_SERVER)
    syncToLiveServer(view, path);
#endif
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
                if (hasUnpositionedNodes(view)) runAutolayout(view);
                view.filePath = path;
                view.lastSavedSnapshot =
                    transmission::encodeUiProject(captureProject(view));
                updateWindowTitle(view);
                addToRecentFiles(view, path);
                rebuildRecentMenu(view);
#if defined(TRANSMISSION_UI_WITH_LIVE_SERVER)
                syncToLiveServer(view, path);
#endif
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

struct MidiRenderCompletion {
    GraphView* view = nullptr;
    bool success = false;
    std::string outputPath;
    std::string error;
    std::size_t trackCount = 0;
    std::size_t eventCount = 0;
};

void midiRenderDialogResponse(GtkDialog* dialog, gint response, gpointer data) {
    if (response != GTK_RESPONSE_CANCEL &&
        response != GTK_RESPONSE_DELETE_EVENT)
        return;
    auto& view = *static_cast<GraphView*>(data);
    view.renderCancel.store(true, std::memory_order_release);
    gtk_window_set_title(GTK_WINDOW(dialog), "Cancelling MIDI Render");
    gtk_dialog_set_response_sensitive(dialog, GTK_RESPONSE_CANCEL, FALSE);
}

gboolean midiRenderCompleted(gpointer data) {
    auto& completion = *static_cast<MidiRenderCompletion*>(data);
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
    const auto type = completion.success ? GTK_MESSAGE_INFO : GTK_MESSAGE_ERROR;
    const auto headline = completion.success ? "MIDI render complete" : "Unable to render MIDI";
    auto* msg = gtk_message_dialog_new(
        GTK_WINDOW(view.window),
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        type, GTK_BUTTONS_CLOSE, "%s", headline);
    if (completion.success) {
        const auto detail = completion.outputPath + "\n" +
            std::to_string(completion.trackCount) + " tracks, " +
            std::to_string(completion.eventCount) + " events";
        gtk_message_dialog_format_secondary_text(
            GTK_MESSAGE_DIALOG(msg), "%s", detail.c_str());
        setStatus(view, "MIDI rendered: " + completion.outputPath, false);
    } else {
        gtk_message_dialog_format_secondary_text(
            GTK_MESSAGE_DIALOG(msg), "%s", completion.error.c_str());
        setStatus(view, "MIDI render failed: " + completion.error, true);
    }
    g_signal_connect(msg, "response",
        G_CALLBACK(+[](GtkDialog* d, gint, gpointer) {
            gtk_widget_destroy(GTK_WIDGET(d));
        }), nullptr);
    gtk_widget_show(msg);
    return G_SOURCE_REMOVE;
}

void beginMidiRender(GraphView& view, std::string outputPath, double bars) {
    if (view.renderInProgress) return;
    if (runtimeRunning(view))
        stopRuntime(view, "Audio stopped for MIDI rendering");
    const auto snapshot = runtimeSnapshot(view);
    const auto bpm = gtk_spin_button_get_value(view.tempo);
    const double sampleRate = 48000.0;
    constexpr std::size_t blockSize = 1024;
    const auto totalFrames = static_cast<std::uint64_t>(
        std::ceil(bars * 4.0 * 60.0 * sampleRate / bpm));

    view.renderCancel.store(false, std::memory_order_release);
    view.renderProgress.store(0.0, std::memory_order_release);
    view.renderInProgress = true;
    auto* dialog = gtk_dialog_new_with_buttons(
        "Rendering MIDI", GTK_WINDOW(view.window),
        static_cast<GtkDialogFlags>(
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        "_Cancel", GTK_RESPONSE_CANCEL, nullptr);
    auto* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 16);
    gtk_box_set_spacing(GTK_BOX(content), 10);
    auto* label = gtk_label_new("Capturing MIDI from all nodes…");
    auto* progress = GTK_PROGRESS_BAR(gtk_progress_bar_new());
    gtk_widget_set_size_request(GTK_WIDGET(progress), 380, -1);
    gtk_progress_bar_set_show_text(progress, TRUE);
    gtk_box_pack_start(GTK_BOX(content), label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), GTK_WIDGET(progress), FALSE, FALSE, 0);
    view.renderDialog = dialog;
    view.renderProgressBar = progress;
    g_signal_connect(dialog, "response",
        G_CALLBACK(midiRenderDialogResponse), &view);
    gtk_widget_show_all(dialog);
    view.renderProgressTimer = g_timeout_add(50, renderProgressTick, &view);

    view.renderThread = std::thread(
        [&view, snapshot, outputPath, bpm, sampleRate, totalFrames, bars]() mutable {
            auto completion = std::make_unique<MidiRenderCompletion>();
            completion->view = &view;
            completion->outputPath = outputPath;

            transmission::GraphRuntimeCompiler compiler(uiProcessorFactory());
            std::string compileError;
            const transmission::AudioDeviceConfig config{2, blockSize, sampleRate, false, 0, 0};
            auto graph = compiler.compile(snapshot, config, compileError);
            if (!graph || !graph->prepare(2, blockSize)) {
                completion->error = compileError.empty()
                    ? "Unable to compile graph for MIDI capture"
                    : compileError;
                std::lock_guard lock(view.renderCompletionMutex);
                view.renderCompletionSource = g_idle_add_full(
                    G_PRIORITY_DEFAULT, midiRenderCompleted,
                    completion.release(),
                    +[](gpointer d) { delete static_cast<MidiRenderCompletion*>(d); });
                return;
            }

            std::vector<std::string> nodeIds;
            nodeIds.reserve(snapshot.nodes.size());
            for (const auto& n : snapshot.nodes) nodeIds.push_back(n.id);

            std::vector<float> inputBuf(2 * blockSize, 0.0f);
            std::vector<float> outputBuf(2 * blockSize, 0.0f);
            std::vector<const float*> inputPtrs = {inputBuf.data(), inputBuf.data() + blockSize};
            std::vector<float*> outputPtrs = {outputBuf.data(), outputBuf.data() + blockSize};

            std::vector<transmission::CapturedMidiEvent> captured;
            std::array<transmission::MidiEvent, transmission::maxMidiEventsPerBlock> midiBuffer{};
            const double beatsPerBlock = static_cast<double>(blockSize) * bpm / (60.0 * sampleRate);

            double beat = 0.0;
            std::uint64_t framesWritten = 0;
            const double totalBeats = bars * 4.0;
            while (framesWritten < totalFrames) {
                if (view.renderCancel.load(std::memory_order_acquire)) {
                    completion->error = "MIDI rendering was cancelled";
                    break;
                }
                graph->setProcessContext({beat, bpm, true});
                graph->process(inputPtrs.data(), outputPtrs.data(), 2, blockSize);
                for (const auto& nodeId : nodeIds) {
                    const auto count = graph->copyNodeMidiOutput(
                        nodeId, midiBuffer.data(), midiBuffer.size());
                    for (std::size_t i = 0; i < count; ++i) {
                        const auto& ev = midiBuffer[i];
                        if (ev.size == 0 || (ev.data[0] & 0x80U) == 0) continue;
                        const double evBeat = beat + static_cast<double>(ev.frameOffset)
                            * bpm / (60.0 * sampleRate);
                        if (evBeat > totalBeats) continue;
                        captured.push_back({nodeId, evBeat, ev.data[0],
                            ev.size > 1 ? ev.data[1] : std::uint8_t{0},
                            ev.size > 2 ? ev.data[2] : std::uint8_t{0}});
                    }
                }
                framesWritten += blockSize;
                beat += beatsPerBlock;
                view.renderProgress.store(
                    static_cast<double>(framesWritten) / static_cast<double>(totalFrames),
                    std::memory_order_release);
            }

            if (completion->error.empty()) {
                completion->error = transmission::writeCapturedSmf(outputPath, captured, bpm);
                if (completion->error.empty()) {
                    std::map<std::string, int> tracksSeen;
                    for (const auto& ev : captured) tracksSeen[ev.nodeId]++;
                    completion->success = true;
                    completion->trackCount = tracksSeen.size();
                    completion->eventCount = captured.size();
                    view.renderProgress.store(1.0, std::memory_order_release);
                }
            }
            {
                std::lock_guard lock(view.renderCompletionMutex);
                view.renderCompletionSource = g_idle_add_full(
                    G_PRIORITY_DEFAULT, midiRenderCompleted,
                    completion.release(),
                    +[](gpointer d) { delete static_cast<MidiRenderCompletion*>(d); });
            }
        });
}

void renderMidiActivated(GtkMenuItem*, gpointer data) {
    auto& view = *static_cast<GraphView*>(data);
    if (view.renderInProgress) {
        setStatus(view, "A render is already in progress", true);
        return;
    }
    auto* dialog = gtk_file_chooser_dialog_new(
        "Render MIDI", GTK_WINDOW(view.window),
        GTK_FILE_CHOOSER_ACTION_SAVE, "_Cancel", GTK_RESPONSE_CANCEL,
        "_Render", GTK_RESPONSE_ACCEPT, nullptr);
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), "transmission.mid");
    auto* midiFilter = gtk_file_filter_new();
    gtk_file_filter_set_name(midiFilter, "MIDI file (*.mid)");
    gtk_file_filter_add_pattern(midiFilter, "*.mid");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), midiFilter);

    auto* optionsGrid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(optionsGrid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(optionsGrid), 10);
    auto* barsLabel = gtk_label_new("Length (bars)");
    auto* bars = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(1.0, 4096.0, 1.0));
    gtk_spin_button_set_value(bars,
        view.arrangementLengthBeats > 0.0
            ? view.arrangementLengthBeats / 4.0
            : std::max(1.0, gtk_spin_button_get_value(view.loopBars)));
    gtk_grid_attach(GTK_GRID(optionsGrid), barsLabel, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(optionsGrid), GTK_WIDGET(bars), 1, 0, 1, 1);
    gtk_widget_show_all(optionsGrid);
    gtk_file_chooser_set_extra_widget(GTK_FILE_CHOOSER(dialog), optionsGrid);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        gchar* selected = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        std::filesystem::path path = selected ? selected : "";
        g_free(selected);
        path.replace_extension(".mid");
        const auto lengthBars = gtk_spin_button_get_value(bars);
        gtk_widget_destroy(dialog);
        beginMidiRender(view, path.string(), lengthBars);
        return;
    }
    gtk_widget_destroy(dialog);
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

void onWindowDestroy(GtkWidget*, gpointer data) {
    auto* view = static_cast<GraphView*>(data);
    if (view->transportTimer) g_source_remove(view->transportTimer);
    if (view->externalConnectionTimer) g_source_remove(view->externalConnectionTimer);
    view->renderCancel.store(true, std::memory_order_release);
    if (view->renderThread.joinable()) view->renderThread.join();
    {
        std::lock_guard lock(view->renderCompletionMutex);
        if (view->renderCompletionSource) g_source_remove(view->renderCompletionSource);
        view->renderCompletionSource = 0;
    }
    if (view->renderProgressTimer) g_source_remove(view->renderProgressTimer);
    if (view->connectionWatchTimer) g_source_remove(view->connectionWatchTimer);
    if (view->peakMeterTimer) g_source_remove(view->peakMeterTimer);
#if defined(TRANSMISSION_UI_WITH_LIVE_SERVER)
    if (view->liveServerPollTimer) g_source_remove(view->liveServerPollTimer);
    if (view->liveServerProcess) {
        g_subprocess_send_signal(view->liveServerProcess, SIGTERM);
        g_object_unref(view->liveServerProcess);
        view->liveServerProcess = nullptr;
    }
    curl_global_cleanup();
#endif
    if (view->consoleWindow) gtk_widget_destroy(view->consoleWindow);
    delete view;
}

static gboolean peakMeterTick(gpointer data) {
    auto& view = *static_cast<GraphView*>(data);
    float newL = 0.f, newR = 0.f;
#if defined(TRANSMISSION_UI_WITH_LIVE_SERVER)
    if (view.liveServerAvailable) {
        const auto json = httpGet(view.liveServerUrl + "/peaks");
        if (!json.empty()) {
            const auto findFloat = [&](const char* key) -> float {
                const auto pos = json.find(key);
                if (pos == std::string::npos) return 0.f;
                const auto colon = json.find(':', pos);
                if (colon == std::string::npos) return 0.f;
                return static_cast<float>(std::strtod(json.data() + colon + 1, nullptr));
            };
            newL = findFloat("\"peakL\"");
            newR = findFloat("\"peakR\"");
        }
    }
#endif
#if defined(TRANSMISSION_UI_WITH_JACK) && defined(TRANSMISSION_UI_WITH_VST3)
    if (newL == 0.f && newR == 0.f && view.runtime && view.runtime->running()) {
        newL = view.runtime->peakL();
        newR = view.runtime->peakR();
    }
#endif
    const float prevL = view.outputPeakL.load(std::memory_order_relaxed);
    const float prevR = view.outputPeakR.load(std::memory_order_relaxed);
    if (std::fabs(newL - prevL) > 0.001f || std::fabs(newR - prevR) > 0.001f) {
        view.outputPeakL.store(newL, std::memory_order_relaxed);
        view.outputPeakR.store(newR, std::memory_order_relaxed);
        gtk_widget_queue_draw(view.canvas);
    }
    updateJackIndicator(view);
    return G_SOURCE_CONTINUE;
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
    loadConfig(*view);
    if (view->startJackOnStartup && !view->jackConnections->available()) {
        const std::string cmd = view->jackStartCommand + " >/dev/null 2>&1 &";
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
        std::system(cmd.c_str());
#pragma GCC diagnostic pop
        sleep(2);
        view->jackConnections = std::make_unique<transmission::JackConnectionManager>();
    }
    scanPlugins(*view);
    GtkWidget* window = gtk_application_window_new(application);
    gtk_window_set_title(GTK_WINDOW(window), "Transmission — Graph");
    gtk_window_set_default_size(GTK_WINDOW(window), 900, 520);

    GtkWidget* frame = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    auto* menuBar = gtk_menu_bar_new();
    auto* fileItem = gtk_menu_item_new_with_mnemonic("_File");
    auto* fileMenu = gtk_menu_new();
    auto* newItem = gtk_menu_item_new_with_mnemonic("_New");
    auto* openItem = gtk_menu_item_new_with_mnemonic("_Open…");
    auto* openRecentItem = gtk_menu_item_new_with_mnemonic("Open _Recent");
    auto* recentMenuWidget = gtk_menu_new();
    view->recentMenu = recentMenuWidget;
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(openRecentItem), recentMenuWidget);
    auto* saveItem = gtk_menu_item_new_with_mnemonic("_Save");
    auto* saveAsItem = gtk_menu_item_new_with_mnemonic("Save _As…");
    auto* renderItem =
        gtk_menu_item_new_with_mnemonic("_Render Audio…");
    auto* renderMidiItem =
        gtk_menu_item_new_with_mnemonic("Render _MIDI…");
    auto* separator = gtk_separator_menu_item_new();
    auto* quitItem = gtk_menu_item_new_with_mnemonic("_Quit");
    gtk_menu_shell_append(GTK_MENU_SHELL(fileMenu), newItem);
    gtk_menu_shell_append(GTK_MENU_SHELL(fileMenu), openItem);
    gtk_menu_shell_append(GTK_MENU_SHELL(fileMenu), openRecentItem);
    gtk_menu_shell_append(GTK_MENU_SHELL(fileMenu), saveItem);
    gtk_menu_shell_append(GTK_MENU_SHELL(fileMenu), saveAsItem);
    gtk_menu_shell_append(GTK_MENU_SHELL(fileMenu), renderItem);
    gtk_menu_shell_append(GTK_MENU_SHELL(fileMenu), renderMidiItem);
    gtk_menu_shell_append(GTK_MENU_SHELL(fileMenu), separator);
    gtk_menu_shell_append(GTK_MENU_SHELL(fileMenu), quitItem);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(fileItem), fileMenu);
    gtk_menu_shell_append(GTK_MENU_SHELL(menuBar), fileItem);
    auto* settingsItem = gtk_menu_item_new_with_mnemonic("_Settings");
    auto* settingsMenu = gtk_menu_new();
    auto* audioSettingsItem =
        gtk_menu_item_new_with_mnemonic("_Audio…");
#if defined(TRANSMISSION_UI_WITH_LIVE_SERVER)
    auto* mcpServerItem =
        gtk_menu_item_new_with_mnemonic("_MCP Server…");
#endif
    auto* reconnectJackItem =
        gtk_menu_item_new_with_mnemonic("_Reconnect JACK Ports");
    auto* jackStartupItem =
        gtk_menu_item_new_with_mnemonic("_JACK Startup…");
    auto* pluginPathItem =
        gtk_menu_item_new_with_mnemonic("Plugin _Path…");
    gtk_menu_shell_append(
        GTK_MENU_SHELL(settingsMenu), audioSettingsItem);
#if defined(TRANSMISSION_UI_WITH_LIVE_SERVER)
    gtk_menu_shell_append(
        GTK_MENU_SHELL(settingsMenu), mcpServerItem);
#endif
    gtk_menu_shell_append(
        GTK_MENU_SHELL(settingsMenu), reconnectJackItem);
    gtk_menu_shell_append(
        GTK_MENU_SHELL(settingsMenu), jackStartupItem);
    gtk_menu_shell_append(
        GTK_MENU_SHELL(settingsMenu), pluginPathItem);
    gtk_menu_item_set_submenu(
        GTK_MENU_ITEM(settingsItem), settingsMenu);
    gtk_menu_shell_append(GTK_MENU_SHELL(menuBar), settingsItem);
    auto* viewItem = gtk_menu_item_new_with_mnemonic("_View");
    auto* viewMenu = gtk_menu_new();
    auto* showConsoleItem = gtk_menu_item_new_with_mnemonic("_Show Console");
    auto* autolayoutItem = gtk_menu_item_new_with_mnemonic("_Autolayout");
    gtk_menu_shell_append(GTK_MENU_SHELL(viewMenu), showConsoleItem);
    gtk_menu_shell_append(GTK_MENU_SHELL(viewMenu), autolayoutItem);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(viewItem), viewMenu);
    gtk_menu_shell_append(GTK_MENU_SHELL(menuBar), viewItem);
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
    gtk_widget_add_accelerator(
        renderMidiItem, "activate", accelerators, GDK_KEY_m,
        static_cast<GdkModifierType>(GDK_CONTROL_MASK | GDK_SHIFT_MASK),
        GTK_ACCEL_VISIBLE);
    gtk_widget_add_accelerator(quitItem, "activate", accelerators, GDK_KEY_q,
                               GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    gtk_widget_add_accelerator(showConsoleItem, "activate", accelerators, GDK_KEY_grave,
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

    // Right-side status indicators — expand filler pushes them to the end
    auto* indicatorFiller = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(transportBar), indicatorFiller, TRUE, TRUE, 0);
    auto* jackIndicator = gtk_label_new(nullptr);
    auto* mcpIndicator = gtk_label_new(nullptr);
    gtk_widget_set_margin_end(mcpIndicator, 8);
    gtk_box_pack_end(GTK_BOX(transportBar), mcpIndicator, FALSE, FALSE, 4);
    gtk_box_pack_end(GTK_BOX(transportBar), jackIndicator, FALSE, FALSE, 4);

    view->tempo = tempo;
    view->loopBars = loopBars;
    view->loop = GTK_TOGGLE_BUTTON(loop);
    view->window = window;
    view->playButton = playButton;
    view->jackIndicator = jackIndicator;
    view->mcpIndicator = mcpIndicator;

    GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
        GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    view->scrolledWindow = scrolled;
    GtkWidget* canvas = gtk_drawing_area_new();
    view->canvas = canvas;
    if (const char* root = std::getenv("TRANSMISSION_ROOT"))
        view->projectHelperPath =
            (std::filesystem::path(root) / "scripts/native-ui-project.js").string();
    else
        view->projectHelperPath =
            (std::filesystem::current_path() / "scripts/native-ui-project.js").string();
    {
        char selfBuf[4096] = {};
        const ssize_t selfLen = readlink("/proc/self/exe", selfBuf, sizeof(selfBuf) - 1);
        if (selfLen > 0)
            view->inspectBinaryPath =
                (std::filesystem::path(selfBuf).parent_path() / "transmission_vst3_inspect").string();
    }

#if defined(TRANSMISSION_UI_WITH_LIVE_SERVER)
    curl_global_init(CURL_GLOBAL_DEFAULT);
    if (const char* liveUrl = std::getenv("TRANSMISSION_LIVE_URL"))
        view->liveServerUrl = liveUrl;
    if (view->mcpServerEnabled && !std::getenv("TRANSMISSION_NO_LIVE_SERVER")) {
        if (pingLiveServer(*view)) {
            logConsole(*view, "Connected to live server at " + view->liveServerUrl);
            updateWindowTitle(*view);
        } else {
            launchLiveServer(*view);
        }
    }
    view->liveServerPollTimer = g_timeout_add(500, liveServerPollTick, view);
#endif
    view->peakMeterTimer = g_timeout_add(80, peakMeterTick, view);

    updateJackIndicator(*view);
    updateMcpIndicator(*view);
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
    g_signal_connect(renderMidiItem, "activate", G_CALLBACK(renderMidiActivated), view);
    g_signal_connect(audioSettingsItem, "activate",
                     G_CALLBACK(audioSettingsActivated), view);
#if defined(TRANSMISSION_UI_WITH_LIVE_SERVER)
    g_signal_connect(mcpServerItem, "activate",
                     G_CALLBACK(mcpServerSettingsActivated), view);
#endif
    g_signal_connect(reconnectJackItem, "activate",
                     G_CALLBACK(reconnectJackActivated), view);
    g_signal_connect(jackStartupItem, "activate",
                     G_CALLBACK(jackStartupSettingsActivated), view);
    g_signal_connect(pluginPathItem, "activate",
                     G_CALLBACK(pluginPathActivated), view);
    g_signal_connect(showConsoleItem, "activate",
                     G_CALLBACK(showConsoleActivated), view);
    g_signal_connect(autolayoutItem, "activate",
                     G_CALLBACK(autolayoutActivated), view);
    g_signal_connect(quitItem, "activate", G_CALLBACK(quitActivated), view);
    gtk_widget_set_size_request(canvas, 800, 400);
    gtk_widget_add_events(canvas, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
        GDK_POINTER_MOTION_MASK | GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);
    g_signal_connect(canvas, "draw", G_CALLBACK(drawGraph), view);
    g_signal_connect(canvas, "button-press-event", G_CALLBACK(buttonPress), view);
    g_signal_connect(canvas, "motion-notify-event", G_CALLBACK(motion), view);
    g_signal_connect(canvas, "button-release-event", G_CALLBACK(buttonRelease), view);
    g_signal_connect(canvas, "scroll-event", G_CALLBACK(scrollEvent), view);
    g_signal_connect(window, "key-press-event", G_CALLBACK(keyPress), view);
    gtk_container_add(GTK_CONTAINER(scrolled), canvas);
    gtk_widget_set_hexpand(scrolled, TRUE);
    gtk_widget_set_vexpand(scrolled, TRUE);
    gtk_box_pack_start(GTK_BOX(frame), scrolled, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(frame), transportBar, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(window), frame);
    g_signal_connect(window, "delete-event", G_CALLBACK(confirmClose), view);
    g_signal_connect(window, "destroy", G_CALLBACK(onWindowDestroy), view);

    view->recentFiles = loadRecentFiles();
    rebuildRecentMenu(*view);
    if (!view->recentFiles.empty()) {
        const std::string lastPath = view->recentFiles.front();
        std::string interchange;
        std::string error;
        if (runProjectHelper(*view, "load", lastPath, "", interchange, error)) {
            transmission::UiProject project;
            if (transmission::decodeUiProject(interchange, project, error) &&
                applyProject(*view, project, error)) {
                if (hasUnpositionedNodes(*view)) runAutolayout(*view);
                view->filePath = lastPath;
                view->lastSavedSnapshot =
                    transmission::encodeUiProject(captureProject(*view));
                updateWindowTitle(*view);
            }
        }
    }

    gtk_widget_show_all(window);
}
} // namespace

int main(int argc, char** argv) {
    XInitThreads();  // Required before any X11 calls; plugins call X11 from their own threads.
    auto* application = gtk_application_new("org.transmission.Graph", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(application, "activate", G_CALLBACK(activate), nullptr);
    const int status = g_application_run(G_APPLICATION(application), argc, argv);
    g_object_unref(application);
    return status;
}
