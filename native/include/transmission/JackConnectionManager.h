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
    std::vector<std::string> midiInputSources() const;
    std::vector<std::string> midiOutputDestinations() const;
    bool connectInput(std::size_t channel, const std::string& source, std::string& error);
    bool connectOutput(std::size_t channel, const std::string& destination, std::string& error);
    bool connectMidiInput(std::size_t port, const std::string& source, std::string& error);
    bool connectMidiOutput(std::size_t port, const std::string& destination, std::string& error);
    bool deviceConfig(AudioDeviceConfig& config, std::string& error) const;
    bool setBufferSize(std::size_t frames, std::string& error);
    bool available() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace transmission
