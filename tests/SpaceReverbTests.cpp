#include "core/ProcessSpec.h"
#include "dsp/fx/SpaceReverb.h"
#include "dsp/fx/EffectChain.h"

#include <array>
#include <cmath>
#include <iostream>

namespace
{
using namespace hypercolor::core;
using namespace hypercolor::dsp::fx;

class Context
{
public:
    void expect(bool value, const char* message)
    {
        if (!value) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
    }
    int failures = 0;
};

void testImpulseAndParameters(Context& context)
{
    SpaceReverb reverb;
    context.expect(reverb.prepare({ 48000.0, 4096, 2 }), "stereo prepare");
    SpaceReverbParameters parameters;
    parameters.mix = 1.0f;
    parameters.decaySeconds = 20.0f;
    parameters.width = 2.0f;
    reverb.setParameters(parameters);

    std::array<float, 4096> left {};
    std::array<float, 4096> right {};
    left[0] = 1.0f;
    EffectAudioBlock block {
        { std::span<float> { left }, std::span<float> { right } }, 2
    };
    reverb.process(block);
    auto finite = true;
    auto nonZero = false;
    for (std::size_t index = 0; index < left.size(); ++index)
    {
        finite = finite && std::isfinite(left[index]) && std::isfinite(right[index]);
        nonZero = nonZero || std::abs(left[index]) > 1.0e-7f
            || std::abs(right[index]) > 1.0e-7f;
    }
    context.expect(finite, "impulse output must remain finite");
    context.expect(nonZero, "impulse should produce a response");
}

void testBypassResetAndMono(Context& context)
{
    SpaceReverb reverb;
    context.expect(reverb.prepare({ 44100.0, 64, 1 }), "mono prepare");
    reverb.setBypassed(true);
    std::array<float, 64> samples {};
    samples.fill(0.25f);
    const auto original = samples;
    EffectChain chain;
    context.expect(chain.addModule(&reverb), "reverb chain add");
    context.expect(chain.prepare({ 44100.0, 64, 1 }), "reverb chain prepare");
    EffectAudioBlock block { { std::span<float> { samples }, {} }, 1 };
    context.expect(
        chain.process(block) == EffectChainResult::success,
        "bypassed chain process"
    );
    context.expect(samples == original, "bypassed module is not processed");
    reverb.setBypassed(false);
    reverb.reset();
    context.expect(reverb.prepare({ 192000.0, 1, 1 }), "reprepare at 192 kHz");
    context.expect(reverb.prepare({ 96000.0, 257, 1 }), "repeated prepare");
}
}

int main()
{
    Context context;
    testImpulseAndParameters(context);
    testBypassResetAndMono(context);
    if (context.failures == 0)
    {
        std::cout << "All SpaceReverb tests passed.\n";
        return 0;
    }
    return 1;
}
