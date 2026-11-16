#include "dsp/fx/ModulationEffect.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace hypercolor::dsp::fx
{
ModulationParameters ModulationEffect::sanitise(
    const ModulationParameters& input
) noexcept
{
    ModulationParameters result;
    result.mode = input.mode == ModulationMode::flanger
        ? ModulationMode::flanger : ModulationMode::chorus;
    result.rateHz = std::isfinite(input.rateHz) ? std::clamp(input.rateHz, 0.01f, 10.0f) : 0.3f;
    result.depth = std::isfinite(input.depth) ? std::clamp(input.depth, 0.0f, 1.0f) : 0.5f;
    result.baseDelayMilliseconds = std::isfinite(input.baseDelayMilliseconds)
        ? std::clamp(input.baseDelayMilliseconds, 0.1f, 20.0f) : 7.0f;
    result.feedback = std::isfinite(input.feedback) ? std::clamp(input.feedback, -0.95f, 0.95f) : 0.0f;
    result.mix = std::isfinite(input.mix) ? std::clamp(input.mix, 0.0f, 1.0f) : 0.3f;
    result.width = std::isfinite(input.width) ? std::clamp(input.width, 0.0f, 2.0f) : 1.0f;
    return result;
}

bool ModulationEffect::prepare(const core::ProcessSpec& spec) noexcept
{
    prepared = false;
    if (!std::isfinite(spec.sampleRate) || spec.sampleRate <= 0.0
        || spec.maximumBlockSize == 0 || (spec.numChannels != 1 && spec.numChannels != 2))
        return false;
    sampleRate = spec.sampleRate;
    numChannels = spec.numChannels;
    capacity = static_cast<std::size_t>(std::ceil(sampleRate * maximumDelaySeconds)) + 2;
    try
    {
        for (auto& channel : buffer)
            channel.assign(capacity, 0.0f);
    }
    catch (...)
    {
        for (auto& channel : buffer) channel.clear();
        return false;
    }
    reset();
    prepared = true;
    return true;
}

void ModulationEffect::reset() noexcept
{
    for (auto& channel : buffer)
        std::fill(channel.begin(), channel.end(), 0.0f);
    writeIndex = 0;
    phase = 0.0;
    current = sanitise(target);
    currentModeBlend = target.mode == ModulationMode::flanger ? 1.0f : 0.0f;
}

void ModulationEffect::setParameters(const ModulationParameters& input) noexcept
{
    target = sanitise(input);
    targetModeBlend = target.mode == ModulationMode::flanger ? 1.0f : 0.0f;
}

float ModulationEffect::readFractional(
    std::size_t channel,
    double delaySamples
) const noexcept
{
    const auto length = buffer[channel].size();
    const auto read = static_cast<double>(writeIndex) - delaySamples;
    auto wrapped = std::fmod(read, static_cast<double>(length));
    if (wrapped < 0.0) wrapped += static_cast<double>(length);
    const auto index = static_cast<std::size_t>(wrapped);
    const auto fraction = wrapped - static_cast<double>(index);
    const auto next = (index + 1) % length;
    return static_cast<float>(
        (1.0 - fraction) * buffer[channel][index]
        + fraction * buffer[channel][next]
    );
}

void ModulationEffect::process(EffectAudioBlock& block) noexcept
{
    if (!prepared || block.numChannels != numChannels)
        return;

    for (std::size_t sample = 0; sample < block.channels[0].size(); ++sample)
    {
        current.rateHz += smoothing * (target.rateHz - current.rateHz);
        current.depth += smoothing * (target.depth - current.depth);
        current.baseDelayMilliseconds += smoothing * (target.baseDelayMilliseconds - current.baseDelayMilliseconds);
        current.feedback += smoothing * (target.feedback - current.feedback);
        current.mix += smoothing * (target.mix - current.mix);
        current.width += smoothing * (target.width - current.width);
        currentModeBlend += smoothing * (targetModeBlend - currentModeBlend);

        const auto inputLeft = std::isfinite(block.channels[0][sample]) ? block.channels[0][sample] : 0.0f;
        const auto inputRight = numChannels == 2 && std::isfinite(block.channels[1][sample])
            ? block.channels[1][sample] : inputLeft;
        const auto delayBase = static_cast<double>(current.baseDelayMilliseconds)
            * sampleRate / 1000.0;
        const auto modeDepth = 12.0 + (4.0 - 12.0) * static_cast<double>(currentModeBlend);
        const auto modulation = modeDepth * static_cast<double>(current.depth)
            * (0.5 + 0.5 * std::sin(2.0 * std::numbers::pi * phase));
        const auto leftDelay = std::max(1.0, delayBase + modulation);
        const auto rightDelay = std::max(1.0, delayBase + modulation * 0.83);
        const auto delayedLeft = readFractional(0, std::min(leftDelay, static_cast<double>(capacity - 2)));
        const auto delayedRight = readFractional(1, std::min(rightDelay, static_cast<double>(capacity - 2)));
        buffer[0][writeIndex] = inputLeft + delayedLeft * current.feedback;
        buffer[1][writeIndex] = inputRight + delayedRight * current.feedback;
        const auto wetMid = 0.5f * (delayedLeft + delayedRight);
        const auto wetSide = 0.5f * (delayedLeft - delayedRight) * current.width;
        const auto wetLeft = wetMid + wetSide;
        const auto wetRight = wetMid - wetSide;
        block.channels[0][sample] = inputLeft + current.mix * (wetLeft - inputLeft);
        if (numChannels == 2)
            block.channels[1][sample] = inputRight + current.mix * (wetRight - inputRight);
        writeIndex = (writeIndex + 1) % capacity;
        phase += static_cast<double>(current.rateHz) / sampleRate;
        if (phase >= 1.0) phase -= std::floor(phase);
    }
}
}
