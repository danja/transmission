#include <gtk/gtk.h>
#include <cairo.h>

#include <algorithm>
#include <cmath>
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
    double x = 0.0;
    double y = 0.0;
};

struct Edge {
    std::size_t from;
    std::size_t to;
    std::size_t fromPort = 0;
    std::size_t toPort = 0;
};

struct GraphView {
    std::vector<Node> nodes{
        {"system-input", "System Input", true, 2, 0, 60.0, 150.0},
        {"gain", "AGain / VST3", false, 2, 2, 340.0, 150.0},
        {"system-output", "System Output", true, 0, 2, 620.0, 150.0},
    };
    std::vector<Edge> edges{{0, 1}, {1, 2}};
    std::size_t dragging = static_cast<std::size_t>(-1);
    double dragX = 0.0;
    double dragY = 0.0;
    std::size_t connectingFrom = static_cast<std::size_t>(-1);
    std::size_t connectingPort = 0;
    double pointerX = 0.0;
    double pointerY = 0.0;
};

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
};

PortHit portAt(const GraphView& view, double x, double y) {
    constexpr double radius = 9.0;
    for (std::size_t index = 0; index < view.nodes.size(); ++index) {
        const auto& node = view.nodes[index];
        for (std::size_t port = 0; port < node.audioInputs; ++port) {
            if (std::hypot(x - node.x, y - portY(node, port)) <= radius)
                return {index, port, false};
        }
        for (std::size_t port = 0; port < node.audioOutputs; ++port) {
            if (std::hypot(x - (node.x + nodeWidth), y - portY(node, port)) <= radius)
                return {index, port, true};
        }
    }
    return {};
}

void drawPort(cairo_t* cr, double x, double y, bool output) {
    cairo_set_source_rgb(cr, output ? 0.42 : 0.28, output ? 0.74 : 0.62,
                         output ? 0.92 : 0.76);
    cairo_arc(cr, x, y, 6.0, 0.0, 2.0 * M_PI);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, 0.08, 0.10, 0.14);
    cairo_set_line_width(cr, 1.5);
    cairo_arc(cr, x, y, 6.0, 0.0, 2.0 * M_PI);
    cairo_stroke(cr);
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
        drawArrow(cr, from.x + nodeWidth, portY(from, edge.fromPort),
                  to.x, portY(to, edge.toPort));
    }

    if (view.connectingFrom != static_cast<std::size_t>(-1)) {
        const auto& from = view.nodes[view.connectingFrom];
        cairo_set_source_rgb(cr, 0.74, 0.80, 0.94);
        cairo_set_line_width(cr, 2.0);
        drawArrow(cr, from.x + nodeWidth, portY(from, view.connectingPort),
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
        cairo_show_text(cr, node.system ? "audio ports" : "audio processor");
        for (std::size_t port = 0; port < node.audioInputs; ++port)
            drawPort(cr, node.x, portY(node, port), false);
        for (std::size_t port = 0; port < node.audioOutputs; ++port)
            drawPort(cr, node.x + nodeWidth, portY(node, port), true);
    }
    return FALSE;
}

gboolean buttonPress(GtkWidget* widget, GdkEventButton* event, gpointer data) {
    if (event->button != GDK_BUTTON_PRIMARY) return FALSE;
    auto& view = *static_cast<GraphView*>(data);
    const auto hit = portAt(view, event->x, event->y);
    if (hit.output) {
        view.connectingFrom = hit.node;
        view.connectingPort = hit.port;
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
        if (!hit.output && hit.node != static_cast<std::size_t>(-1) && hit.node != view.connectingFrom) {
            const auto duplicate = std::find_if(view.edges.begin(), view.edges.end(), [&](const auto& edge) {
                return edge.from == view.connectingFrom && edge.fromPort == view.connectingPort &&
                       edge.to == hit.node && edge.toPort == hit.port;
            });
            if (duplicate == view.edges.end())
                view.edges.push_back({view.connectingFrom, hit.node, view.connectingPort, hit.port});
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
    GtkWidget* window = gtk_application_window_new(application);
    gtk_window_set_title(GTK_WINDOW(window), "Transmission — Graph");
    gtk_window_set_default_size(GTK_WINDOW(window), 900, 520);

    GtkWidget* frame = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget* title = gtk_label_new("  Transmission   •   drag nodes   •   connect output sockets to input sockets");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_widget_set_margin_top(title, 12);
    gtk_widget_set_margin_bottom(title, 12);
    gtk_widget_set_margin_start(title, 8);
    gtk_box_pack_start(GTK_BOX(frame), title, FALSE, FALSE, 0);

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
        delete static_cast<GraphView*>(data);
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
