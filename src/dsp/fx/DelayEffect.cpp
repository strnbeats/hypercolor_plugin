#include "dsp/fx/DelayEffect.h"

#include <algorithm>
#include <cmath>

namespace hypercolor::dsp::fx
{
DelayParameters DelayEffect::sanitise(const DelayParameters& input) noexcept
{
    DelayParameters result;
    result.mode = input.mode == DelayMode::pingPong ? DelayMode::pingPong : DelayMode::stereo;
    result.sync = input.sync;
    result.timeMilliseconds = std::isfinite(input.timeMilliseconds) ? std::clamp(input.timeMilliseconds, 1.0f, 2000.0f) : 350.0f;
    result.division = static_cast<DelayDivision>(std::min(static_cast<unsigned>(input.division), 8u));
    result.feedback = std::isfinite(input.feedback) ? std::clamp(input.feedback, 0.0f, 0.95f) : 0.35f;
    result.lowCutHz = std::isfinite(input.lowCutHz) ? std::clamp(input.lowCutHz, 20.0f, 2000.0f) : 100.0f;
    result.highCutHz = std::isfinite(input.highCutHz) ? std::clamp(input.highCutHz, 1000.0f, 20000.0f) : 12000.0f;
    if (result.highCutHz < result.lowCutHz + 20.0f)
        result.highCutHz = std::min(20000.0f, result.lowCutHz + 20.0f);
    result.mix = std::isfinite(input.mix) ? std::clamp(input.mix, 0.0f, 1.0f) : 0.3f;
    result.width = std::isfinite(input.width) ? std::clamp(input.width, 0.0f, 2.0f) : 1.0f;
    result.bpm = std::isfinite(input.bpm) ? std::clamp(input.bpm, 20.0f, 300.0f) : 120.0f;
    return result;
}

bool DelayEffect::prepare(const core::ProcessSpec& spec) noexcept
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

void DelayEffect::reset() noexcept
{
    for (auto& channel : buffer)
        std::fill(channel.begin(), channel.end(), 0.0f);
    lowState.fill(0.0f);
    highState.fill(0.0f);
    previousInput.fill(0.0f);
    writeIndex = 0;
    current = sanitise(target);
}

void DelayEffect::setParameters(const DelayParameters& parameters) noexcept
{
    target = sanitise(parameters);
}

double DelayEffect::divisionMultiplier(DelayDivision division) noexcept
{
    constexpr std::array multipliers { 4.0, 2.0, 1.0, 0.5, 0.25, 1.5, 0.75, 2.0 / 3.0, 1.0 / 3.0 };
    return multipliers[static_cast<std::size_t>(division)];
}

float DelayEffect::readFractional(std::size_t channel, double delay) const noexcept
{
    const auto read = static_cast<double>(writeIndex) - delay;
    auto wrapped = std::fmod(read, static_cast<double>(capacity));
    if (wrapped < 0.0) wrapped += static_cast<double>(capacity);
    const auto index = static_cast<std::size_t>(wrapped);
    const auto fraction = wrapped - static_cast<double>(index);
    const auto next = (index + 1) % capacity;
    return static_cast<float>((1.0 - fraction) * buffer[channel][index] + fraction * buffer[channel][next]);
}

void DelayEffect::process(EffectAudioBlock& block) noexcept
{
    if (!prepared || block.numChannels != numChannels || block.channels[0].empty()
        || (numChannels == 2 && block.channels[1].size() != block.channels[0].size()))
        return;

    for (std::size_t sample = 0; sample < block.channels[0].size(); ++sample)
    {
        current.timeMilliseconds += smoothing * (target.timeMilliseconds - current.timeMilliseconds);
        current.feedback += smoothing * (target.feedback - current.feedback);
        current.lowCutHz += smoothing * (target.lowCutHz - current.lowCutHz);
        current.highCutHz += smoothing * (target.highCutHz - current.highCutHz);
        current.mix += smoothing * (target.mix - current.mix);
        current.width += smoothing * (target.width - current.width);
        current.bpm += smoothing * (target.bpm - current.bpm);
        current.sync = target.sync;
        current.division = target.division;
        current.mode = target.mode;

        const auto delayMs = current.sync
            ? static_cast<float>(60000.0 * divisionMultiplier(current.division) / current.bpm)
            : current.timeMilliseconds;
        const auto delaySamples = std::clamp(static_cast<double>(delayMs) * sampleRate / 1000.0, 1.0, static_cast<double>(capacity - 2));
        const auto leftInput = std::isfinite(block.channels[0][sample]) ? block.channels[0][sample] : 0.0f;
        const auto rightInput = numChannels == 2 && std::isfinite(block.channels[1][sample]) ? block.channels[1][sample] : leftInput;
        const auto delayedLeft = readFractional(0, delaySamples);
        const auto delayedRight = readFractional(1, delaySamples);

        const auto lowAlpha = 1.0f - std::exp(-2.0f * 3.14159265358979323846f * current.lowCutHz / static_cast<float>(sampleRate));
        const auto highAlpha = 1.0f - std::exp(-2.0f * 3.14159265358979323846f * current.highCutHz / static_cast<float>(sampleRate));
        const auto filterFeedback = [&](std::size_t channel, float value) noexcept
        {
            lowState[channel] += lowAlpha * (value - lowState[channel]);
            highState[channel] += highAlpha * (value - highState[channel]);
            return highState[channel] - lowState[channel];
        };
        const auto filteredLeft = filterFeedback(0, delayedLeft);
        const auto filteredRight = filterFeedback(1, delayedRight);
        const auto feedbackLeft = current.mode == DelayMode::pingPong ? filteredRight : filteredLeft;
        const auto feedbackRight = current.mode == DelayMode::pingPong ? filteredLeft : filteredRight;
        buffer[0][writeIndex] = leftInput + feedbackLeft * current.feedback;
        buffer[1][writeIndex] = rightInput + feedbackRight * current.feedback;
        const auto wetMid = 0.5f * (delayedLeft + delayedRight);
        const auto wetSide = 0.5f * (delayedLeft - delayedRight) * current.width;
        const auto wetLeft = wetMid + wetSide;
        const auto wetRight = wetMid - wetSide;
        const auto outputLeft = leftInput + current.mix * (wetLeft - leftInput);
        const auto outputRight = rightInput + current.mix * (wetRight - rightInput);
        block.channels[0][sample] = std::isfinite(outputLeft) ? outputLeft : 0.0f;
        if (numChannels == 2)
            block.channels[1][sample] = std::isfinite(outputRight) ? outputRight : 0.0f;
        writeIndex = (writeIndex + 1) % capacity;
    }
}
}
