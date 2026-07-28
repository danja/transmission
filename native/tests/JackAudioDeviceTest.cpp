#include "transmission/JackAudioDevice.h"

#include <cassert>

int main() {
    transmission::JackAudioDevice device;
    assert(!device.configure({0, 256, 48000.0}));
    assert(!device.configure({2, 0, 48000.0}));
    return 0;
}
