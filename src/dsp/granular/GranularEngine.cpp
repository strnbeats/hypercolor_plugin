#include "dsp/granular/GranularEngine.h"

#include <array>
#include <cmath>

namespace hypercolor::dsp::granular
{
bool GranularEngine::prepare(const core::ProcessSpec& spec)
{
    prepared = false;
    grainPool.reset();
    scheduler.reset();

    const auto bufferPrepared = circularBuffer.prepare(
        spec,
        CircularAudioBuffer::productMaximumDurationSeconds,
        CircularAudioBuffer::defaultLogicalDurationSeconds
    );
    const auto schedulerPrepared = scheduler.prepare(spec);

    prepared = bufferPrepared && schedulerPrepared;

    if (!prepared)
    {
        circularBuffer.reset();
        scheduler.reset();
        grainPool.reset();
    }

    return prepared;
}

void GranularEngine::reset() noexcept
{
    circularBuffer.reset();
    scheduler.reset();
    grainPool.reset();
}

GranularEngineProcessResult GranularEngine::processBlock(
    const GranularAudioBlockView& block,
    const GranularEngineParameters& parameters
) noexcept
{
    if (!prepared)
        return GranularEngineProcessResult::engineNotPrepared;

    if (!isBlockConfigurationValid(block))
        return GranularEngineProcessResult::invalidConfiguration;

    const auto numChannels = block.numChannels;
    const auto numSamples = block.inputChannels[0].size();
    auto degraded = false;

    for (std::size_t sampleIndex = 0;
         sampleIndex < numSamples;
         ++sampleIndex)
    {
        std::array<float, 2> inputFrame {};

        for (std::uint32_t channel = 0;
             channel < numChannels;
             ++channel)
        {
            const auto input = block.inputChannels[channel][sampleIndex];

            if (std::isfinite(input))
                inputFrame[channel] = input;
            else
                degraded = true;
        }

        if (!circularBuffer.writeFrame(
                std::span<const float> { inputFrame.data(), numChannels }
            ))
        {
            degraded = true;
        }

        const auto schedulerResult = scheduler.processSample(
            parameters,
            circularBuffer,
            grainPool
        );

        switch (schedulerResult)
        {
            case GrainSchedulerStepResult::noEvent:
            case GrainSchedulerStepResult::started:
            case GrainSchedulerStepResult::insufficientHistory:
                break;
            case GrainSchedulerStepResult::poolFull:
            case GrainSchedulerStepResult::invalidParameters:
            case GrainSchedulerStepResult::invalidTrajectory:
            case GrainSchedulerStepResult::traversalInProgress:
            case GrainSchedulerStepResult::schedulerNotPrepared:
            case GrainSchedulerStepResult::sourceUnavailable:
                degraded = true;
                break;
        }

        std::array<float, 2> wetFrame {};
        const auto rendererResult = renderer.renderSample(
            circularBuffer,
            grainPool,
            std::span<float> { wetFrame.data(), numChannels }
        );

        if (rendererResult != GrainRendererResult::success)
            degraded = true;

        grainPool.reclaimFinished();

        auto wetFrameIsFinite = true;

        for (std::uint32_t channel = 0;
             channel < numChannels;
             ++channel)
        {
            wetFrameIsFinite = wetFrameIsFinite
                && std::isfinite(wetFrame[channel]);
        }

        if (!wetFrameIsFinite)
        {
            wetFrame = {};
            degraded = true;
        }

        for (std::uint32_t channel = 0;
             channel < numChannels;
             ++channel)
        {
            block.outputChannels[channel][sampleIndex] = wetFrame[channel];
        }
    }

    return degraded
        ? GranularEngineProcessResult::degraded
        : GranularEngineProcessResult::success;
}

bool GranularEngine::isPrepared() const noexcept
{
    return prepared;
}

bool GranularEngine::isBlockConfigurationValid(
    const GranularAudioBlockView& block
) const noexcept
{
    if ((block.numChannels != 1 && block.numChannels != 2)
        || block.numChannels != circularBuffer.getNumChannels())
    {
        return false;
    }

    const auto numSamples = block.inputChannels[0].size();

    for (std::uint32_t channel = 0;
         channel < block.numChannels;
         ++channel)
    {
        if (block.inputChannels[channel].size() != numSamples
            || block.outputChannels[channel].size() != numSamples)
        {
            return false;
        }

        if (numSamples > 0
            && (block.inputChannels[channel].data() == nullptr
                || block.outputChannels[channel].data() == nullptr))
        {
            return false;
        }
    }

    return true;
}
}
