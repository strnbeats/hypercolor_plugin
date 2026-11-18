#include "dsp/granular/GrainRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

namespace hypercolor::dsp::granular
{
GrainRendererResult GrainRenderer::renderSample(
    const CircularAudioBuffer& source,
    GrainPool& pool,
    std::span<float> wetOutputFrame
) const noexcept
{
    const auto numChannels = source.getNumChannels();

    if (!source.isPrepared()
        || (numChannels != 1 && numChannels != 2)
        || wetOutputFrame.size() != numChannels)
    {
        return GrainRendererResult::invalidConfiguration;
    }

    std::array<double, 2> accumulators {};
    auto degraded = false;

    pool.forEachActive(
        [&](GrainRenderAccess& access) noexcept
        {
            const auto readPosition = access.getReadPosition();
            const auto window = access.getWindowCoefficient();
            std::array<float, 2> sourceFrame {};
            auto validFrame = std::isfinite(readPosition)
                && std::isfinite(window);

            for (std::uint32_t channel = 0;
                 channel < numChannels;
                 ++channel)
            {
                float sample = 0.0f;
                const auto readSucceeded
                    = access.getSourceKind() == CausalSourceKind::history
                    ? source.tryReadLinear(
                        access.getSourceRegion(),
                        channel,
                        readPosition,
                        sample
                    )
                    : source.tryReadCausalLive(
                        access.getSourceGeneration(),
                        channel,
                        readPosition,
                        access.getCausalMarginSamples(),
                        sample
                    );

                sourceFrame[channel] = sample;
                validFrame = validFrame
                    && readSucceeded
                    && std::isfinite(sample);
            }

            if (!validFrame)
            {
                access.markFinished();
                degraded = true;
                return;
            }

            for (std::uint32_t channel = 0;
                 channel < numChannels;
                 ++channel)
            {
                auto channelGain = 1.0;
                if (numChannels == 2)
                {
                    const auto pan = std::clamp(
                        access.getPan(), -1.0, 1.0
                    );
                    const auto angle = (pan + 1.0)
                        * std::numbers::pi / 4.0;
                    const auto centreGain = std::sqrt(0.5);
                    channelGain = channel == 0
                        ? std::cos(angle) / centreGain
                        : std::sin(angle) / centreGain;
                }
                accumulators[channel]
                    += static_cast<double>(sourceFrame[channel])
                        * static_cast<double>(window)
                        * channelGain;
            }

            access.advanceAfterRender();
        }
    );

    std::array<float, 2> convertedOutput {};
    constexpr auto maximumFloat = static_cast<double>(
        std::numeric_limits<float>::max()
    );
    auto outputRepresentable = true;

    for (std::uint32_t channel = 0; channel < numChannels; ++channel)
    {
        const auto value = accumulators[channel];

        if (!std::isfinite(value)
            || value > maximumFloat
            || value < -maximumFloat)
        {
            outputRepresentable = false;
            break;
        }

        convertedOutput[channel] = static_cast<float>(value);
    }

    if (!outputRepresentable)
    {
        for (std::uint32_t channel = 0; channel < numChannels; ++channel)
            wetOutputFrame[channel] = 0.0f;

        return GrainRendererResult::degraded;
    }

    for (std::uint32_t channel = 0; channel < numChannels; ++channel)
        wetOutputFrame[channel] = convertedOutput[channel];

    return degraded
        ? GrainRendererResult::degraded
        : GrainRendererResult::success;
}
}
