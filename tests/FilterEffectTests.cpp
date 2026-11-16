#include "dsp/fx/FilterEffect.h"

#include <array>
#include <cmath>
#include <iostream>

using namespace hypercolor::dsp::fx;

int main()
{
    FilterEffect effect;
    if (!effect.prepare({ 48000.0, 4096, 2 })) return 1;
    std::array<float, 4096> left {};
    std::array<float, 4096> right {};
    left.fill(0.25f); right.fill(-0.25f);
    EffectAudioBlock block { { std::span<float> { left }, std::span<float> { right } }, 2 };
    for (const auto mode : { FilterMode::lowPass, FilterMode::highPass, FilterMode::bandPass, FilterMode::notch })
    {
        effect.reset();
        effect.setParameters({ mode, 20.0f, 1.0f, 1.0f, 1.0f, 0.0f });
        effect.process(block);
        for (const auto value : left)
            if (!std::isfinite(value) || std::abs(value) > 4.0f) return 2;
    }
    effect.setParameters({ FilterMode::lowPass, 10000.0f, 0.2f, 0.0f, 0.0f, 0.0f });
    effect.reset();
    left.fill(0.3f); right.fill(-0.3f);
    const auto original = left;
    effect.process(block);
    for (std::size_t i = 0; i < left.size(); ++i)
        if (std::abs(left[i] - original[i]) > 1.0e-6f) return 3;
    std::cout << "All FilterEffect tests passed.\n";
    return 0;
}
