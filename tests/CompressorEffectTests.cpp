#include "dsp/fx/CompressorEffect.h"

#include <array>
#include <cmath>
#include <iostream>

using namespace hypercolor::dsp::fx;

int main()
{
    CompressorEffect effect;
    if (!effect.prepare({ 48000.0, 4096, 2 })) return 1;
    std::array<float, 4096> left {};
    std::array<float, 4096> right {};
    left.fill(1.0f); right.fill(0.5f);
    EffectAudioBlock block { { std::span<float> { left }, std::span<float> { right } }, 2 };
    effect.setParameters({ -30.0f, 20.0f, 0.1f, 10.0f, 0.0f, 0.0f, 1.0f, 1.0f });
    effect.process(block);
    for (auto value : left) if (!std::isfinite(value) || std::abs(value) > 2.0f) return 2;
    for (auto value : right) if (!std::isfinite(value) || std::abs(value) > 2.0f) return 3;
    effect.setParameters({ -18.0f, 4.0f, 10.0f, 100.0f, 6.0f, 0.0f, 0.0f, 0.0f });
    effect.reset(); left.fill(0.25f); right.fill(-0.25f);
    const auto original = left;
    effect.process(block);
    for (std::size_t i = 0; i < left.size(); ++i) if (std::abs(left[i] - original[i]) > 1.0e-6f) return 4;
    std::cout << "All CompressorEffect tests passed.\n";
    return 0;
}
