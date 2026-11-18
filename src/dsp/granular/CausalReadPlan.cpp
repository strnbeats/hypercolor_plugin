#include "dsp/granular/CausalReadPlan.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace hypercolor::dsp::granular
{
namespace
{
constexpr double maximumExactDoubleInteger
    = static_cast<double>(std::uint64_t { 1 }
        << std::numeric_limits<double>::digits);

bool finiteAndRepresentable(double value) noexcept
{
    return std::isfinite(value)
        && value >= 0.0
        && value < maximumExactDoubleInteger;
}

bool checkedCeilToFrames(double value, std::uint64_t& result) noexcept
{
    if (!std::isfinite(value) || value < 0.0
        || value >= maximumExactDoubleInteger)
    {
        return false;
    }

    const auto rounded = std::ceil(value);
    if (!std::isfinite(rounded)
        || rounded > static_cast<double>(
            std::numeric_limits<std::uint64_t>::max()))
    {
        return false;
    }

    result = static_cast<std::uint64_t>(rounded);
    return true;
}

double positionAt(
    double start,
    double rate,
    std::uint32_t offset
) noexcept
{
    return CircularAudioBuffer::calculateLinearTrajectoryPosition(
        start,
        rate,
        offset
    );
}

bool lowerBoundAtOffset(
    std::uint64_t writeExclusive,
    std::uint64_t capacity,
    std::uint64_t logicalLength,
    std::uint64_t offset,
    std::uint64_t& physicalOldest,
    std::uint64_t& logicalOldest
) noexcept
{
    if (offset > std::numeric_limits<std::uint64_t>::max()
            - writeExclusive)
    {
        return false;
    }

    const auto futureWrite = writeExclusive + offset;
    physicalOldest = futureWrite > capacity
        ? futureWrite - capacity
        : 0;
    logicalOldest = futureWrite > logicalLength
        ? futureWrite - logicalLength
        : 0;
    return true;
}

bool checkedIncrement(std::uint64_t value, std::uint64_t& result) noexcept
{
    if (value == std::numeric_limits<std::uint64_t>::max())
        return false;
    result = value + 1;
    return true;
}
}

bool CausalReadPlanBuilder::isKnownPolicy(CausalSourcePolicy policy) noexcept
{
    switch (policy)
    {
        case CausalSourcePolicy::historyOnly:
        case CausalSourcePolicy::causalLiveOnly:
        case CausalSourcePolicy::historyThenLiveFallback:
            return true;
    }
    return false;
}

bool CausalReadPlanBuilder::isValidMargin(std::uint32_t margin) noexcept
{
    // Linear interpolation reads floor(position) and floor(position)+1.
    // With a write-exclusive head, M=2 is the smallest unambiguous margin.
    return margin >= 2;
}

bool CausalReadPlanBuilder::validEvent(
    const CausalEventSnapshot& event
) noexcept
{
    return event.generation != 0
        && std::isfinite(event.positionNormalized)
        && event.positionNormalized >= 0.0
        && event.positionNormalized <= 1.0
        && std::isfinite(event.playbackRate)
        && event.playbackRate != 0.0
        && event.grainLengthFrames > 0
        && isKnownPolicy(event.sourcePolicy);
}

bool CausalReadPlanBuilder::captureEventSnapshot(
    CausalSourcePolicy sourcePolicy,
    CircularAudioBuffer::Generation generation,
    CircularAudioBuffer::AbsoluteFrame eventSample,
    double positionNormalized,
    double playbackRate,
    std::uint32_t grainLengthFrames,
    CausalEventSnapshot& result
) noexcept
{
    result = {};
    result.sourcePolicy = sourcePolicy;
    result.generation = generation;
    result.eventSample = eventSample;
    result.positionNormalized = positionNormalized;
    result.playbackRate = playbackRate;
    result.grainLengthFrames = grainLengthFrames;
    return validEvent(result);
}

bool CausalReadPlanBuilder::captureEventSnapshot(
    const CircularAudioBuffer& source,
    CausalSourcePolicy sourcePolicy,
    CircularAudioBuffer::AbsoluteFrame eventSample,
    double positionNormalized,
    double playbackRate,
    std::uint32_t grainLengthFrames,
    CausalEventSnapshot& result,
    std::uint32_t causalMarginSamples
) noexcept
{
    result = {};
    if (!source.isPrepared() || !isValidMargin(causalMarginSamples))
        return false;

    std::uint64_t eventHead = 0;
    if (!checkedIncrement(eventSample, eventHead)
        || eventHead > source.getAbsoluteWritePosition())
    {
        return false;
    }

    return captureEventSnapshot(
        sourcePolicy,
        source.getGeneration(),
        eventSample,
        positionNormalized,
        playbackRate,
        grainLengthFrames,
        result
    );
}

CausalReadPlan CausalReadPlanBuilder::buildAtLaunch(
    const CircularAudioBuffer& source,
    const CausalEventSnapshot& event,
    const CircularAudioBuffer::SourceRegion& captureRegion
) noexcept
{
    CausalReadPlan plan;
    plan.sourcePolicy = event.sourcePolicy;
    plan.generation = event.generation;
    plan.eventSample = event.eventSample;
    plan.captureWriteExclusive = captureRegion.newestExclusive;
    plan.positionNormalized = event.positionNormalized;
    plan.playbackRate = event.playbackRate;
    plan.grainLengthFrames = event.grainLengthFrames;
    plan.historyRegion = captureRegion;

    if (!validEvent(event))
    {
        plan.feasibility = CausalReadFeasibility::invalidEventSnapshot;
        return plan;
    }
    if (!source.isPrepared())
    {
        plan.feasibility = CausalReadFeasibility::notPrepared;
        return plan;
    }
    if (source.getGeneration() != event.generation
        || captureRegion.generation != event.generation)
    {
        plan.feasibility = CausalReadFeasibility::generationMismatch;
        return plan;
    }
    std::uint64_t eventHead = 0;
    if (!checkedIncrement(event.eventSample, eventHead)
        || eventHead > captureRegion.newestExclusive)
    {
        plan.feasibility = CausalReadFeasibility::invalidEventTimestamp;
        return plan;
    }
    const auto currentLogicalRange = source.getLogicalFrameRange();
    if (captureRegion.isEmpty()
        || captureRegion.oldest != currentLogicalRange.oldest
        || captureRegion.newestExclusive != currentLogicalRange.newestExclusive
        || captureRegion.newestExclusive != source.getAbsoluteWritePosition())
    {
        plan.feasibility = CausalReadFeasibility::logicalRangeInvalid;
        return plan;
    }
    if (event.sourcePolicy != CausalSourcePolicy::historyOnly)
    {
        plan.feasibility = CausalReadFeasibility::fallbackNotAllowed;
        return plan;
    }

    const auto travel = std::abs(static_cast<long double>(event.playbackRate))
        * static_cast<long double>(event.grainLengthFrames - 1);
    const auto oldest = static_cast<long double>(captureRegion.oldest);
    const auto upperExclusive = static_cast<long double>(captureRegion.newestExclusive) - 1.0L;
    const auto oldestEndpoint = oldest + travel;
    const auto freshestEndpoint = calculateHistoryFreshEndpoint(captureRegion);
    if (!std::isfinite(travel) || !std::isfinite(oldestEndpoint)
        || static_cast<long double>(freshestEndpoint) < oldestEndpoint)
    {
        plan.feasibility = CausalReadFeasibility::insufficientHistory;
        return plan;
    }

    const auto selected = static_cast<long double>(freshestEndpoint)
        - static_cast<long double>(event.positionNormalized)
            * (static_cast<long double>(freshestEndpoint) - oldestEndpoint);
    auto endpoint = static_cast<double>(selected);
    if (!std::isfinite(endpoint))
    {
        plan.feasibility = CausalReadFeasibility::causalViolation;
        return plan;
    }
    if (static_cast<long double>(endpoint) < oldestEndpoint)
        endpoint = std::nextafter(endpoint, std::numeric_limits<double>::infinity());
    if (!std::isfinite(endpoint) || endpoint > freshestEndpoint
        || static_cast<long double>(endpoint) < oldestEndpoint
        || static_cast<long double>(endpoint) >= upperExclusive)
    {
        plan.feasibility = CausalReadFeasibility::causalViolation;
        return plan;
    }

    const auto startValue = event.playbackRate < 0.0
        ? static_cast<long double>(endpoint)
        : static_cast<long double>(endpoint) - travel;
    auto anchor = static_cast<double>(startValue);
    if (!std::isfinite(anchor))
    {
        plan.feasibility = CausalReadFeasibility::causalViolation;
        return plan;
    }
    if (static_cast<long double>(anchor) < oldest)
        anchor = std::nextafter(anchor, std::numeric_limits<double>::infinity());
    if (!std::isfinite(anchor) || anchor < 0.0
        || anchor >= maximumExactDoubleInteger
        || static_cast<long double>(anchor) < oldest)
    {
        plan.feasibility = CausalReadFeasibility::causalViolation;
        return plan;
    }

    if (!source.isLinearTrajectoryValid(
            captureRegion, anchor, event.playbackRate, event.grainLengthFrames))
    {
        plan.feasibility = CausalReadFeasibility::physicalRangeInvalid;
        return plan;
    }

    plan.sourceKind = CausalSourceKind::history;
    plan.anchor = anchor;
    plan.feasibility = CausalReadFeasibility::valid;
    plan.causalReadyWriteHead = captureRegion.newestExclusive;
    plan.earliestValidWriteHead = captureRegion.newestExclusive;
    return plan;
}

CausalReadPlan CausalReadPlanBuilder::buildAtLaunch(
    const CircularAudioBuffer& source,
    const CausalEventSnapshot& event
) noexcept
{
    return buildAtLaunch(source, event, source.captureSourceRegion());
}

std::uint64_t CausalReadPlanBuilder::requiredPrehistoryFrames(
    double playbackRate,
    std::uint32_t grainLengthFrames,
    std::uint32_t causalMarginSamples
) noexcept
{
    if (!std::isfinite(playbackRate)
        || playbackRate == 0.0
        || grainLengthFrames == 0
        || !isValidMargin(causalMarginSamples))
    {
        return 0;
    }

    const auto extra = std::max({
        0.0,
        playbackRate - 1.0,
        -playbackRate
    }) * static_cast<double>(grainLengthFrames - 1);
    std::uint64_t result = 0;
    if (!checkedCeilToFrames(
            static_cast<double>(causalMarginSamples) + extra,
            result
        ))
    {
        return 0;
    }

    return result;
}

double CausalReadPlanBuilder::calculateHistoryFreshEndpoint(
    const CircularAudioBuffer::SourceRegion& region
) noexcept
{
    if (region.isEmpty()
        || region.newestExclusive == 0
        || region.newestExclusive >= static_cast<CircularAudioBuffer::AbsoluteFrame>(maximumExactDoubleInteger))
    {
        return -1.0;
    }

    // Match GrainScheduler::calculateStartPosition(): both taps must be
    // within [oldest, newestExclusive), so x < U - 1. Use the greatest
    // representable double strictly below that boundary.
    const auto upperExclusive = static_cast<double>(region.newestExclusive) - 1.0;
    return std::nextafter(
        upperExclusive,
        -std::numeric_limits<double>::infinity()
    );
}

bool CausalReadPlanBuilder::isConservativelyCausal(
    double startPosition,
    double playbackRate,
    std::uint32_t grainLengthFrames,
    CircularAudioBuffer::AbsoluteFrame writeExclusive,
    std::uint32_t causalMarginSamples
) noexcept
{
    if (grainLengthFrames == 0 || !isValidMargin(causalMarginSamples)
        || !finiteAndRepresentable(startPosition)
        || !std::isfinite(playbackRate))
    {
        return false;
    }

    const auto last = grainLengthFrames - 1;
    const auto firstPosition = positionAt(startPosition, playbackRate, 0);
    const auto lastPosition = positionAt(startPosition, playbackRate, last);
    if (!finiteAndRepresentable(firstPosition)
        || !finiteAndRepresentable(lastPosition))
    {
        return false;
    }

    const auto margin = static_cast<double>(causalMarginSamples);
    if (last > std::numeric_limits<std::uint64_t>::max() - writeExclusive)
        return false;

    return firstPosition <= static_cast<double>(writeExclusive) - margin
        && lastPosition
            <= static_cast<double>(writeExclusive + last) - margin;
}

bool CausalReadPlanBuilder::calculateEndpointRange(
    CircularAudioBuffer::AbsoluteFrame logicalOldest,
    CircularAudioBuffer::AbsoluteFrame writeExclusive,
    double playbackRate,
    std::uint32_t grainLengthFrames,
    std::uint32_t causalMarginSamples,
    double& oldestEndpoint,
    double& freshestEndpoint
) noexcept
{
    if (grainLengthFrames == 0
        || !isValidMargin(causalMarginSamples)
        || !std::isfinite(playbackRate)
        || playbackRate == 0.0
        || writeExclusive < logicalOldest
        || writeExclusive >= static_cast<CircularAudioBuffer::AbsoluteFrame>(maximumExactDoubleInteger)
        || logicalOldest >= static_cast<CircularAudioBuffer::AbsoluteFrame>(maximumExactDoubleInteger))
    {
        return false;
    }

    const auto last = static_cast<double>(grainLengthFrames - 1);
    const auto travel = std::abs(playbackRate) * last;
    if (!std::isfinite(travel))
        return false;

    oldestEndpoint = static_cast<double>(logicalOldest) + travel;
    if (!std::isfinite(oldestEndpoint))
        return false;

    const auto write = static_cast<double>(writeExclusive);
    const auto margin = static_cast<double>(causalMarginSamples);
    if (playbackRate > 0.0)
    {
        // The first sample must already be available. For rates below one,
        // the trajectory remains behind the write head; rates above one
        // retreat the anchor and catch up with the live write head.
        freshestEndpoint = write - margin
            + std::min(playbackRate, 1.0) * last;
    }
    else
    {
        // Reverse never reads future input. Its freshest complete trajectory
        // starts at the latest safe sample and travels backwards.
        freshestEndpoint = write - margin;
    }

    return std::isfinite(freshestEndpoint)
        && freshestEndpoint >= oldestEndpoint;
}

bool CausalReadPlanBuilder::calculateStartFromEndpoint(
    double endpoint,
    double playbackRate,
    std::uint32_t grainLengthFrames,
    double& startPosition
) noexcept
{
    if (!std::isfinite(endpoint)
        || !std::isfinite(playbackRate)
        || playbackRate == 0.0
        || grainLengthFrames == 0)
    {
        return false;
    }

    const auto travel = std::abs(playbackRate)
        * static_cast<double>(grainLengthFrames - 1);
    startPosition = playbackRate > 0.0
        ? endpoint - travel
        : endpoint;
    return finiteAndRepresentable(startPosition);
}

bool CausalReadPlanBuilder::isCausalTrajectoryValid(
    const CircularAudioBuffer& source,
    double startPosition,
    double playbackRate,
    std::uint32_t grainLengthFrames,
    std::uint32_t causalMarginSamples
) noexcept
{
    if (!source.isPrepared()
        || !isFutureRangeValid(
            source.getAbsoluteWritePosition(),
            static_cast<std::uint64_t>(source.getCapacityFrames()),
            static_cast<std::uint64_t>(source.getLogicalLengthFrames()),
            startPosition,
            playbackRate,
            grainLengthFrames
        ))
    {
        return false;
    }

    if (!isConservativelyCausal(
            startPosition,
            playbackRate,
            grainLengthFrames,
            source.getAbsoluteWritePosition(),
            causalMarginSamples
        ))
    {
        return false;
    }

    const auto last = grainLengthFrames - 1;
    const auto first = positionAt(startPosition, playbackRate, 0);
    const auto end = positionAt(startPosition, playbackRate, last);
    const auto logical = source.getLogicalFrameRange();
    const auto oldest = static_cast<double>(logical.oldest);
    return std::floor(std::min(first, end)) >= oldest;
}

bool CausalReadPlanBuilder::isFutureRangeValid(
    CircularAudioBuffer::AbsoluteFrame writeExclusive,
    std::uint64_t capacityFrames,
    std::uint64_t logicalLengthFrames,
    double startPosition,
    double playbackRate,
    std::uint32_t grainLengthFrames
) noexcept
{
    if (capacityFrames == 0 || logicalLengthFrames == 0
        || logicalLengthFrames > capacityFrames
        || grainLengthFrames == 0
        || !finiteAndRepresentable(startPosition)
        || !std::isfinite(playbackRate))
    {
        return false;
    }

    const auto last = grainLengthFrames - 1;
    const auto checkOffset = [&](std::uint64_t offset) noexcept
    {
        const auto position = positionAt(
            startPosition,
            playbackRate,
            static_cast<std::uint32_t>(offset)
        );
        if (!finiteAndRepresentable(position))
            return false;

        std::uint64_t physicalOldest = 0;
        std::uint64_t logicalOldest = 0;
        if (!lowerBoundAtOffset(
                writeExclusive,
                capacityFrames,
                logicalLengthFrames,
                offset,
                physicalOldest,
                logicalOldest
            ))
        {
            return false;
        }

        const auto first = std::floor(position);
        return first >= static_cast<double>(physicalOldest)
            && first >= static_cast<double>(logicalOldest);
    };

    if (!checkOffset(0) || !checkOffset(last))
        return false;

    const auto firstBoundary = writeExclusive < capacityFrames
        ? capacityFrames - writeExclusive
        : 0;
    const auto logicalBoundary = writeExclusive < logicalLengthFrames
        ? logicalLengthFrames - writeExclusive
        : 0;

    for (const auto boundary : { firstBoundary, logicalBoundary })
    {
        if (boundary > 0 && boundary <= last
            && (!checkOffset(boundary - 1) || !checkOffset(boundary)))
        {
            return false;
        }
    }

    return true;
}

}
