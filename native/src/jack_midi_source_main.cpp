#include <jack/jack.h>
#include <jack/midiport.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <thread>

namespace {
struct Source {
    jack_port_t* output = nullptr;
    std::uint64_t callbacks = 0;
};

int process(jack_nframes_t frames, void* opaque) noexcept {
    auto* source = static_cast<Source*>(opaque);
    auto* buffer = jack_port_get_buffer(source->output, frames);
    if (!buffer) return 0;
    jack_midi_clear_buffer(buffer);
    if (source->callbacks % 20 == 0) {
        const std::uint8_t message[] = {
            static_cast<std::uint8_t>((source->callbacks / 20) % 2 == 0 ? 0x90 : 0x80),
            60, 100};
        if (auto* destination = jack_midi_event_reserve(buffer, 0, sizeof(message)))
            std::memcpy(destination, message, sizeof(message));
    }
    ++source->callbacks;
    return 0;
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: transmission_jack_midi_source <target-port>\n";
        return 2;
    }
    jack_status_t status = JackFailure;
    auto* client = jack_client_open("transmission-midi-test", JackNoStartServer, &status);
    if (!client) return 1;
    Source source;
    source.output = jack_port_register(client, "out", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);
    if (!source.output || jack_set_process_callback(client, process, &source) != 0 ||
        jack_activate(client) != 0 ||
        jack_connect(client, jack_port_name(source.output), argv[1]) != 0) {
        jack_client_close(client);
        return 1;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    jack_deactivate(client);
    jack_client_close(client);
    return 0;
}
