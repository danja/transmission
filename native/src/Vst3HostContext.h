// native/src/Vst3HostContext.h
//
// A VST3 host context that also hands out a Linux::IRunLoop.
//
// Why this exists: on Linux a plug-in gets no event loop from the system, and
// the VST3 specification says so outright — "On Linux the host has to provide
// this interface to the plug-in as there's no global event run loop defined as
// on other platforms." A plug-in that is handed no run loop has nowhere to run
// its message queue: posts accumulate until the framework's hard limit (JUCE
// asserts at 128 unprocessed messages in juce_Messaging_linux.cpp) and timers
// never fire.
//
// The SDK offers two delivery points. IPlugFrame carries one, but only a
// plug-in *with an editor* ever sees it. JUCE loads its run loop in
// setHostContext, so the context is the delivery point that also covers an
// audio-only instance — which is precisely the one that gets hammered when
// something drives a live server from another thread.
//
// The loop runs on a thread of its own rather than on a UI main loop, because
// the engine that loads plug-ins has no main loop at all: a plug-in must get
// its timers and its message queue serviced whether or not an editor is open.
// Nothing is created until a plug-in registers a handler or a timer, so a
// project with no such plug-in pays for none of this.

#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

#include <poll.h>

#include "pluginterfaces/gui/iplugview.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"

namespace transmission {

/**
 * The host application, plus the Linux run loop the specification requires a
 * host to provide. Both the processing instance and the editor instance use
 * one; the editor additionally offers its own frame run loop, which is what the
 * plug-in prefers when it has a view.
 */
class Vst3HostContext : public Steinberg::Vst::HostApplication {
public:
    Vst3HostContext() = default;
    ~Vst3HostContext() = default;

    // TUID is char[16], so this is the SDK's `const TUID` = `const char*`.
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid,
                                                 void** obj) override {
        if (obj == nullptr) return Steinberg::kInvalidArgument;
        if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Linux::IRunLoop::iid)) {
            *obj = static_cast<Steinberg::Linux::IRunLoop*>(&runLoop_);
            runLoop_.addRef();
            return Steinberg::kResultTrue;
        }
        return Steinberg::Vst::HostApplication::queryInterface(iid, obj);
    }

