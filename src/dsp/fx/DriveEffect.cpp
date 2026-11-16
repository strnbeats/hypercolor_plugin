#include "dsp/fx/DriveEffect.h"

#include <algorithm>
#include <cmath>

namespace hypercolor::dsp::fx
{
DriveParameters DriveEffect::sanitise(const DriveParameters& input) noexcept
{
    DriveParameters result;
    result.mode = input.mode == DriveMode::hardClip
        ? DriveMode::hardClip
        : (input.mode == DriveMode::tube ? DriveMode::tube : DriveMode::softClip);
    result.driveDb = std::isfinite(input.driveDb) ? std::clamp(input.driveDb, 0.0f, 36.0f) : 0.0f;
    result.tone = std::isfinite(input.tone) ? std::clamp(input.tone, 0.0f, 1.0f) : 0.5f;
    result.mix = std::isfinite(input.mix) ? std::clamp(input.mix, 0.0f, 1.0f) : 1.0f;
    result.outputDb = std::isfinite(input.outputDb) ? std::clamp(input.outputDb, -24.0f, 12.0f) : 0.0f;
    return result;
}

bool DriveEffect::prepare(const core::ProcessSpec& spec) noexcept
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

void DriveEffect::reset() noexcept
{
    toneState.fill(0.0f);
    dcInputState.fill(0.0f);
    dcOutputState.fill(0.0f);
    current = sanitise(target);
}

void DriveEffect::setParameters(const DriveParameters& parameters) noexcept
{
    target = sanitise(parameters);
}

float DriveEffect::shape(float input, DriveMode mode) noexcept
{
    switch (mode)
    {
        case DriveMode::hardClip:
            return std::clamp(input, -1.0f, 1.0f);
        case DriveMode::tube:
        {
            // Unequal positive/negative slopes create a restrained asymmetry.
            const auto positive = std::tanh(1.25f * input);
            const auto negative = 0.86f * std::tanh(1.55f * input);
            return input >= 0.0f ? positive : negative;
        }
        case DriveMode::softClip:
        default:
            return std::tanh(input);
    }
}

void DriveEffect::process(EffectAudioBlock& block) noexcept
{
    if (!prepared || block.numChannels != numChannels
        || block.channels[0].empty()
        || (numChannels == 2 && block.channels[1].size() != block.channels[0].size()))
        return;

    for (std::size_t sample = 0; sample < block.channels[0].size(); ++sample)
    {
        current.driveDb += smoothing * (target.driveDb - current.driveDb);
        current.tone += smoothing * (target.tone - current.tone);
        current.mix += smoothing * (target.mix - current.mix);
        current.outputDb += smoothing * (target.outputDb - current.outputDb);
        current.mode = target.mode;
        const auto driveGain = std::pow(10.0f, current.driveDb / 20.0f);
        const auto outputGain = std::pow(10.0f, current.outputDb / 20.0f);
        const auto toneAlpha = 1.0f - std::exp(-2.0f * 3.14159265358979323846f
            * (80.0f + 12000.0f * current.tone) / static_cast<float>(sampleRate));

        const auto processChannel = [&](std::size_t channel) noexcept
        {
            const auto input = std::isfinite(block.channels[channel][sample])
                ? block.channels[channel][sample] : 0.0f;
            const auto driveAmount = current.driveDb / 36.0f;
            const auto shaped = shape(input * driveGain, current.mode);
            const auto driven = input + driveAmount * (shaped - input);
            toneState[channel] += toneAlpha * (driven - toneState[channel]);
            const auto toned = toneState[channel] * (1.0f - current.tone) + driven * current.tone;
            const auto dcRemoved = toned - dcInputState[channel]
                + dcPole * dcOutputState[channel];
            dcInputState[channel] = toned;
            dcOutputState[channel] = dcRemoved;
            const auto wet = dcRemoved * outputGain;
            const auto output = (input + current.mix * (wet - input));
            block.channels[channel][sample] = std::isfinite(output) ? output : 0.0f;
        };

        processChannel(0);
        if (numChannels == 2)
            processChannel(1);
    }
}
}
