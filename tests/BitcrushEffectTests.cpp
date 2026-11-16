#include "dsp/fx/BitcrushEffect.h"

#include <array>
#include <cmath>
#include <iostream>

using namespace hypercolor::dsp::fx;

int main()
{
    BitcrushEffect effect;
    if (!effect.prepare({ 48000.0, 4096, 2 }))
        return 1;
    std::array<float, 4096> left {};
    std::array<float, 4096> right {};
    left[0] = 0.37f;
    right[0] = -0.37f;
    EffectAudioBlock block { { std::span<float> { left }, std::span<float> { right } }, 2 };
    effect.setParameters({ 4.0f, 500.0f, BitcrushMode::truncate, 1.0f, 0.0f });
    effect.process(block);
    for (const auto value : left)
        if (!std::isfinite(value)) return 2;
    for (const auto value : right)
        if (!std::isfinite(value)) return 3;

    effect.setParameters({ 24.0f, 44100.0f, BitcrushMode::round, 0.0f, 0.0f });
    effect.reset();
    left.fill(0.25f);
    right.fill(-0.25f);
    const auto original = left;
    effect.process(block);
    for (std::size_t i = 0; i < left.size(); ++i)
        if (std::abs(left[i] - original[i]) > 1.0e-6f) return 4;

    std::cout << "All BitcrushEffect tests passed.\n";
    return 0;
}
