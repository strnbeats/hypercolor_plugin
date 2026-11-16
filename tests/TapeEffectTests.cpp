#include "dsp/fx/TapeEffect.h"

#include <array>
#include <cmath>
#include <iostream>

using namespace hypercolor::dsp::fx;

int main()
{
    TapeEffect effect;
    if (!effect.prepare({ 48000.0, 4096, 2 }))
        return 1;

    std::array<float, 4096> left {};
    std::array<float, 4096> right {};
    left[0] = 1.0f;
    right[0] = -1.0f;
    EffectAudioBlock block { { std::span<float> { left }, std::span<float> { right } }, 2 };
    effect.setParameters({ 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 12.0f });
    effect.process(block);
    for (const auto value : left)
        if (!std::isfinite(value) || std::abs(value) > 8.0f)
            return 2;
    for (const auto value : right)
        if (!std::isfinite(value) || std::abs(value) > 8.0f)
            return 3;

    effect.setParameters({ 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f });
    effect.reset();
    left.fill(0.25f);
    right.fill(-0.25f);
    const auto original = left;
    effect.process(block);
    for (std::size_t i = 0; i < left.size(); ++i)
        if (std::abs(left[i] - original[i]) > 1.0e-6f)
            return 4;

    std::cout << "All TapeEffect tests passed.\n";
    return 0;
}
