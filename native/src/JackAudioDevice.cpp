#include "transmission/JackAudioDevice.h"

#include <jack/jack.h>
#include <jack/midiport.h>

#include <algorithm>

namespace transmission {

JackAudioDevice::~JackAudioDevice() { stop(); }

bool JackAudioDevice::configure(const AudioDeviceConfig& config) {
    if (running_.load(std::memory_order_acquire) || config.channels == 0 ||
        config.blockSize == 0 || config.sampleRate <= 0.0) {
        return false;
    }
    closeClient();
    lastError_.clear();

    jack_status_t status = JackFailure;
    client_ = jack_client_open("transmission", JackNoStartServer, &status);
    if (!client_) {
        lastError_ = "unable to open JACK client";
        return false;
    }
    config_ = config;
    if (jack_get_sample_rate(client_) != static_cast<jack_nframes_t>(config.sampleRate) ||
        jack_get_buffer_size(client_) != static_cast<jack_nframes_t>(config.blockSize)) {
        lastError_ = "JACK sample rate or block size does not match configuration";
        closeClient();
        return false;
    }
    try {
        inputPorts_.resize(config.channels);
        outputPorts_.resize(config.channels);
        midiInputPorts_.resize(config.midiInputs);
        midiOutputPorts_.resize(config.midiOutputs);
        inputPointers_.resize(config.channels);
        outputPointers_.resize(config.channels);
    } catch (...) {
        lastError_ = "unable to allocate JACK port metadata";
        closeClient();
        return false;
    }
    for (std::size_t channel = 0; channel < config.channels; ++channel) {
        const auto inputName = "in_" + std::to_string(channel + 1);
        const auto outputName = "out_" + std::to_string(channel + 1);
        inputPorts_[channel] = jack_port_register(client_, inputName.c_str(),
                                                   JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
        outputPorts_[channel] = jack_port_register(client_, outputName.c_str(),
                                                    JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
        if (!inputPorts_[channel] || !outputPorts_[channel]) {
            lastError_ = "unable to register JACK audio ports";
            closeClient();
            return false;
        }
    }
    for (std::size_t port = 0; port < config.midiInputs; ++port) {
        const auto name = "midi_in_" + std::to_string(port + 1);
        midiInputPorts_[port] = jack_port_register(client_, name.c_str(),
                                                    JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
        if (!midiInputPorts_[port]) {
            lastError_ = "unable to register JACK MIDI input ports";
            closeClient();
            return false;
        }
    }
    for (std::size_t port = 0; port < config.midiOutputs; ++port) {
        const auto name = "midi_out_" + std::to_string(port + 1);
        midiOutputPorts_[port] = jack_port_register(client_, name.c_str(),
                                                     JACK_DEFAULT_MIDI_TYPE,
                                                     JackPortIsOutput, 0);
        if (!midiOutputPorts_[port]) {
            lastError_ = "unable to register JACK MIDI output ports";
            closeClient();
            return false;
        }
    }
    if (jack_set_process_callback(client_, &JackAudioDevice::processThunk, this) != 0) {
        lastError_ = "unable to install JACK process callback";
        closeClient();
        return false;
    }
    if (jack_set_buffer_size_callback(
            client_, &JackAudioDevice::bufferSizeThunk, this) != 0) {
        lastError_ = "unable to install JACK buffer-size callback";
        closeClient();
        return false;
    }
    if (jack_set_xrun_callback(client_, &JackAudioDevice::xrunThunk, this) != 0) {
        lastError_ = "unable to install JACK xrun callback";
        closeClient();
        return false;
    }
    configured_ = true;
    if (config.autoConnect && !connectPhysicalPorts()) {
        closeClient();
        return false;
    }
    return true;
}

bool JackAudioDevice::connectPhysicalPorts() {
    if (!configured_ || !client_) return false;
    const auto connectDirection = [this](const std::vector<_jack_port*>& localPorts,
                                         unsigned remoteFlags, bool remoteToLocal) {
        const auto* remotePorts = jack_get_ports(client_, nullptr, JACK_DEFAULT_AUDIO_TYPE,
                                                 remoteFlags | JackPortIsPhysical);
        if (!remotePorts) return false;
        std::size_t remoteIndex = 0;
        for (; remoteIndex < localPorts.size() && remotePorts[remoteIndex]; ++remoteIndex) {
            const char* localName = jack_port_name(localPorts[remoteIndex]);
            const char* source = remoteToLocal ? remotePorts[remoteIndex] : localName;
            const char* destination = remoteToLocal ? localName : remotePorts[remoteIndex];
            if (jack_connect(client_, source, destination) != 0) {
                jack_free(const_cast<char**>(remotePorts));
                return false;
            }
        }
        const bool connected = remoteIndex == localPorts.size();
        jack_free(const_cast<char**>(remotePorts));
        return connected;
    };
    // Physical capture ports are JACK outputs; physical playback ports are
    // JACK inputs. Capture -> Transmission inputs, Transmission outputs -> playback.
    return connectDirection(inputPorts_, JackPortIsOutput, true) &&
           connectDirection(outputPorts_, JackPortIsInput, false);
}

bool JackAudioDevice::start(AudioCallback& callback) {
    if (!configured_ || running_.load(std::memory_order_acquire) || !client_) return false;
    callback_.store(&callback, std::memory_order_release);
    running_.store(true, std::memory_order_release);
    if (jack_activate(client_) != 0) {
        running_.store(false, std::memory_order_release);
        callback_.store(nullptr, std::memory_order_release);
        lastError_ = "unable to activate JACK client";
        return false;
    }
    // PipeWire may assign a different quantum after activation.
    config_.blockSize = static_cast<std::size_t>(jack_get_buffer_size(client_));
    return true;
}

std::size_t JackAudioDevice::actualBlockSize() const noexcept {
    return client_ ? static_cast<std::size_t>(jack_get_buffer_size(client_)) : 0;
}

void JackAudioDevice::stop() noexcept {
    running_.store(false, std::memory_order_release);
    callback_.store(nullptr, std::memory_order_release);
    if (client_) jack_deactivate(client_);
}

int JackAudioDevice::processThunk(unsigned frames, void* opaque) noexcept {
    return static_cast<JackAudioDevice*>(opaque)->process(frames);
}

int JackAudioDevice::bufferSizeThunk(unsigned frames, void* opaque) noexcept {
    auto& device = *static_cast<JackAudioDevice*>(opaque);
    if (frames != device.config_.blockSize) {
        if (auto* callback = device.callback_.load(std::memory_order_acquire);
            callback && device.running_.load(std::memory_order_acquire))
            callback->handleXrun();
    }
    return 0;
}

int JackAudioDevice::xrunThunk(void* opaque) noexcept {
    auto& device = *static_cast<JackAudioDevice*>(opaque);
    if (auto* callback = device.callback_.load(std::memory_order_acquire);
        callback && device.running_.load(std::memory_order_acquire))
        callback->handleXrun();
    return 0;
}

int JackAudioDevice::process(unsigned frames) noexcept {
    auto* callback = callback_.load(std::memory_order_acquire);
    if (!callback || !running_.load(std::memory_order_acquire) ||
        frames != config_.blockSize) {
        return 0;
    }
    for (std::size_t channel = 0; channel < config_.channels; ++channel) {
        inputPointers_[channel] = static_cast<const float*>(
            jack_port_get_buffer(inputPorts_[channel], frames));
        outputPointers_[channel] = static_cast<float*>(
            jack_port_get_buffer(outputPorts_[channel], frames));
        if (!inputPointers_[channel] || !outputPointers_[channel]) return 0;
    }
    for (std::size_t port = 0; port < midiInputPorts_.size(); ++port) {
        auto* midiBuffer = jack_port_get_buffer(midiInputPorts_[port], frames);
        if (!midiBuffer) continue;
        const auto eventCount = jack_midi_get_event_count(midiBuffer);
        for (uint32_t index = 0; index < eventCount; ++index) {
            jack_midi_event_t event{};
            if (jack_midi_event_get(&event, midiBuffer, index) != 0 ||
                event.size == 0 || event.size > 3) continue;
            MidiEvent midi;
            midi.frameOffset = event.time < frames ? event.time : frames - 1;
            midi.port = port;
            midi.size = static_cast<std::uint8_t>(event.size);
            std::copy_n(event.buffer, event.size, midi.data.begin());
            callback->handleMidi(midi);
        }
    }
    for (auto* port : midiOutputPorts_) {
        auto* midiBuffer = jack_port_get_buffer(port, frames);
        if (midiBuffer) jack_midi_clear_buffer(midiBuffer);
    }
    callback->process(inputPointers_.data(), outputPointers_.data(),
                      config_.channels, frames);
    const auto outputEventCount = callback->takeOutputMidi(
        midiOutputEvents_.data(), midiOutputEvents_.size());
    for (std::size_t index = 0; index < outputEventCount; ++index) {
        const auto& event = midiOutputEvents_[index];
        if (event.port >= midiOutputPorts_.size() || event.size == 0 ||
            event.size > event.data.size())
            continue;
        auto* midiBuffer = jack_port_get_buffer(midiOutputPorts_[event.port], frames);
        if (!midiBuffer) continue;
        jack_midi_event_write(
            midiBuffer, std::min<std::size_t>(event.frameOffset, frames - 1),
            event.data.data(), event.size);
    }
    return 0;
}

void JackAudioDevice::closeClient() noexcept {
    stop();
    if (client_) {
        jack_client_close(client_);
        client_ = nullptr;
    }
    inputPorts_.clear();
    outputPorts_.clear();
    midiInputPorts_.clear();
    midiOutputPorts_.clear();
    inputPointers_.clear();
    outputPointers_.clear();
    configured_ = false;
}

} // namespace transmission
