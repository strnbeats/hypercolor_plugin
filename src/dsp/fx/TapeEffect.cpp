#include "dsp/fx/TapeEffect.h"

#include <algorithm>
#include <cmath>

namespace hypercolor::dsp::fx
{
TapeParameters TapeEffect::sanitise(const TapeParameters& input) noexcept
{
    TapeParameters result;
    result.drive = std::isfinite(input.drive) ? std::clamp(input.drive, 0.0f, 1.0f) : 0.35f;
    result.wow = std::isfinite(input.wow) ? std::clamp(input.wow, 0.0f, 1.0f) : 0.15f;
    result.flutter = std::isfinite(input.flutter) ? std::clamp(input.flutter, 0.0f, 1.0f) : 0.10f;
    result.wear = std::isfinite(input.wear) ? std::clamp(input.wear, 0.0f, 1.0f) : 0.15f;
    result.mix = std::isfinite(input.mix) ? std::clamp(input.mix, 0.0f, 1.0f) : 1.0f;
    result.outputDb = std::isfinite(input.outputDb) ? std::clamp(input.outputDb, -24.0f, 12.0f) : 0.0f;
    return result;
}

bool TapeEffect::prepare(const core::ProcessSpec& spec) noexcept
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

void TapeEffect::reset() noexcept
{
    for (auto& channel : buffer)
        std::fill(channel.begin(), channel.end(), 0.0f);
    wearState.fill(0.0f);
    writeIndex = 0;
    wowPhase = 0.0;
    flutterPhase = 0.0;
    current = sanitise(target);
}

void TapeEffect::setParameters(const TapeParameters& parameters) noexcept
{
    target = sanitise(parameters);
}

float TapeEffect::readFractional(std::size_t channel, double delay) const noexcept
{
    const auto read = static_cast<double>(writeIndex) - delay;
    auto wrapped = std::fmod(read, static_cast<double>(capacity));
    if (wrapped < 0.0) wrapped += static_cast<double>(capacity);
    const auto index = static_cast<std::size_t>(wrapped);
    const auto fraction = wrapped - static_cast<double>(index);
    const auto next = (index + 1) % capacity;
    return static_cast<float>((1.0 - fraction) * buffer[channel][index] + fraction * buffer[channel][next]);
}

void TapeEffect::process(EffectAudioBlock& block) noexcept
{
    if (!prepared || block.numChannels != numChannels || block.channels[0].empty()
        || (numChannels == 2 && block.channels[1].size() != block.channels[0].size()))
        return;

    for (std::size_t sample = 0; sample < block.channels[0].size(); ++sample)
    {
        current.drive += smoothing * (target.drive - current.drive);
        current.wow += smoothing * (target.wow - current.wow);
        current.flutter += smoothing * (target.flutter - current.flutter);
        current.wear += smoothing * (target.wear - current.wear);
        current.mix += smoothing * (target.mix - current.mix);
        current.outputDb += smoothing * (target.outputDb - current.outputDb);

        const auto modulationLeft = 1.0 + 5.0 * current.wow * std::sin(2.0 * 3.141592653589793 * wowPhase)
            + 1.2 * current.flutter * std::sin(2.0 * 3.141592653589793 * flutterPhase);
        const auto modulationRight = 1.0 + 5.0 * current.wow * std::sin(2.0 * 3.141592653589793 * (wowPhase + 0.07))
            + 1.2 * current.flutter * std::sin(2.0 * 3.141592653589793 * (flutterPhase + 0.13));
        const auto baseDelay = 8.0;
        const auto leftDelay = std::clamp(baseDelay * modulationLeft, 1.0, static_cast<double>(capacity - 2));
        const auto rightDelay = std::clamp(baseDelay * modulationRight, 1.0, static_cast<double>(capacity - 2));
        const auto inputLeft = std::isfinite(block.channels[0][sample]) ? block.channels[0][sample] : 0.0f;
        const auto inputRight = numChannels == 2 && std::isfinite(block.channels[1][sample]) ? block.channels[1][sample] : inputLeft;
        const auto delayedLeft = readFractional(0, leftDelay);
        const auto delayedRight = readFractional(1, rightDelay);
        const auto cutoff = 18000.0f - 16000.0f * current.wear;
        const auto alpha = 1.0f - std::exp(-2.0f * 3.14159265358979323846f * cutoff / static_cast<float>(sampleRate));
        const auto tape = [&](std::size_t channel, float value) noexcept
        {
            wearState[channel] += alpha * (value - wearState[channel]);
            const auto shaped = wearState[channel] * (1.0f - current.wear) + value * current.wear;
            const auto gain = 1.0f + 5.0f * current.drive;
            const auto asymmetric = shaped * gain + 0.035f * current.drive * shaped * shaped;
            return asymmetric / (1.0f + 0.55f * std::abs(asymmetric));
        };
        const auto wetLeft = tape(0, delayedLeft);
        const auto wetRight = tape(1, delayedRight);
        const auto outputGain = std::pow(10.0f, current.outputDb / 20.0f);
        const auto effectiveMix = target.mix <= 0.0f ? 0.0f : current.mix;
        const auto outputLeft = inputLeft + effectiveMix * (wetLeft * outputGain - inputLeft);
        const auto outputRight = inputRight + effectiveMix * (wetRight * outputGain - inputRight);
        block.channels[0][sample] = std::isfinite(outputLeft) ? outputLeft : 0.0f;
        if (numChannels == 2)
            block.channels[1][sample] = std::isfinite(outputRight) ? outputRight : 0.0f;
        buffer[0][writeIndex] = inputLeft;
        buffer[1][writeIndex] = inputRight;
        writeIndex = (writeIndex + 1) % capacity;
        wowPhase += 0.27 / sampleRate;
        flutterPhase += 6.2 / sampleRate;
        if (wowPhase >= 1.0) wowPhase -= std::floor(wowPhase);
        if (flutterPhase >= 1.0) flutterPhase -= std::floor(flutterPhase);
    }
}
}
