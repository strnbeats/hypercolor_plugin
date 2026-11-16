#include "dsp/fx/DelayEffect.h"

#include <array>
#include <cmath>
#include <iostream>

using namespace hypercolor::dsp::fx;

int main()
{
    DelayEffect effect;
    if (!effect.prepare({ 48000.0, 4096, 2 }))
        return 1;

    std::array<float, 4096> left {};
    std::array<float, 4096> right {};
    left[0] = 1.0f;
    EffectAudioBlock block { { std::span<float> { left }, std::span<float> { right } }, 2 };
    effect.setParameters({ DelayMode::pingPong, false, 20.5f, DelayDivision::quarter, 0.95f, 100.0f, 12000.0f, 1.0f, 1.0f, 120.0f });
    effect.process(block);
    for (const auto value : left)
        if (!std::isfinite(value) || std::abs(value) > 4.0f)
            return 2;
    for (const auto value : right)
        if (!std::isfinite(value) || std::abs(value) > 4.0f)
            return 3;

    effect.setParameters({ DelayMode::stereo, false, 350.0f, DelayDivision::quarter, 0.0f, 100.0f, 12000.0f, 0.0f, 1.0f, 120.0f });
    effect.reset();
    left.fill(0.25f);
    right.fill(-0.25f);
    const auto original = left;
    effect.process(block);
    for (std::size_t i = 0; i < left.size(); ++i)
        if (std::abs(left[i] - original[i]) > 1.0e-6f)
            return 4;

    std::cout << "All DelayEffect tests passed.\n";
    return 0;
}
