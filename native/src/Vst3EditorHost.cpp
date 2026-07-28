#include "transmission/Vst3EditorHost.h"

#ifdef TRANSMISSION_UI_WITH_VST3

#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

#include <gtk/gtk.h>
#include <gtk/gtkx.h>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <memory>
#include <unordered_map>

namespace transmission {

class EditorFrame final : public Steinberg::IPlugFrame, public Steinberg::Linux::IRunLoop {
public:
    explicit EditorFrame(Vst3EditorHost::Impl* owner) : owner_(owner) {}
    ~EditorFrame();

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid,
                                                  void** object) override;
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }
    Steinberg::tresult PLUGIN_API resizeView(Steinberg::IPlugView* view,
                                             Steinberg::ViewRect* newSize) override;
    Steinberg::tresult PLUGIN_API registerEventHandler(Steinberg::Linux::IEventHandler*,
                                                       Steinberg::Linux::FileDescriptor) override {
        return Steinberg::kNotImplemented;
    }
    Steinberg::tresult PLUGIN_API unregisterEventHandler(Steinberg::Linux::IEventHandler*) override {
        return Steinberg::kNotImplemented;
    }
    Steinberg::tresult PLUGIN_API registerTimer(Steinberg::Linux::ITimerHandler* handler,
                                                Steinberg::Linux::TimerInterval milliseconds) override;
    Steinberg::tresult PLUGIN_API unregisterTimer(Steinberg::Linux::ITimerHandler* handler) override;

private:
    Vst3EditorHost::Impl* owner_;
    std::unordered_map<Steinberg::Linux::ITimerHandler*, guint> timers_;
};

struct Vst3EditorHost::Impl {
    VST3::Hosting::Module::Ptr module;
    Steinberg::Vst::HostApplication hostApplication;
    std::unique_ptr<Steinberg::Vst::PlugProvider> provider;
    Steinberg::IPtr<Steinberg::Vst::IEditController> controller;
    Steinberg::IPtr<Steinberg::IPlugView> view;
    std::unique_ptr<EditorFrame> frame;
    GtkWidget* window = nullptr;
    GtkWidget* socket = nullptr;
    bool closing = false;

    void destroyWindow() noexcept {
        if (window && !closing) {
            closing = true;
            gtk_widget_destroy(window);
            window = nullptr;
            socket = nullptr;
            closing = false;
        }
    }
};

Steinberg::tresult PLUGIN_API EditorFrame::queryInterface(const Steinberg::TUID iid,
                                                           void** object) {
    if (!object) return Steinberg::kInvalidArgument;
    if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::IPlugFrame::iid) ||
        Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::FUnknown::iid)) {
        *object = static_cast<Steinberg::IPlugFrame*>(this);
        addRef();
        return Steinberg::kResultTrue;
    }
    if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Linux::IRunLoop::iid)) {
        *object = static_cast<Steinberg::Linux::IRunLoop*>(this);
        addRef();
        return Steinberg::kResultTrue;
    }
    *object = nullptr;
    return Steinberg::kNoInterface;
}

EditorFrame::~EditorFrame() {
    for (const auto& [handler, source] : timers_) {
        (void)handler;
        g_source_remove(source);
    }
}

static gboolean timerCallback(gpointer data) {
    static_cast<Steinberg::Linux::ITimerHandler*>(data)->onTimer();
    return G_SOURCE_CONTINUE;
}

