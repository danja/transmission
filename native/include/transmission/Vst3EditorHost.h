#pragma once

#include "AudioProcessor.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace transmission {

/** UI-thread VST3 editor owner. It never participates in audio processing. */
class Vst3EditorHost {
public:
    using ParameterEditCallback =
        std::function<void(std::uint32_t, double)>;
    using StateCallback = std::function<void(ProcessorState)>;

    Vst3EditorHost();
    ~Vst3EditorHost();

    Vst3EditorHost(const Vst3EditorHost&) = delete;
    Vst3EditorHost& operator=(const Vst3EditorHost&) = delete;

    bool open(const std::string& modulePath, const std::string& title,
              ParameterEditCallback parameterEdit = {},
              StateCallback stateChanged = {},
              const ProcessorState& initialState = {},
              const std::vector<std::pair<std::uint32_t, double>>&
                  initialParameters = {});
    void close() noexcept;
    bool open() const noexcept;

public:
    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace transmission
