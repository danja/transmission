#include "transmission/JackConnectionManager.h"
#include "transmission/JackPortIdentity.h"

#ifdef TRANSMISSION_UI_WITH_JACK

#include <jack/jack.h>

#include <algorithm>

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

static std::vector<std::string> listPorts(jack_client_t* client, unsigned flags) {
    if (!client) return {};
    const auto* ports = jack_get_ports(client, nullptr, JACK_DEFAULT_AUDIO_TYPE, flags);
    if (!ports) return {};
    std::vector<std::string> result;
    for (std::size_t index = 0; ports[index]; ++index) result.emplace_back(ports[index]);
    jack_free(const_cast<char**>(ports));
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<std::string> JackConnectionManager::inputSources() const {
    return impl_->ensureClient() ? listPorts(impl_->client, JackPortIsOutput)
                                 : std::vector<std::string>{};
}

std::vector<std::string> JackConnectionManager::outputDestinations() const {
    return impl_->ensureClient() ? listPorts(impl_->client, JackPortIsInput)
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
    const auto targetName = "transmission:in_" + std::to_string(channel + 1);
    auto* target = jack_port_by_name(impl_->client, targetName.c_str());
    if (!target) {
        error = "Transmission JACK input port is not registered: " + targetName;
        return false;
    }
    if (!clearConnections(impl_->client, target, true, error)) return false;
    if (source == "No connection") return true;
    const auto resolved = resolveJackPortName(
        source, listPorts(impl_->client, JackPortIsOutput));
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
    const auto sourceName = "transmission:out_" + std::to_string(channel + 1);
    auto* source = jack_port_by_name(impl_->client, sourceName.c_str());
    if (!source) {
        error = "Transmission JACK output port is not registered: " + sourceName;
        return false;
    }
    if (!clearConnections(impl_->client, source, false, error)) return false;
    if (destination == "No connection") return true;
    const auto resolved = resolveJackPortName(
        destination, listPorts(impl_->client, JackPortIsInput));
    if (jack_connect(impl_->client, sourceName.c_str(), resolved.c_str()) != 0) {
        error = "unable to connect " + sourceName + " to JACK destination";
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
bool JackConnectionManager::connectInput(std::size_t, const std::string&, std::string& error) {
    error = "JACK support is not enabled";
    return false;
}
bool JackConnectionManager::connectOutput(std::size_t, const std::string&, std::string& error) {
    error = "JACK support is not enabled";
    return false;
}
bool JackConnectionManager::deviceConfig(AudioDeviceConfig&, std::string& error) const {
    error = "JACK support is not enabled";
    return false;
}
bool JackConnectionManager::available() const noexcept { return false; }
} // namespace transmission

#endif
