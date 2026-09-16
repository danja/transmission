#include "transmission/Vst3EditorHost.h"

#ifdef TRANSMISSION_UI_WITH_VST3

#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/common/memorystream.h"
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
    Steinberg::tresult PLUGIN_API registerEventHandler(Steinberg::Linux::IEventHandler* handler,
                                                       Steinberg::Linux::FileDescriptor fd) override;
    Steinberg::tresult PLUGIN_API unregisterEventHandler(Steinberg::Linux::IEventHandler* handler) override;
    Steinberg::tresult PLUGIN_API registerTimer(Steinberg::Linux::ITimerHandler* handler,
                                                Steinberg::Linux::TimerInterval milliseconds) override;
    Steinberg::tresult PLUGIN_API unregisterTimer(Steinberg::Linux::ITimerHandler* handler) override;

private:
    struct FdSource { guint id; Steinberg::Linux::FileDescriptor fd; };

    Vst3EditorHost::Impl* owner_;
    // Keyed by fd so multiple fds registered for the same handler are all watched.
    struct FdEntry { guint sourceId; Steinberg::Linux::IEventHandler* handler; };
    std::unordered_map<Steinberg::Linux::ITimerHandler*, guint> timers_;
    std::unordered_map<Steinberg::Linux::FileDescriptor, FdEntry> fdSources_;
};

class ComponentHandler final : public Steinberg::Vst::IComponentHandler {
public:
    explicit ComponentHandler(Vst3EditorHost::Impl* owner) : owner_(owner) {}

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid,
                                                  void** object) override;
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }
    Steinberg::tresult PLUGIN_API beginEdit(
        Steinberg::Vst::ParamID) override {
        return Steinberg::kResultTrue;
    }
    Steinberg::tresult PLUGIN_API performEdit(
        Steinberg::Vst::ParamID id,
        Steinberg::Vst::ParamValue valueNormalized) override;
    Steinberg::tresult PLUGIN_API endEdit(
        Steinberg::Vst::ParamID) override {
        return Steinberg::kResultTrue;
    }
    Steinberg::tresult PLUGIN_API restartComponent(
        Steinberg::int32 flags) override;

private:
    Vst3EditorHost::Impl* owner_;
};

struct Vst3EditorHost::Impl {
    VST3::Hosting::Module::Ptr module;
    Steinberg::Vst::HostApplication hostApplication;
    std::unique_ptr<Steinberg::Vst::PlugProvider> provider;
    Steinberg::IPtr<Steinberg::Vst::IEditController> controller;
    Steinberg::IPtr<Steinberg::Vst::IComponent> component;
    std::unique_ptr<ComponentHandler> componentHandler;
    Steinberg::IPtr<Steinberg::IPlugView> view;
    std::unique_ptr<EditorFrame> frame;
    GtkWidget* window = nullptr;
    Window    xembedWindow = 0;  // bare X11 window; no GDK event selection
    bool closing = false;
    ParameterEditCallback parameterEdit;
    StateCallback stateChanged;
    Vst3EditorHost::LiveStateCallback liveStateChanged;

    void forwardLiveState() {
        if (!liveStateChanged || !component) return;
        ProcessorState state;
        Steinberg::MemoryStream stream;
        if (component->getState(&stream) == Steinberg::kResultTrue) {
            const auto size = static_cast<std::size_t>(stream.getSize());
            const auto* data = reinterpret_cast<const std::uint8_t*>(stream.getData());
            if (data && size) state.component.assign(data, data + size);
        }
        liveStateChanged(state);
    }

    void forwardParameter(Steinberg::Vst::ParamID id,
                          Steinberg::Vst::ParamValue value) {
        if (parameterEdit)
            parameterEdit(static_cast<std::uint32_t>(id), value);
    }

    void forwardAllParameters() {
        if (!controller || !parameterEdit) return;
        const auto count = controller->getParameterCount();
        for (Steinberg::int32 index = 0; index < count; ++index) {
            Steinberg::Vst::ParameterInfo info{};
            if (controller->getParameterInfo(index, info) != Steinberg::kResultTrue)
                continue;
            forwardParameter(info.id, controller->getParamNormalized(info.id));
        }
    }

    void destroyWindow() noexcept {
        if (window && !closing) {
            closing = true;
            // GTK destroying the top-level window also destroys all X11 children,
            // including xembedWindow, so do not call XDestroyWindow separately.
            gtk_widget_destroy(window);
            window = nullptr;
            xembedWindow = 0;
            closing = false;
        }
    }
};

