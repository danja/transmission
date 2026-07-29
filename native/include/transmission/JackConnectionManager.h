#pragma once

#include "AudioDevice.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace transmission {

/** Control-plane JACK port discovery and connection management for the UI. */
class JackConnectionManager {
public:
    JackConnectionManager();
    ~JackConnectionManager();

    JackConnectionManager(const JackConnectionManager&) = delete;
    JackConnectionManager& operator=(const JackConnectionManager&) = delete;

    std::vector<std::string> inputSources() const;
    std::vector<std::string> outputDestinations() const;
    bool connectInput(std::size_t channel, const std::string& source, std::string& error);
    bool connectOutput(std::size_t channel, const std::string& destination, std::string& error);
    bool deviceConfig(AudioDeviceConfig& config, std::string& error) const;
    bool available() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace transmission
