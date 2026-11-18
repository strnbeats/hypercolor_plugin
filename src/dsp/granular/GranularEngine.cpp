#include "dsp/granular/GranularEngine.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace hypercolor::dsp::granular
{
bool GranularEngine::prepare(const core::ProcessSpec& spec)
{
    prepared = false;
    grainPool.reset();
    scheduler.reset();
    resetWaveformAccumulator();
    activeGrainTelemetryCount = 0;
    telemetryParameters = {};
    telemetrySequence = 0;

    const auto bufferPrepared = circularBuffer.prepare(
        spec,
        CircularAudioBuffer::productMaximumDurationSeconds,
        CircularAudioBuffer::defaultLogicalDurationSeconds
    );
    const auto schedulerPrepared = scheduler.prepare(spec);

    prepared = bufferPrepared && schedulerPrepared;

    if (prepared)
    {
        configureWaveformAccumulator();
    }
    else
    {
        circularBuffer.reset();
        scheduler.reset();
        grainPool.reset();
        resetWaveformAccumulator();
    }

    return prepared;
}

void GranularEngine::reset() noexcept
{
    circularBuffer.reset();
    scheduler.reset();
    grainPool.reset();
    resetWaveformAccumulator();
    activeGrainTelemetryCount = 0;
    telemetryParameters = {};
    telemetrySequence = 0;
}

bool GranularEngine::setLogicalDurationSeconds(
    double durationSeconds
) noexcept
{
    if (!prepared
        || !circularBuffer.setLogicalDurationSeconds(durationSeconds))
    {
        return false;
    }

    configureWaveformAccumulator();
    return true;
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

        const auto frameWritten = circularBuffer.writeFrame(
                std::span<const float> { inputFrame.data(), numChannels }
            );

        if (!frameWritten)
        {
            degraded = true;
        }
        else
        {
            accumulateWaveformFrame(
                std::span<const float> { inputFrame.data(), numChannels }
            );
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
            case GrainSchedulerStepResult::pendingStale:
                break;
            case GrainSchedulerStepResult::poolFull:
            case GrainSchedulerStepResult::invalidParameters:
            case GrainSchedulerStepResult::invalidTrajectory:
            case GrainSchedulerStepResult::traversalInProgress:
            case GrainSchedulerStepResult::schedulerNotPrepared:
            case GrainSchedulerStepResult::sourceUnavailable:
            case GrainSchedulerStepResult::pendingOverflow:
            case GrainSchedulerStepResult::pendingExpired:
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

        // The snapshot is externally observable only after processBlock()
        // returns, so capture the final sample of the block rather than adding
        // a second 64-slot traversal to every sample. The ordering remains
        // strictly post-render and pre-reclaim for the captured sample.
        if (sampleIndex + 1 == numSamples)
            captureActiveGrainTelemetry(parameters);

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

bool GranularEngine::copyWaveformTelemetry(
    GranularTelemetrySnapshot& destination
) const noexcept
{
    return copyTelemetrySnapshot(destination);
}

bool GranularEngine::copyTelemetrySnapshot(
    GranularTelemetrySnapshot& destination
) const noexcept
{
    if (!prepared)
        return false;

    destination = {};
    destination.sequence = telemetrySequence;
    const auto logicalRange = circularBuffer.getLogicalFrameRange();
    destination.generation = waveformGeneration;
    destination.logicalOldest = logicalRange.oldest;
    destination.logicalNewestExclusive = logicalRange.newestExclusive;
    destination.sampleRate = circularBuffer.getSampleRate();
    destination.logicalDurationSeconds
        = circularBuffer.getLogicalDurationSeconds();
    destination.validDurationSeconds = destination.sampleRate > 0.0
        ? static_cast<double>(logicalRange.size()) / destination.sampleRate
        : 0.0;
    destination.numChannels = circularBuffer.getNumChannels();
    destination.appliedParameters = telemetryParameters;
    destination.waveformPointCount
        = static_cast<std::uint32_t>(waveformPointCount);
    destination.activeGrainCount
        = static_cast<std::uint32_t>(activeGrainTelemetryCount);
    destination.waveformNewestExclusive = waveformNewestExclusive;

    const auto representedFrames = static_cast<CircularAudioBuffer::AbsoluteFrame>(
        waveformPointCount * waveformFramesPerPoint
    );
    destination.waveformOldest = waveformNewestExclusive >= representedFrames
        ? waveformNewestExclusive - representedFrames
        : 0;

    const auto oldestIndex = (
        waveformWriteIndex + waveformPointCapacity - waveformPointCount
    ) % waveformPointCapacity;

    for (std::size_t point = 0; point < waveformPointCount; ++point)
    {
        const auto sourceIndex = (oldestIndex + point)
            % waveformPointCapacity;
        destination.waveformPoints[point].minimum
            = waveformMinimum[sourceIndex];
        destination.waveformPoints[point].maximum
            = waveformMaximum[sourceIndex];
    }

    for (std::size_t point = waveformPointCount;
         point < waveformPointCapacity;
         ++point)
    {
        destination.waveformPoints[point] = {};
    }

    for (std::size_t grain = 0;
         grain < activeGrainTelemetryCount;
         ++grain)
    {
        destination.grains[grain] = activeGrainTelemetry[grain];
    }

    return true;
}

void GranularEngine::configureWaveformAccumulator() noexcept
{
    static_assert(
        waveformPointCapacity == granularTelemetryWaveformCapacity
    );

    const auto logicalLength = circularBuffer.getLogicalLengthFrames();
    waveformFramesPerPoint = std::max<std::size_t>(
        1,
        logicalLength / waveformPointCapacity
    );
    waveformMaximumPointCount = std::min<std::size_t>(
        waveformPointCapacity,
        logicalLength / waveformFramesPerPoint
    );
    resetWaveformAccumulator();
}

void GranularEngine::resetWaveformAccumulator() noexcept
{
    waveformWriteIndex = 0;
    waveformPointCount = 0;
    waveformFramesInCurrentPoint = 0;
    waveformNewestExclusive = circularBuffer.getAbsoluteWritePosition();
    waveformGeneration = circularBuffer.getGeneration();
    currentWaveformMinimum = {};
    currentWaveformMaximum = {};
}

void GranularEngine::accumulateWaveformFrame(
    std::span<const float> inputFrame
) noexcept
{
    if (circularBuffer.getGeneration() != waveformGeneration)
        resetWaveformAccumulator();

    if (waveformFramesInCurrentPoint == 0)
    {
        currentWaveformMinimum = {};
        currentWaveformMaximum = {};

        for (std::size_t channel = 0; channel < inputFrame.size(); ++channel)
        {
            currentWaveformMinimum[channel] = inputFrame[channel];
            currentWaveformMaximum[channel] = inputFrame[channel];
        }
    }
    else
    {
        for (std::size_t channel = 0; channel < inputFrame.size(); ++channel)
        {
            currentWaveformMinimum[channel] = std::min(
                currentWaveformMinimum[channel],
                inputFrame[channel]
            );
            currentWaveformMaximum[channel] = std::max(
                currentWaveformMaximum[channel],
                inputFrame[channel]
            );
        }
    }

    ++waveformFramesInCurrentPoint;

    if (waveformFramesInCurrentPoint == waveformFramesPerPoint)
    {
        waveformMinimum[waveformWriteIndex] = currentWaveformMinimum;
        waveformMaximum[waveformWriteIndex] = currentWaveformMaximum;
        waveformWriteIndex = (waveformWriteIndex + 1)
            % waveformPointCapacity;
        waveformPointCount = std::min(
            waveformPointCount + 1,
            waveformMaximumPointCount
        );
        waveformFramesInCurrentPoint = 0;
        waveformNewestExclusive = circularBuffer.getAbsoluteWritePosition();
    }

    discardExpiredWaveformPoints();
}

void GranularEngine::discardExpiredWaveformPoints() noexcept
{
    const auto currentWritePosition
        = circularBuffer.getAbsoluteWritePosition();
    const auto logicalLength = static_cast<CircularAudioBuffer::AbsoluteFrame>(
        circularBuffer.getLogicalLengthFrames()
    );

    if (waveformPointCount == 0)
        return;

    const auto representedFrames
        = static_cast<CircularAudioBuffer::AbsoluteFrame>(
            waveformPointCount * waveformFramesPerPoint
        );
    const auto oldest = waveformNewestExclusive - representedFrames;

    // One input frame advances the logical frontier by at most one frame, so
    // at most one fixed-width completed point can expire per call. Logical
    // duration changes and generation changes reset the accumulator first.
    if (currentWritePosition - oldest > logicalLength)
        --waveformPointCount;
}

void GranularEngine::captureActiveGrainTelemetry(
    const GranularEngineParameters& parameters
) noexcept
{
    static_assert(
        granularTelemetryGrainCapacity == GrainPool::maximumGrains
    );

    activeGrainTelemetryCount = 0;
    telemetryParameters = parameters;

    grainPool.forEachActive(
        [&](GrainRenderAccess& access) noexcept
        {
            if (activeGrainTelemetryCount
                >= activeGrainTelemetry.size())
            {
                return;
            }

            const auto& sourceRegion = access.getSourceRegion();
            auto& telemetry
                = activeGrainTelemetry[activeGrainTelemetryCount++];
            telemetry.sourceKind = access.getSourceKind();
            telemetry.readPosition = access.getReadPosition();
            telemetry.playbackRate = access.getPlaybackRate();
            if (telemetry.sourceKind == CausalSourceKind::history)
            {
                telemetry.sourceOldest = sourceRegion.oldest;
                telemetry.sourceNewestExclusive
                    = sourceRegion.newestExclusive;
            }
            else
            {
                const auto liveRange = circularBuffer.getLogicalFrameRange();
                telemetry.sourceOldest = liveRange.oldest;
                telemetry.sourceNewestExclusive = liveRange.newestExclusive;
            }
            telemetry.durationFrames = access.getDurationFrames();
            telemetry.renderedFrames = access.getRenderedFrames();
        }
    );

    ++telemetrySequence;
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