Steinberg::tresult PLUGIN_API ComponentHandler::queryInterface(
    const Steinberg::TUID iid, void** object) {
    if (!object) return Steinberg::kInvalidArgument;
    if (Steinberg::FUnknownPrivate::iidEqual(
            iid, Steinberg::Vst::IComponentHandler::iid) ||
        Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::FUnknown::iid)) {
        *object = static_cast<Steinberg::Vst::IComponentHandler*>(this);
        addRef();
        return Steinberg::kResultTrue;
    }
    *object = nullptr;
    return Steinberg::kNoInterface;
}

Steinberg::tresult PLUGIN_API ComponentHandler::performEdit(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue valueNormalized) {
    if (!owner_) return Steinberg::kResultFalse;
    owner_->forwardParameter(id, valueNormalized);
    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API ComponentHandler::restartComponent(
    Steinberg::int32 flags) {
    if (!owner_) return Steinberg::kResultFalse;
    if ((flags & Steinberg::Vst::kParamValuesChanged) != 0)
        owner_->forwardAllParameters();
    owner_->forwardLiveState();
    return Steinberg::kResultTrue;
}

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
    for (const auto& [fd, entry] : fdSources_) {
        (void)fd;
        g_source_remove(entry.sourceId);
    }
}

Steinberg::tresult PLUGIN_API EditorFrame::registerEventHandler(
    Steinberg::Linux::IEventHandler* handler, Steinberg::Linux::FileDescriptor fd) {
    if (!handler || fdSources_.count(fd))
        return Steinberg::kInvalidArgument;
    struct Ctx { Steinberg::Linux::IEventHandler* h; Steinberg::Linux::FileDescriptor fd; };
    auto* ctx = new Ctx{handler, fd};
    GIOChannel* ch = g_io_channel_unix_new(fd);
    const guint id = g_io_add_watch_full(
        ch, G_PRIORITY_DEFAULT, G_IO_IN,
        [](GIOChannel*, GIOCondition, gpointer data) -> gboolean {
            auto* c = static_cast<Ctx*>(data);
            c->h->onFDIsSet(c->fd);
            return G_SOURCE_CONTINUE;
        },
        ctx, [](gpointer data) { delete static_cast<Ctx*>(data); });
    g_io_channel_unref(ch);
    if (!id) { delete ctx; return Steinberg::kResultFalse; }
    fdSources_[fd] = { id, handler };
    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API EditorFrame::unregisterEventHandler(
    Steinberg::Linux::IEventHandler* handler) {
    bool found = false;
    for (auto it = fdSources_.begin(); it != fdSources_.end(); ) {
        if (it->second.handler == handler) {
            g_source_remove(it->second.sourceId);
            it = fdSources_.erase(it);
            found = true;
        } else {
            ++it;
        }
    }
    return found ? Steinberg::kResultTrue : Steinberg::kInvalidArgument;
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
    const int w = newSize->right - newSize->left;
    const int h = newSize->bottom - newSize->top;
    if (owner_->xembedWindow) {
        Display* dpy = gdk_x11_display_get_xdisplay(gdk_display_get_default());
        XResizeWindow(dpy, owner_->xembedWindow, static_cast<unsigned>(w), static_cast<unsigned>(h));
        XFlush(dpy);
    }
    gtk_window_resize(GTK_WINDOW(owner_->window), w, h);
    return Steinberg::kResultTrue;
}

Vst3EditorHost::Vst3EditorHost() : impl_(std::make_unique<Impl>()) {}
Vst3EditorHost::~Vst3EditorHost() { close(); }

bool Vst3EditorHost::open(const std::string& modulePath,
                          const std::string& title,
                          ParameterEditCallback parameterEdit,
                          StateCallback stateChanged,
                          const ProcessorState& initialState,
                          const std::vector<std::pair<std::uint32_t, double>>&
                              initialParameters,
                          LiveStateCallback liveStateChanged) {
    close();
    const auto fail = [&](const char* message) {
        std::cerr << "VST3 editor: " << message << " (" << modulePath << ")\n";
        close();
        return false;
    };
    const auto* display = gdk_display_get_default();
    if (!display || !GDK_IS_X11_DISPLAY(display)) {
        return fail("native editors require the GTK X11 backend; launch with GDK_BACKEND=x11");
    }
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
    impl_->component = impl_->provider->getComponentPtr();
    if (!impl_->controller) return fail("plugin has no edit controller");
    impl_->parameterEdit = std::move(parameterEdit);
    impl_->stateChanged = std::move(stateChanged);
    impl_->liveStateChanged = std::move(liveStateChanged);
    if (impl_->component && !initialState.component.empty()) {
        Steinberg::MemoryStream stream(
            const_cast<std::uint8_t*>(initialState.component.data()),
            static_cast<Steinberg::TSize>(
                initialState.component.size()));
        if (impl_->component->setState(&stream) != Steinberg::kResultTrue)
            return fail("plugin rejected saved component state");
        stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
        impl_->controller->setComponentState(&stream);
    }
    if (!initialState.controller.empty()) {
        Steinberg::MemoryStream stream(
            const_cast<std::uint8_t*>(initialState.controller.data()),
            static_cast<Steinberg::TSize>(
                initialState.controller.size()));
        if (impl_->controller->setState(&stream) != Steinberg::kResultTrue)
            return fail("plugin rejected saved controller state");
    }
    for (const auto& [id, value] : initialParameters)
        impl_->controller->setParamNormalized(
            static_cast<Steinberg::Vst::ParamID>(id), value);
    impl_->componentHandler = std::make_unique<ComponentHandler>(impl_.get());
    if (impl_->controller->setComponentHandler(
            impl_->componentHandler.get()) != Steinberg::kResultTrue)
        return fail("plugin rejected the component handler");
    impl_->view = impl_->controller->createView(Steinberg::Vst::ViewType::kEditor);
    if (!impl_->view) return fail("plugin does not provide an editor view");
    Steinberg::ViewRect size{};
    if (impl_->view->getSize(&size) != Steinberg::kResultTrue) return fail("editor size query failed");
    if (impl_->view->isPlatformTypeSupported(Steinberg::kPlatformTypeX11EmbedWindowID) !=
        Steinberg::kResultTrue) return fail("editor does not support X11 embedding");

    impl_->frame = std::make_unique<EditorFrame>(impl_.get());
    impl_->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(impl_->window), title.c_str());
    const int plugW = size.right - size.left;
    const int plugH = size.bottom - size.top;
    gtk_window_set_default_size(GTK_WINDOW(impl_->window), plugW, plugH);
    gtk_widget_show_all(impl_->window);

    // Create a bare X11 window with no GDK event selection as the embedding
    // parent. GtkSocket (XEMBED) and GtkDrawingArea (GDK-managed) both
    // interfere with XI2 event delivery to JUCE's child window. A plain
    // XCreateSimpleWindow on GDK's display has no event masks and lets JUCE
    // receive XI2 pointer events directly.
    GdkWindow* gdkWin = gtk_widget_get_window(impl_->window);
    const Window gtkXWin = gdkWin ? gdk_x11_window_get_xid(gdkWin) : 0;
    Display* x11Dpy = gdk_x11_display_get_xdisplay(gdk_display_get_default());
    if (!gtkXWin || !x11Dpy) return fail("could not get GTK X11 window");

    impl_->xembedWindow = XCreateSimpleWindow(x11Dpy, gtkXWin,
                                              0, 0, static_cast<unsigned>(plugW), static_cast<unsigned>(plugH),
                                              0, 0, 0);
    XMapWindow(x11Dpy, impl_->xembedWindow);
    XFlush(x11Dpy);

    impl_->view->setFrame(impl_->frame.get());
    if (!impl_->xembedWindow || impl_->view->attached(
            reinterpret_cast<void*>(static_cast<std::uintptr_t>(impl_->xembedWindow)),
            Steinberg::kPlatformTypeX11EmbedWindowID) != Steinberg::kResultTrue) {
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
    if (impl_->stateChanged && impl_->component) {
        ProcessorState state;
        Steinberg::MemoryStream componentStream;
        if (impl_->component->getState(&componentStream) ==
            Steinberg::kResultTrue) {
            const auto size = static_cast<std::size_t>(
                componentStream.getSize());
            const auto* data =
                reinterpret_cast<const std::uint8_t*>(
                    componentStream.getData());
            if (data && size) state.component.assign(data, data + size);
        }
        Steinberg::MemoryStream controllerStream;
        if (impl_->controller &&
            impl_->controller->getState(&controllerStream) ==
                Steinberg::kResultTrue) {
            const auto size = static_cast<std::size_t>(
                controllerStream.getSize());
            const auto* data =
                reinterpret_cast<const std::uint8_t*>(
                    controllerStream.getData());
            if (data && size) state.controller.assign(data, data + size);
        }
        impl_->stateChanged(std::move(state));
    }
    if (impl_->controller) impl_->controller->setComponentHandler(nullptr);
    impl_->componentHandler.reset();
    impl_->controller = nullptr;
    impl_->component = nullptr;
    impl_->parameterEdit = {};
    impl_->stateChanged = {};
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
bool Vst3EditorHost::open(
    const std::string&, const std::string&, ParameterEditCallback,
    StateCallback, const ProcessorState&,
    const std::vector<std::pair<std::uint32_t, double>>&) {
    return false;
}
void Vst3EditorHost::close() noexcept {}
bool Vst3EditorHost::open() const noexcept { return false; }
} // namespace transmission

#endif
