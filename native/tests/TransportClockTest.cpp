// native/tests/TransportClockTest.cpp

#include "transmission/TransportClock.h"

#include <cassert>
#include <cmath>

int main() {
    transmission::TransportClock clock(48000.0);
    assert(clock.setTempo(60.0, 1.0));
    clock.start();
    auto change = clock.advance(48000);
    assert(change.segmentCount > 1);
    assert(std::abs(clock.positionBeats() - 1.5) < 0.0001);

    assert(clock.setLoop(1.0, 2.0, true));
    clock.seek(1.5);
    auto looped = clock.advance(48000);
    assert(looped.wrapped);
    assert(std::abs(clock.positionBeats() - 1.5) < 0.0001);
    clock.stop(true);
    assert(clock.positionBeats() == 0.0);
    return 0;
}
