#include "dsp/fx/SpaceReverb.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace hypercolor::dsp::fx
{
namespace
{
constexpr std::array<std::size_t, SpaceReverb::delayCount> delayMultipliers {
    1, 2, 3, 5, 7, 11, 13, 17
};
constexpr auto feedbackLimit = 0.9995f;
constexpr auto smoothingCoefficient = 0.001f;
constexpr auto inverseSqrtEight = 0.3535533905932738f;
}

SpaceReverbParameters SpaceReverb::sanitise(
    const SpaceReverbParameters& input
) noexcept
{
    SpaceReverbParameters result;
    result.mix = std::isfinite(input.mix) ? std::clamp(input.mix, 0.0f, 1.0f) : 0.3f;
    result.size = std::isfinite(input.size) ? std::clamp(input.size, 0.0f, 1.0f) : 0.5f;
    result.decaySeconds = std::isfinite(input.decaySeconds)
        ? std::clamp(input.decaySeconds, 0.1f, 20.0f) : 4.0f;
    result.damping = std::isfinite(input.damping) ? std::clamp(input.damping, 0.0f, 1.0f) : 0.4f;
    result.preDelayMilliseconds = std::isfinite(input.preDelayMilliseconds)
        ? std::clamp(input.preDelayMilliseconds, 0.0f, 200.0f) : 20.0f;
    result.width = std::isfinite(input.width) ? std::clamp(input.width, 0.0f, 2.0f) : 1.0f;
    return result;
}

bool SpaceReverb::prepare(const core::ProcessSpec& spec) noexcept
{
    prepared = false;
    if (!std::isfinite(spec.sampleRate)
        || spec.sampleRate <= 0.0
        || spec.maximumBlockSize == 0
        || (spec.numChannels != 1 && spec.numChannels != 2))
        return false;

    sampleRate = spec.sampleRate;
    numChannels = spec.numChannels;
    maximumDelaySamples = static_cast<std::size_t>(
        std::ceil(sampleRate * maximumDelaySeconds)
    ) + 1;
    maximumPreDelaySamples = static_cast<std::size_t>(
        std::ceil(sampleRate * maximumPreDelaySeconds)
    ) + 1;

    try
    {
        for (std::size_t line = 0; line < delayCount; ++line)
            delays[line].assign(maximumDelaySamples + 1, 0.0f);
        for (auto& channel : preDelay)
            channel.assign(maximumPreDelaySamples + 1, 0.0f);
    }
    catch (...)
    {
        for (auto& line : delays)
            line.clear();
        for (auto& channel : preDelay)
            channel.clear();
        return false;
    }

    reset();
    prepared = true;
    return true;
}

void SpaceReverb::reset() noexcept
{
    for (auto& line : delays)
        std::fill(line.begin(), line.end(), 0.0f);
    for (auto& channel : preDelay)
        std::fill(channel.begin(), channel.end(), 0.0f);
    dampingState.fill(0.0f);
    delayLengths.fill(1);
    delayWriteIndex = 0;
    preDelayWriteIndex = 0;
    parameters = sanitise(targetParameters);
}

void SpaceReverb::setParameters(const SpaceReverbParameters& input) noexcept
{
    targetParameters = sanitise(input);
}

const SpaceReverbParameters& SpaceReverb::getParameters() const noexcept
{
    return targetParameters;
}

float SpaceReverb::readDelay(std::size_t line, std::size_t delaySamples) const noexcept
{
    const auto length = delays[line].size();
    const auto offset = delaySamples % length;
    const auto index = (delayWriteIndex + length - offset) % length;
    return delays[line][index];
}

void SpaceReverb::process(EffectAudioBlock& block) noexcept
{
    if (!prepared || block.numChannels != numChannels)
        return;

    for (std::uint32_t channel = 0; channel < numChannels; ++channel)
    {
        if (block.channels[channel].empty())
            continue;
    }

    for (std::size_t sample = 0; sample < block.channels[0].size(); ++sample)
    {
        parameters.mix += smoothingCoefficient * (targetParameters.mix - parameters.mix);
        parameters.size += smoothingCoefficient * (targetParameters.size - parameters.size);
        parameters.decaySeconds += smoothingCoefficient * (targetParameters.decaySeconds - parameters.decaySeconds);
        parameters.damping += smoothingCoefficient * (targetParameters.damping - parameters.damping);
        parameters.preDelayMilliseconds += smoothingCoefficient * (targetParameters.preDelayMilliseconds - parameters.preDelayMilliseconds);
        parameters.width += smoothingCoefficient * (targetParameters.width - parameters.width);

        const auto delayScale = 0.025 + 0.095 * static_cast<double>(parameters.size);
        const auto baseDelaySamples = static_cast<std::size_t>(
            std::max(1.0, std::round(sampleRate * delayScale))
        );
        for (std::size_t line = 0; line < delayCount; ++line)
            delayLengths[line] = std::min(
                maximumDelaySamples,
                std::max<std::size_t>(1, baseDelaySamples * delayMultipliers[line] / 17)
            );

        const auto preDelaySamples = std::min(
            maximumPreDelaySamples,
            static_cast<std::size_t>(std::round(
                sampleRate * static_cast<double>(parameters.preDelayMilliseconds) / 1000.0
            ))
        );
        const auto preLength = preDelay[0].size();
        const auto preRead = (preDelayWriteIndex + preLength - (preDelaySamples % preLength)) % preLength;
        const auto inputLeft = std::isfinite(block.channels[0][sample]) ? block.channels[0][sample] : 0.0f;
        const auto inputRight = numChannels == 2 && std::isfinite(block.channels[1][sample])
            ? block.channels[1][sample] : inputLeft;
        preDelay[0][preDelayWriteIndex] = inputLeft;
        preDelay[1][preDelayWriteIndex] = inputRight;
        const auto delayedLeft = preDelay[0][preRead];
        const auto delayedRight = preDelay[1][preRead];
        const auto monoInput = 0.5f * (delayedLeft + delayedRight);

        std::array<float, delayCount> delayed {};
        for (std::size_t line = 0; line < delayCount; ++line)
        {
            const auto raw = readDelay(line, delayLengths[line]);
            dampingState[line] += (1.0f - parameters.damping) * (raw - dampingState[line]);
            delayed[line] = dampingState[line];
        }

        std::array<float, delayCount> transformed {};
        for (std::size_t line = 0; line < delayCount; ++line)
        {
            float sum = 0.0f;
            for (std::size_t source = 0; source < delayCount; ++source)
            {
                const auto sign = ((line & source) == 0) ? 1.0f : -1.0f;
                sum += sign * delayed[source];
            }
            transformed[line] = sum * inverseSqrtEight;
            const auto excitation = monoInput * (line < 4 ? 0.25f : -0.25f);
            const auto feedback = 0.15f + 0.84f * std::exp(
                -3.0f * static_cast<float>(delayLengths[line])
                    / static_cast<float>(sampleRate * std::max(0.1, static_cast<double>(parameters.decaySeconds)))
            );
            delays[line][delayWriteIndex] = excitation + transformed[line] * std::min(feedback, feedbackLimit);
        }

        const auto wetMid = 0.25f * (delayed[0] + delayed[1] + delayed[2] + delayed[3]);
        const auto wetSide = 0.25f * (delayed[4] - delayed[5] + delayed[6] - delayed[7]);
        const auto wetLeft = wetMid + wetSide * parameters.width;
        const auto wetRight = wetMid - wetSide * parameters.width;
        const auto dryLeft = inputLeft;
        const auto dryRight = inputRight;
        block.channels[0][sample] = dryLeft + parameters.mix * (wetLeft - dryLeft);
        if (numChannels == 2)
            block.channels[1][sample] = dryRight + parameters.mix * (wetRight - dryRight);

        delayWriteIndex = (delayWriteIndex + 1) % delays[0].size();
        preDelayWriteIndex = (preDelayWriteIndex + 1) % preLength;
    }
}
}
