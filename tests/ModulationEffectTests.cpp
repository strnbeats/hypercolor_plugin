#include "dsp/fx/ModulationEffect.h"
#include <array>
#include <cmath>
#include <iostream>
using namespace hypercolor::dsp::fx;
using hypercolor::core::ProcessSpec;
int main()
{
    ModulationEffect effect;
    if (!effect.prepare({ 48000.0, 4096, 2 })) return 1;
    ModulationParameters p;
    p.mode = ModulationMode::flanger; p.mix = 1.0f; p.feedback = 0.95f;
    effect.setParameters(p);
    std::array<float, 4096> left {}, right {};
    left[0] = 1.0f;
    EffectAudioBlock block { { std::span<float> { left }, std::span<float> { right } }, 2 };
    effect.process(block);
    for (auto value : left) if (!std::isfinite(value)) return 2;
    for (auto value : right) if (!std::isfinite(value)) return 3;
    effect.reset();
    std::cout << "All ModulationEffect tests passed.\n";
    return 0;
}