Steinberg::tresult PLUGIN_API EditorFrame::registerTimer(
    Steinberg::Linux::ITimerHandler* handler, Steinberg::Linux::TimerInterval milliseconds) {
    if (!handler || timers_.count(handler) != 0) return Steinberg::kInvalidArgument;
    const auto source = g_timeout_add(static_cast<guint>(std::max<std::uint64_t>(1, milliseconds)),
                                      timerCallback, handler);
    if (!source) return Steinberg::kResultFalse;
    timers_[handler] = source;
    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API EditorFrame::unregisterTimer(
    Steinberg::Linux::ITimerHandler* handler) {
    const auto it = timers_.find(handler);
    if (it == timers_.end()) return Steinberg::kInvalidArgument;
    g_source_remove(it->second);
    timers_.erase(it);
    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API EditorFrame::resizeView(Steinberg::IPlugView*,
                                                      Steinberg::ViewRect* newSize) {
    if (!owner_ || !owner_->window || !newSize) return Steinberg::kInvalidArgument;
    gtk_window_resize(GTK_WINDOW(owner_->window), newSize->right - newSize->left,
                      newSize->bottom - newSize->top);
    return Steinberg::kResultTrue;
}

Vst3EditorHost::Vst3EditorHost() : impl_(std::make_unique<Impl>()) {}
Vst3EditorHost::~Vst3EditorHost() { close(); }

bool Vst3EditorHost::open(const std::string& modulePath, const std::string& title) {
    close();
    const auto fail = [&](const char* message) {
        std::cerr << "VST3 editor: " << message << " (" << modulePath << ")\n";
        close();
        return false;
    };
    std::string error;
    impl_->module = VST3::Hosting::Module::create(modulePath, error);
    if (!impl_->module) return fail(error.empty() ? "module load failed" : error.c_str());
    Steinberg::Vst::PluginContextFactory::instance().setPluginContext(&impl_->hostApplication);
    VST3::Hosting::ClassInfo selected;
    bool found = false;
    for (const auto& info : impl_->module->getFactory().classInfos()) {
        if (info.category() == kVstAudioEffectClass) {
            selected = info;
            found = true;
            break;
        }
    }
    if (!found) return fail("no audio effect class");
    impl_->provider = std::make_unique<Steinberg::Vst::PlugProvider>(
        impl_->module->getFactory(), selected, true);
    if (!impl_->provider->initialize()) return fail("plugin initialization failed");
    impl_->controller = impl_->provider->getController();
    if (!impl_->controller) return fail("plugin has no edit controller");
    impl_->view = impl_->controller->createView(Steinberg::Vst::ViewType::kEditor);
    if (!impl_->view) return fail("plugin does not provide an editor view");
    Steinberg::ViewRect size{};
    if (impl_->view->getSize(&size) != Steinberg::kResultTrue) return fail("editor size query failed");
    if (impl_->view->isPlatformTypeSupported(Steinberg::kPlatformTypeX11EmbedWindowID) !=
        Steinberg::kResultTrue) return fail("editor does not support X11 embedding");

    impl_->frame = std::make_unique<EditorFrame>(impl_.get());
    impl_->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(impl_->window), title.c_str());
    gtk_window_set_default_size(GTK_WINDOW(impl_->window), size.right - size.left,
                                size.bottom - size.top);
    impl_->socket = gtk_socket_new();
    gtk_container_add(GTK_CONTAINER(impl_->window), impl_->socket);
    gtk_widget_show_all(impl_->window);
    impl_->view->setFrame(impl_->frame.get());
    const auto socketId = gtk_socket_get_id(GTK_SOCKET(impl_->socket));
    if (!socketId || impl_->view->attached(reinterpret_cast<void*>(
            static_cast<std::uintptr_t>(socketId)), Steinberg::kPlatformTypeX11EmbedWindowID) != Steinberg::kResultTrue) {
        return fail("editor attachment failed");
    }
    return true;
}

void Vst3EditorHost::close() noexcept {
    if (!impl_) return;
    if (impl_->view) {
        impl_->view->setFrame(nullptr);
        impl_->view->removed();
    }
    impl_->view = nullptr;
    impl_->frame.reset();
    impl_->controller = nullptr;
    impl_->destroyWindow();
    impl_->provider.reset();
    impl_->module.reset();
}

bool Vst3EditorHost::open() const noexcept { return impl_ && impl_->window != nullptr; }

} // namespace transmission

#else

namespace transmission {
struct Vst3EditorHost::Impl {};
Vst3EditorHost::Vst3EditorHost() : impl_(std::make_unique<Impl>()) {}
Vst3EditorHost::~Vst3EditorHost() = default;
bool Vst3EditorHost::open(const std::string&, const std::string&) { return false; }
void Vst3EditorHost::close() noexcept {}
bool Vst3EditorHost::open() const noexcept { return false; }
} // namespace transmission

#endif
