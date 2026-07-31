// native/tests/AudioProcessorTest.cpp

#include "transmission/AudioProcessor.h"

#include <cassert>
#include <cmath>

int main() {
    constexpr float input[] = {0.25F, -0.5F, 0.75F, 1.0F};
    float output[] = {0.0F, 0.0F, 0.0F, 0.0F};
    const float* inputs[] = {input};
    float* outputs[] = {output};
    transmission::PassThroughProcessor processor;
    processor.process(inputs, outputs, 1, 4);
    for (int i = 0; i < 4; ++i) assert(output[i] == input[i]);

    transmission::GainProcessor gain(
        4.0, 0.0, {{0.0, 0.0, true}, {1.0, -120.0, true}});
    gain.setProcessContext({0.0, 60.0, true});
    gain.process(inputs, outputs, 1, 4);
    assert(std::fabs(output[0] - input[0]) < 0.0001F);
    assert(std::fabs(output[3]) < std::fabs(input[3]) * 0.001F);
    return 0;
}
