// native/tests/AudioProcessorTest.cpp

#include "transmission/AudioProcessor.h"

#include <cassert>

int main() {
    constexpr float input[] = {0.25F, -0.5F, 0.75F, 1.0F};
    float output[] = {0.0F, 0.0F, 0.0F, 0.0F};
    const float* inputs[] = {input};
    float* outputs[] = {output};
    transmission::PassThroughProcessor processor;
    processor.process(inputs, outputs, 1, 4);
    for (int i = 0; i < 4; ++i) assert(output[i] == input[i]);
    return 0;
}
