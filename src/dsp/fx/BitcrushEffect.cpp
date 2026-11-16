#include "dsp/fx/BitcrushEffect.h"

#include <algorithm>
#include <cmath>

namespace hypercolor::dsp::fx
{
BitcrushParameters BitcrushEffect::sanitise(const BitcrushParameters& input) noexcept
{
    BitcrushParameters result;
    result.bits = std::isfinite(input.bits) ? std::clamp(input.bits, 4.0f, 24.0f) : 16.0f;
    result.rateHz = std::isfinite(input.rateHz) ? std::clamp(input.rateHz, 500.0f, 44100.0f) : 22050.0f;
    result.mode = input.mode == BitcrushMode::truncate ? BitcrushMode::truncate : BitcrushMode::round;
    result.mix = std::isfinite(input.mix) ? std::clamp(input.mix, 0.0f, 1.0f) : 1.0f;
    result.outputDb = std::isfinite(input.outputDb) ? std::clamp(input.outputDb, -24.0f, 12.0f) : 0.0f;
    return result;
}

bool BitcrushEffect::prepare(const core::ProcessSpec& spec) noexcept
{
    prepared = false;
    if (!std::isfinite(spec.sampleRate) || spec.sampleRate <= 0.0
        || spec.maximumBlockSize == 0 || (spec.numChannels != 1 && spec.numChannels != 2))
        return false;
    sampleRate = spec.sampleRate;
    numChannels = spec.numChannels;
    reset();
    prepared = true;
    return true;
}

void BitcrushEffect::reset() noexcept
{
    phase = 0.0;
    held.fill(0.0f);
    current = sanitise(target);
}

void BitcrushEffect::setParameters(const BitcrushParameters& parameters) noexcept
{
    target = sanitise(parameters);
}

float BitcrushEffect::quantise(float input, float bits, BitcrushMode mode) noexcept
{
    const auto levels = std::ldexp(1.0f, static_cast<int>(bits));
    const auto scaled = input * levels;
    const auto quantised = mode == BitcrushMode::truncate
        ? std::trunc(scaled) : std::round(scaled);
    return quantised / levels;
}

void BitcrushEffect::process(EffectAudioBlock& block) noexcept
{
    if (!prepared || block.numChannels != numChannels || block.channels[0].empty()
        || (numChannels == 2 && block.channels[1].size() != block.channels[0].size()))
        return;

    for (std::size_t sample = 0; sample < block.channels[0].size(); ++sample)
    {
        current.bits += smoothing * (target.bits - current.bits);
        current.rateHz += smoothing * (target.rateHz - current.rateHz);
        current.mix += smoothing * (target.mix - current.mix);
        current.outputDb += smoothing * (target.outputDb - current.outputDb);
        current.mode = target.mode;
        const auto left = std::isfinite(block.channels[0][sample]) ? block.channels[0][sample] : 0.0f;
        const auto right = numChannels == 2 && std::isfinite(block.channels[1][sample]) ? block.channels[1][sample] : left;
        phase += std::min(1.0, static_cast<double>(current.rateHz) / sampleRate);
        if (phase >= 1.0)
        {
            held[0] = quantise(left, current.bits, current.mode);
            held[1] = quantise(right, current.bits, current.mode);
            phase -= 1.0;
        }
        const auto gain = std::pow(10.0f, current.outputDb / 20.0f);
        const auto outputLeft = left + current.mix * (held[0] * gain - left);
        const auto outputRight = right + current.mix * (held[1] * gain - right);
        block.channels[0][sample] = std::isfinite(outputLeft) ? outputLeft : 0.0f;
        if (numChannels == 2)
            block.channels[1][sample] = std::isfinite(outputRight) ? outputRight : 0.0f;
    }
}
}