private:
    /**
     * A run loop on its own thread: poll() for descriptors, a deadline for
     * timers, and a wake pipe so registering or stopping is immediate.
     *
     * A separate object rather than a second base class on the context, since
     * two FUnknown bases make every FUnknown method ambiguous.
     */
    class RunLoop : public Steinberg::Linux::IRunLoop {
    public:
        ~RunLoop() { stop(); }

        // Defined here rather than via DECLARE_FUNKNOWN_METHODS, whose bodies
        // live in pluginterfacesupport.cpp: this is a private nested class, so
        // the SDK has no translation unit to put them in.
        Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid,
                                                     void** obj) override {
            if (obj == nullptr) return Steinberg::kInvalidArgument;
            if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Linux::IRunLoop::iid)) {
                *obj = static_cast<Steinberg::Linux::IRunLoop*>(this);
                addRef();
                return Steinberg::kResultTrue;
            }
            *obj = nullptr;
            return Steinberg::kNoInterface;
        }

        RunLoop() : __funknownRefCount(1) {}

        Steinberg::uint32 PLUGIN_API addRef() override {
            return Steinberg::FUnknownPrivate::atomicAdd(__funknownRefCount, 1);
        }

        Steinberg::uint32 PLUGIN_API release() override {
            // FUnknown::release deletes, but IRunLoop's destructor is not
            // virtual, so this cannot: the context owns the run loop and
            // outlives every reference the plug-in takes on it.
            return Steinberg::FUnknownPrivate::atomicAdd(__funknownRefCount, -1);
        }

        Steinberg::tresult PLUGIN_API registerEventHandler(
            Steinberg::Linux::IEventHandler* handler,
            Steinberg::Linux::FileDescriptor fd) override {
            if (handler == nullptr || fd < 0) return Steinberg::kInvalidArgument;

            {
                const std::lock_guard<std::mutex> lock(mutex_);
                if (stopping_) return Steinberg::kResultFalse;
                for (const auto& entry : fds_) {
                    if (entry.fd != fd) continue;
                    return entry.handler == handler ? Steinberg::kResultFalse
                                                   : Steinberg::kInvalidArgument;
                }
                fds_.push_back(FdEntry{fd, handler});
            }

            ensureThread();
            return Steinberg::kResultTrue;
        }

        Steinberg::tresult PLUGIN_API unregisterEventHandler(
            Steinberg::Linux::IEventHandler* handler) override {
            bool removed = false;
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                if (stopping_) return Steinberg::kResultFalse;
                for (auto it = fds_.begin(); it != fds_.end(); ++it) {
                    if (it->handler != handler) continue;
                    // Wait for any call already running on this handler: the
                    // plug-in is free to destroy it the moment we return.
                    const std::lock_guard<std::mutex> callbacks(callbacks_);
                    it = fds_.erase(it);
                    removed = true;
                }
            }

            if (removed) wake();
            return removed ? Steinberg::kResultTrue : Steinberg::kInvalidArgument;
        }

        Steinberg::tresult PLUGIN_API registerTimer(
            Steinberg::Linux::ITimerHandler* handler,
            Steinberg::Linux::TimerInterval milliseconds) override {
            if (handler == nullptr || milliseconds == 0)
                return Steinberg::kInvalidArgument;

            {
                const std::lock_guard<std::mutex> lock(mutex_);
                if (stopping_) return Steinberg::kResultFalse;
                for (const auto& entry : timers_) {
                    if (entry.handler == handler) return Steinberg::kResultFalse;
                }
                timers_.push_back(TimerEntry{
                    handler, milliseconds, std::chrono::steady_clock::now() +
                                              std::chrono::milliseconds(milliseconds)});
            }

            ensureThread();
            return Steinberg::kResultTrue;
        }

        Steinberg::tresult PLUGIN_API unregisterTimer(
            Steinberg::Linux::ITimerHandler* handler) override {
            bool removed = false;
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                if (stopping_) return Steinberg::kResultFalse;
                for (auto it = timers_.begin(); it != timers_.end(); ++it) {
                    if (it->handler != handler) continue;
                    const std::lock_guard<std::mutex> callbacks(callbacks_);
                    it = timers_.erase(it);
                    removed = true;
                }
            }

            if (removed) wake();
            return removed ? Steinberg::kResultTrue : Steinberg::kInvalidArgument;
        }

    private:
        struct FdEntry {
            Steinberg::Linux::FileDescriptor fd;
            Steinberg::Linux::IEventHandler* handler;
        };
        struct TimerEntry {
            Steinberg::Linux::ITimerHandler* handler;
            Steinberg::Linux::TimerInterval interval;
            std::chrono::steady_clock::time_point due;
        };

        void ensureThread() {
            const std::lock_guard<std::mutex> lock(threadMutex_);
            if (! thread_.joinable()) thread_ = std::thread([this] { run(); });
        }

        void wake() {
            const std::lock_guard<std::mutex> lock(wakeMutex_);
            wake_ = true;
            wakeCondition_.notify_all();
        }

        void stop() {
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                if (stopping_) return;
                stopping_ = true;
            }
            wake();
            if (thread_.joinable()) thread_.join();
        }

        void run() {
            while (true) {
                std::vector<pollfd> descriptors;
                std::vector<Steinberg::Linux::IEventHandler*> handlers;

                {
                    const std::lock_guard<std::mutex> lock(mutex_);
                    if (stopping_) return;
                    for (const auto& entry : fds_) {
                        pollfd descriptor{};
                        descriptor.fd = entry.fd;
                        descriptor.events = POLLIN;
                        descriptors.push_back(descriptor);
                        handlers.push_back(entry.handler);
                    }
                }

                if (descriptors.empty() && timers_.empty()) {
                    // Nothing registered: sleep until told otherwise rather
                    // than spinning a thread for nothing.
                    std::unique_lock<std::mutex> lock(wakeMutex_);
                    wakeCondition_.wait(lock, [this] { return wake_; });
                    wake_ = false;
                    continue;
                }

                // Bounded so a timer still comes due on time even when no
                // descriptor ever becomes readable.
                int timeout = 20;
                {
                    const std::lock_guard<std::mutex> lock(mutex_);
                    if (stopping_) return;
                    const auto now = std::chrono::steady_clock::now();
                    for (const auto& entry : timers_) {
                        const auto remaining =
                            std::chrono::duration_cast<std::chrono::milliseconds>(
                                entry.due - now).count();
                        if (remaining > 0)
                            timeout = std::min(timeout, static_cast<int>(remaining));
                        else
                            timeout = 0;
                    }
                }

                const int ready =
                    descriptors.empty()
                        ? (timeout > 0 ? ::poll(nullptr, 0, timeout) : 0)
                        : ::poll(descriptors.data(),
                                 static_cast<nfds_t>(descriptors.size()), timeout);

                const auto now = std::chrono::steady_clock::now();
                const std::lock_guard<std::mutex> lock(mutex_);
                if (stopping_) return;

                if (ready > 0) {
                    for (std::size_t index = 0; index < descriptors.size(); ++index) {
                        if ((descriptors[index].revents & POLLIN) == 0) continue;
                        const std::lock_guard<std::mutex> callbacks(callbacks_);
                        handlers[index]->onFDIsSet(descriptors[index].fd);
                    }
                }

                for (auto it = timers_.begin(); it != timers_.end(); ++it) {
                    if (now < it->due) continue;
                    it->due = now + std::chrono::milliseconds(it->interval);
                    const std::lock_guard<std::mutex> callbacks(callbacks_);
                    it->handler->onTimer();
                }
            }
        }

        protected:
        Steinberg::int32 __funknownRefCount{1};

    private:
        mutable std::mutex mutex_;
        // Held across a callback so unregistering cannot pull a handler out
        // from under a call that is already on the stack.
        mutable std::mutex callbacks_;
        std::mutex threadMutex_;
        std::mutex wakeMutex_;
        std::condition_variable wakeCondition_;
        bool wake_ = false;
        bool stopping_ = false;
        std::vector<FdEntry> fds_;
        std::vector<TimerEntry> timers_;
        std::thread thread_;
    };

    RunLoop runLoop_;
};

} // namespace transmission