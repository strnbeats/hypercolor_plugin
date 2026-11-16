#include "core/ProcessSpec.h"
#include "dsp/fx/SpaceReverb.h"
#include "dsp/fx/EffectChain.h"

#include <array>
#include <bit>
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

constexpr int expectedHadamardSign(std::size_t line, std::size_t source) noexcept
{
    return (std::popcount(static_cast<unsigned int>(line & source)) % 2 == 0)
        ? 1 : -1;
}

void testMixingMatrix(Context& context)
{
    for (std::size_t line = 0; line < 8; ++line)
        for (std::size_t other = 0; other < 8; ++other)
        {
            auto dot = 0;
            for (std::size_t source = 0; source < 8; ++source)
                dot += expectedHadamardSign(line, source) * expectedHadamardSign(other, source);

            context.expect(dot == (line == other ? 8 : 0), "mixing matrix must be orthogonal");
        }
}

void testLongTermStability(Context& context)
{
    constexpr std::array<double, 4> sampleRates { 44100.0, 48000.0, 96000.0, 192000.0 };
    constexpr std::array<std::size_t, 4> blockSizes { 1, 17, 257, 1024 };

    for (const auto sampleRate : sampleRates)
        for (const auto channels : { 1u, 2u })
            for (const auto blockSize : blockSizes)
                for (const auto maximumDecay : { false, true })
                {
                    SpaceReverb reverb;
                    context.expect(
                        reverb.prepare({ sampleRate, static_cast<std::uint32_t>(blockSize), channels }),
                        "long-term stability prepare"
                    );
                    SpaceReverbParameters parameters;
                    parameters.mix = 1.0f;
                    parameters.decaySeconds = maximumDecay ? 20.0f : 4.0f;
                    reverb.setParameters(parameters);

                    std::array<float, 1024> left {};
                    std::array<float, 1024> right {};
                    constexpr auto seconds = 2.0;
                    const auto totalSamples = static_cast<std::size_t>(sampleRate * seconds);
                    auto firstHalfPeak = 0.0f;
                    auto secondHalfPeak = 0.0f;
                    auto finite = true;

                    for (std::size_t offset = 0; offset < totalSamples; offset += blockSize)
                    {
                        const auto count = std::min(blockSize, totalSamples - offset);
                        left.fill(0.1f);
                        right.fill(0.1f);
                        EffectAudioBlock block {
                            { std::span<float> { left.data(), count },
                              std::span<float> { right.data(), count } }, channels
                        };
                        reverb.process(block);

                        for (std::size_t index = 0; index < count; ++index)
                        {
                            finite = finite && std::isfinite(left[index]);
                            if (channels == 2)
                                finite = finite && std::isfinite(right[index]);
                            const auto peak = std::max(
                                std::abs(left[index]),
                                channels == 2 ? std::abs(right[index]) : 0.0f
                            );
                            if (offset + index < totalSamples / 2)
                                firstHalfPeak = std::max(firstHalfPeak, peak);
                            else
                                secondHalfPeak = std::max(secondHalfPeak, peak);
                        }
                    }

                    context.expect(finite, "long-term output must remain finite");
                    context.expect(firstHalfPeak < 1000.0f, "initial output must remain bounded");
                    context.expect(secondHalfPeak < 1000.0f, "long-term output must remain bounded");
                    context.expect(
                        secondHalfPeak < 4.0f * std::max(1.0f, firstHalfPeak),
                        "feedback energy must not grow without bound"
                    );
                }
}

void testImpulseAndParameters(Context& context)
{
    for (const auto sampleRate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (const auto channels : { 1u, 2u })
        {
            SpaceReverb reverb;
            context.expect(
                reverb.prepare({ sampleRate, 4096, channels }),
                "impulse prepare"
            );
            SpaceReverbParameters parameters;
            parameters.mix = 1.0f;
            parameters.decaySeconds = 20.0f;
            parameters.width = 2.0f;
            reverb.setParameters(parameters);

            std::array<float, 4096> left {};
            std::array<float, 4096> right {};
            left[0] = 1.0f;
            EffectAudioBlock block {
                { std::span<float> { left }, std::span<float> { right } }, channels
            };
            reverb.process(block);
            auto finite = true;
            auto nonZero = false;
            for (std::size_t index = 0; index < left.size(); ++index)
            {
                finite = finite && std::isfinite(left[index]);
                if (channels == 2)
                    finite = finite && std::isfinite(right[index]);
                nonZero = nonZero || std::abs(left[index]) > 1.0e-7f
                    || (channels == 2 && std::abs(right[index]) > 1.0e-7f);
            }
            context.expect(finite, "impulse output must remain finite");
            context.expect(nonZero, "impulse should produce a response");
        }
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
    testMixingMatrix(context);
    testImpulseAndParameters(context);
    testBypassResetAndMono(context);
    testLongTermStability(context);
    if (context.failures == 0)
    {
        std::cout << "All SpaceReverb tests passed.\n";
        return 0;
    }
    return 1;
}
