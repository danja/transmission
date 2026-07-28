#include "transmission/JackConnectionManager.h"

#ifdef TRANSMISSION_UI_WITH_JACK

#include <jack/jack.h>

#include <algorithm>

namespace transmission {

struct JackConnectionManager::Impl {
    jack_client_t* client = nullptr;
    ~Impl() {
        if (client) jack_client_close(client);
    }
};

JackConnectionManager::JackConnectionManager() : impl_(std::make_unique<Impl>()) {
    jack_status_t status = JackFailure;
    impl_->client = jack_client_open("transmission_ui", JackNoStartServer, &status);
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
    return listPorts(impl_->client, JackPortIsOutput);
}

std::vector<std::string> JackConnectionManager::outputDestinations() const {
    return listPorts(impl_->client, JackPortIsInput);
}

static bool clearConnections(jack_client_t* client, jack_port_t* port, bool portIsInput,
                             std::string& error) {
    const auto* current = jack_port_get_connections(port);
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
    if (!impl_->client) {
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
    if (jack_connect(impl_->client, source.c_str(), targetName.c_str()) != 0) {
        error = "unable to connect JACK source to " + targetName;
        return false;
    }
    return true;
}

bool JackConnectionManager::connectOutput(std::size_t channel, const std::string& destination,
                                           std::string& error) {
    if (!impl_->client) {
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
    if (jack_connect(impl_->client, sourceName.c_str(), destination.c_str()) != 0) {
        error = "unable to connect " + sourceName + " to JACK destination";
        return false;
    }
    return true;
}

bool JackConnectionManager::available() const noexcept { return impl_ && impl_->client; }

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
bool JackConnectionManager::available() const noexcept { return false; }
} // namespace transmission

#endif
