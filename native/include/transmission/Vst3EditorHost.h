#pragma once

#include <memory>
#include <string>

namespace transmission {

/** UI-thread VST3 editor owner. It never participates in audio processing. */
class Vst3EditorHost {
public:
    Vst3EditorHost();
    ~Vst3EditorHost();

    Vst3EditorHost(const Vst3EditorHost&) = delete;
    Vst3EditorHost& operator=(const Vst3EditorHost&) = delete;

    bool open(const std::string& modulePath, const std::string& title);
    void close() noexcept;
    bool open() const noexcept;

public:
    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace transmission
