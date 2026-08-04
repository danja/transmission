#include "transmission/JackConnectionManager.h"
#include "transmission/JackPortIdentity.h"

#ifdef TRANSMISSION_UI_WITH_JACK

#include <jack/jack.h>

#include <algorithm>
#include <limits>

namespace transmission {

struct JackConnectionManager::Impl {
    mutable jack_client_t* client = nullptr;
    bool ensureClient() const {
        if (client) return true;
        jack_status_t status = JackFailure;
        client = jack_client_open("transmission_ui", JackNoStartServer, &status);
        return client != nullptr;
    }
    ~Impl() {
        if (client) jack_client_close(client);
    }
};

JackConnectionManager::JackConnectionManager() : impl_(std::make_unique<Impl>()) {
    impl_->ensureClient();
}

JackConnectionManager::~JackConnectionManager() = default;

static std::vector<std::string> listPorts(jack_client_t* client, const char* type,
                                          unsigned flags) {
    if (!client) return {};
    const auto* ports = jack_get_ports(client, nullptr, type, flags);
    if (!ports) return {};
    std::vector<std::string> result;
    for (std::size_t index = 0; ports[index]; ++index) result.emplace_back(ports[index]);
    jack_free(const_cast<char**>(ports));
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<std::string> JackConnectionManager::inputSources() const {
    return impl_->ensureClient() ? listPorts(impl_->client, JACK_DEFAULT_AUDIO_TYPE,
                                             JackPortIsOutput)
                                 : std::vector<std::string>{};
}

std::vector<std::string> JackConnectionManager::outputDestinations() const {
    return impl_->ensureClient() ? listPorts(impl_->client, JACK_DEFAULT_AUDIO_TYPE,
                                             JackPortIsInput)
                                 : std::vector<std::string>{};
}

std::vector<std::string> JackConnectionManager::midiInputSources() const {
    return impl_->ensureClient() ? listPorts(impl_->client, JACK_DEFAULT_MIDI_TYPE,
                                             JackPortIsOutput)
                                 : std::vector<std::string>{};
}

std::vector<std::string> JackConnectionManager::midiOutputDestinations() const {
    return impl_->ensureClient() ? listPorts(impl_->client, JACK_DEFAULT_MIDI_TYPE,
                                             JackPortIsInput)
                                 : std::vector<std::string>{};
}

static bool clearConnections(jack_client_t* client, jack_port_t* port, bool portIsInput,
                             std::string& error) {
    const auto* current = jack_port_get_all_connections(client, port);
    if (!current) return true;
    const auto portName = jack_port_name(port);
    for (std::size_t index = 0; current[index]; ++index) {
        const auto* source = portIsInput ? current[index] : portName;
        const auto* destination = portIsInput ? portName : current[index];
        if (jack_disconnect(client, source, destination) != 0) {
            error = "unable to disconnect existing JACK route";
            jack_free(const_cast<char**>(current));
            return false;
        }
    }
    jack_free(const_cast<char**>(current));
    return true;
}

bool JackConnectionManager::connectInput(std::size_t channel, const std::string& source,
                                          std::string& error) {
    if (!impl_->ensureClient()) {
        error = "JACK is not available";
        return false;
    }
    const auto targetName = resolveJackPortName(
        "transmission:in_" + std::to_string(channel + 1),
        listPorts(impl_->client, JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput));
    auto* target = jack_port_by_name(impl_->client, targetName.c_str());
    if (!target) {
        error = "Transmission JACK input port is not registered: " + targetName;
        return false;
    }
    if (!clearConnections(impl_->client, target, true, error)) return false;
    if (source == "No connection") return true;
    const auto resolved = resolveJackPortName(
        source, listPorts(impl_->client, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput));
    if (jack_connect(impl_->client, resolved.c_str(), targetName.c_str()) != 0) {
        error = "unable to connect JACK source to " + targetName;
        return false;
    }
    return true;
}

bool JackConnectionManager::connectOutput(std::size_t channel, const std::string& destination,
                                           std::string& error) {
    if (!impl_->ensureClient()) {
        error = "JACK is not available";
        return false;
    }
    const auto sourceName = resolveJackPortName(
        "transmission:out_" + std::to_string(channel + 1),
        listPorts(impl_->client, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput));
    auto* source = jack_port_by_name(impl_->client, sourceName.c_str());
    if (!source) {
        error = "Transmission JACK output port is not registered: " + sourceName;
        return false;
    }
    if (!clearConnections(impl_->client, source, false, error)) return false;
    if (destination == "No connection") return true;
    const auto resolved = resolveJackPortName(
        destination, listPorts(impl_->client, JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput));
    if (jack_connect(impl_->client, sourceName.c_str(), resolved.c_str()) != 0) {
        error = "unable to connect " + sourceName + " to JACK destination";
        return false;
    }
    return true;
}

bool JackConnectionManager::connectMidiInput(std::size_t port,
                                              const std::string& source,
                                              std::string& error) {
    if (!impl_->ensureClient()) {
        error = "JACK is not available";
        return false;
    }
    const auto targetName = resolveJackPortName(
        "transmission:midi_in_" + std::to_string(port + 1),
        listPorts(impl_->client, JACK_DEFAULT_MIDI_TYPE, JackPortIsInput));
    auto* target = jack_port_by_name(impl_->client, targetName.c_str());
    if (!target) {
        error = "Transmission JACK MIDI input is not registered: " + targetName;
        return false;
    }
    if (!clearConnections(impl_->client, target, true, error)) return false;
    if (source == "No connection") return true;
    const auto resolved = resolveJackPortName(
        source, listPorts(impl_->client, JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput));
    if (jack_connect(impl_->client, resolved.c_str(), targetName.c_str()) != 0) {
        error = "unable to connect JACK MIDI source to " + targetName;
        return false;
    }
    return true;
}

bool JackConnectionManager::connectMidiOutput(std::size_t port,
                                               const std::string& destination,
                                               std::string& error) {
    if (!impl_->ensureClient()) {
        error = "JACK is not available";
        return false;
    }
    const auto sourceName = resolveJackPortName(
        "transmission:midi_out_" + std::to_string(port + 1),
        listPorts(impl_->client, JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput));
    auto* source = jack_port_by_name(impl_->client, sourceName.c_str());
    if (!source) {
        error = "Transmission JACK MIDI output is not registered: " + sourceName;
        return false;
    }
    if (!clearConnections(impl_->client, source, false, error)) return false;
    if (destination == "No connection") return true;
    const auto resolved = resolveJackPortName(
        destination, listPorts(impl_->client, JACK_DEFAULT_MIDI_TYPE, JackPortIsInput));
    if (jack_connect(impl_->client, sourceName.c_str(), resolved.c_str()) != 0) {
        error = "unable to connect " + sourceName + " to JACK MIDI destination";
        return false;
    }
    return true;
}

bool JackConnectionManager::deviceConfig(AudioDeviceConfig& config,
                                         std::string& error) const {
    if (!impl_ || !impl_->ensureClient()) {
        error = "JACK server is not available";
        return false;
    }
    config.blockSize = static_cast<std::size_t>(jack_get_buffer_size(impl_->client));
    config.sampleRate = static_cast<double>(jack_get_sample_rate(impl_->client));
    if (config.blockSize == 0 || config.sampleRate <= 0.0) {
        error = "JACK returned an invalid sample rate or block size";
        return false;
    }
    return true;
}

bool JackConnectionManager::setBufferSize(std::size_t frames,
                                          std::string& error) {
    if (!impl_ || !impl_->ensureClient()) {
        error = "JACK server is not available";
        return false;
    }
    if (frames == 0 || (frames & (frames - 1)) != 0 ||
        frames > std::numeric_limits<jack_nframes_t>::max()) {
        error = "JACK buffer size must be a positive power of two";
        return false;
    }
    const auto previous = jack_get_buffer_size(impl_->client);
    const auto requestResult = jack_set_buffer_size(
        impl_->client, static_cast<jack_nframes_t>(frames));
    const auto adopted = jack_get_buffer_size(impl_->client);
    if (requestResult == 0 &&
        adopted == static_cast<jack_nframes_t>(frames)) {
        error.clear();
        return true;
    }
    const bool restored =
        previous != 0 &&
        jack_set_buffer_size(impl_->client, previous) == 0 &&
        jack_get_buffer_size(impl_->client) == previous;
    if (requestResult != 0)
        error = "JACK rejected the requested " + std::to_string(frames) +
                "-frame period";
    else
        error = "JACK/PipeWire kept a " + std::to_string(adopted) +
                "-frame period instead of the requested " +
                std::to_string(frames) + " frames";
    if (restored) {
        error += "; audio will use the restored " +
                 std::to_string(previous) + "-frame period";
        return true;
    }
    error += "; restart the audio server before trying again";
    return false;
}

std::vector<std::string> JackConnectionManager::portConnections(const std::string& portName) const {
    if (!impl_->ensureClient()) return {};
    auto* port = jack_port_by_name(impl_->client, portName.c_str());
    if (!port) return {};
    const auto** connections = jack_port_get_all_connections(impl_->client, port);
    if (!connections) return {};
    std::vector<std::string> result;
    for (std::size_t i = 0; connections[i]; ++i) result.emplace_back(connections[i]);
    jack_free(const_cast<void*>(static_cast<const void*>(connections)));
    return result;
}

bool JackConnectionManager::available() const noexcept {
    return impl_ && impl_->ensureClient();
}

} // namespace transmission

#else

namespace transmission {
struct JackConnectionManager::Impl {};
JackConnectionManager::JackConnectionManager() : impl_(std::make_unique<Impl>()) {}
JackConnectionManager::~JackConnectionManager() = default;
std::vector<std::string> JackConnectionManager::inputSources() const { return {}; }
std::vector<std::string> JackConnectionManager::outputDestinations() const { return {}; }
std::vector<std::string> JackConnectionManager::midiInputSources() const { return {}; }
std::vector<std::string> JackConnectionManager::midiOutputDestinations() const { return {}; }
bool JackConnectionManager::connectInput(std::size_t, const std::string&, std::string& error) {
    error = "JACK support is not enabled";
    return false;
}
bool JackConnectionManager::connectOutput(std::size_t, const std::string&, std::string& error) {
    error = "JACK support is not enabled";
    return false;
}
bool JackConnectionManager::connectMidiInput(std::size_t, const std::string&,
                                              std::string& error) {
    error = "JACK support is not enabled";
    return false;
}
bool JackConnectionManager::connectMidiOutput(std::size_t, const std::string&,
                                               std::string& error) {
    error = "JACK support is not enabled";
    return false;
}
bool JackConnectionManager::deviceConfig(AudioDeviceConfig&, std::string& error) const {
    error = "JACK support is not enabled";
    return false;
}
bool JackConnectionManager::setBufferSize(std::size_t, std::string& error) {
    error = "JACK support is not enabled";
    return false;
}
std::vector<std::string> JackConnectionManager::portConnections(const std::string&) const { return {}; }
bool JackConnectionManager::available() const noexcept { return false; }
} // namespace transmission

#endif
