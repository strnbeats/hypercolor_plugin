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
        case CausalSourcePolicy::livePreferredHistoryFallback:
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
    const auto historyAttempt = [&]() noexcept
    {
        return buildHistoryPlan(source, event, captureRegion, plan);
    };
    const auto liveAttempt = [&]() noexcept
    {
        return buildLivePlan(source, event, captureRegion, plan);
    };

    CausalReadFeasibility failure = CausalReadFeasibility::fallbackNotAllowed;
    switch (event.sourcePolicy)
    {
        case CausalSourcePolicy::historyOnly:
            failure = historyAttempt();
            break;
        case CausalSourcePolicy::causalLiveOnly:
            failure = liveAttempt();
            break;
        case CausalSourcePolicy::historyThenLiveFallback:
            failure = historyAttempt();
            if (failure != CausalReadFeasibility::valid)
            {
                plan = {};
                plan.sourcePolicy = event.sourcePolicy;
                plan.generation = event.generation;
                plan.eventSample = event.eventSample;
                plan.captureWriteExclusive = captureRegion.newestExclusive;
                plan.positionNormalized = event.positionNormalized;
                plan.playbackRate = event.playbackRate;
                plan.grainLengthFrames = event.grainLengthFrames;
                failure = liveAttempt();
                if (failure == CausalReadFeasibility::valid)
                    plan.usedLiveFallback = true;
            }
            break;
        case CausalSourcePolicy::livePreferredHistoryFallback:
            failure = liveAttempt();
            if (failure != CausalReadFeasibility::valid)
            {
                plan = {};
                plan.sourcePolicy = event.sourcePolicy;
                plan.generation = event.generation;
                plan.eventSample = event.eventSample;
                plan.captureWriteExclusive = captureRegion.newestExclusive;
                plan.positionNormalized = event.positionNormalized;
                plan.playbackRate = event.playbackRate;
                plan.grainLengthFrames = event.grainLengthFrames;
                failure = historyAttempt();
                if (failure == CausalReadFeasibility::valid)
                    plan.usedLiveFallback = false;
            }
            break;
    }
    plan.feasibility = failure;
    return plan;
}

CausalReadFeasibility CausalReadPlanBuilder::buildHistoryPlan(
    const CircularAudioBuffer& source,
    const CausalEventSnapshot& event,
    const CircularAudioBuffer::SourceRegion& captureRegion,
    CausalReadPlan& plan
) noexcept
{
    const auto travel = std::abs(static_cast<long double>(event.playbackRate))
        * static_cast<long double>(event.grainLengthFrames - 1);
    const auto oldest = static_cast<long double>(captureRegion.oldest);
    const auto upperExclusive
        = static_cast<long double>(captureRegion.newestExclusive) - 1.0L;
    const auto oldestEndpoint = oldest + travel;
    const auto freshestEndpoint = calculateHistoryFreshEndpoint(captureRegion);
    if (!std::isfinite(travel) || !std::isfinite(oldestEndpoint)
        || static_cast<long double>(freshestEndpoint) < oldestEndpoint)
    {
        return CausalReadFeasibility::insufficientHistory;
    }

    const auto selected = static_cast<long double>(freshestEndpoint)
        - static_cast<long double>(event.positionNormalized)
            * (static_cast<long double>(freshestEndpoint) - oldestEndpoint);
    auto endpoint = static_cast<double>(selected);
    if (!std::isfinite(endpoint))
        return CausalReadFeasibility::causalViolation;
    if (static_cast<long double>(endpoint) < oldestEndpoint)
        endpoint = std::nextafter(endpoint, std::numeric_limits<double>::infinity());
    if (!std::isfinite(endpoint) || endpoint > freshestEndpoint
        || static_cast<long double>(endpoint) < oldestEndpoint
        || static_cast<long double>(endpoint) >= upperExclusive)
    {
        return CausalReadFeasibility::causalViolation;
    }

    const auto startValue = event.playbackRate < 0.0
        ? static_cast<long double>(endpoint)
        : static_cast<long double>(endpoint) - travel;
    auto anchor = static_cast<double>(startValue);
    if (!std::isfinite(anchor))
        return CausalReadFeasibility::causalViolation;
    if (static_cast<long double>(anchor) < oldest)
        anchor = std::nextafter(anchor, std::numeric_limits<double>::infinity());
    if (!std::isfinite(anchor) || anchor < 0.0
        || anchor >= maximumExactDoubleInteger
        || static_cast<long double>(anchor) < oldest)
    {
        return CausalReadFeasibility::causalViolation;
    }

    if (!source.isLinearTrajectoryValid(
            captureRegion, anchor, event.playbackRate, event.grainLengthFrames))
    {
        return CausalReadFeasibility::physicalRangeInvalid;
    }

    plan.sourceKind = CausalSourceKind::history;
    plan.anchor = anchor;
    plan.historyRegion = captureRegion;
    plan.requiredPrehistoryFrames = 0;
    plan.causalReadyWriteHead = captureRegion.newestExclusive;
    plan.earliestValidWriteHead = captureRegion.newestExclusive;
    return CausalReadFeasibility::valid;
}

CausalReadFeasibility CausalReadPlanBuilder::buildLivePlan(
    const CircularAudioBuffer& source,
    const CausalEventSnapshot& event,
    const CircularAudioBuffer::SourceRegion& captureRegion,
    CausalReadPlan& plan
) noexcept
{
    const auto write = captureRegion.newestExclusive;
    const auto last = static_cast<long double>(event.grainLengthFrames - 1);
    const auto rate = static_cast<long double>(event.playbackRate);
    const auto margin = static_cast<long double>(plan.causalMarginSamples);
    if (write < plan.causalMarginSamples
        || write >= static_cast<CircularAudioBuffer::AbsoluteFrame>(maximumExactDoubleInteger))
    {
        return CausalReadFeasibility::insufficientHistory;
    }

    // Position interpolates the complete range of causal launch anchors.
    // The oldest anchor accounts for the moving logical oldest during render;
    // the freshest anchor accounts for the closest legal interpolation pair.
    const auto oldestAnchorExact
        = static_cast<long double>(captureRegion.oldest)
            + std::max(0.0L, 1.0L - rate) * last;
    const auto freshestAnchorExact
        = static_cast<long double>(write) - margin
            - std::max(0.0L, rate - 1.0L) * last;
    if (!std::isfinite(oldestAnchorExact)
        || !std::isfinite(freshestAnchorExact)
        || freshestAnchorExact < oldestAnchorExact)
    {
        return CausalReadFeasibility::insufficientHistory;
    }

    auto oldestAnchor = static_cast<double>(oldestAnchorExact);
    auto freshestAnchor = static_cast<double>(freshestAnchorExact);
    if (static_cast<long double>(oldestAnchor) < oldestAnchorExact)
        oldestAnchor = std::nextafter(oldestAnchor,
            std::numeric_limits<double>::infinity());
    if (static_cast<long double>(freshestAnchor) > freshestAnchorExact)
        freshestAnchor = std::nextafter(freshestAnchor,
            -std::numeric_limits<double>::infinity());
    if (!finiteAndRepresentable(oldestAnchor)
        || !finiteAndRepresentable(freshestAnchor)
        || freshestAnchor < oldestAnchor)
    {
        return CausalReadFeasibility::insufficientHistory;
    }

    const auto selected = static_cast<long double>(freshestAnchor)
        - static_cast<long double>(event.positionNormalized)
            * (static_cast<long double>(freshestAnchor) - oldestAnchor);
    auto anchor = static_cast<double>(selected);
    if (!std::isfinite(anchor))
        return CausalReadFeasibility::causalViolation;
    anchor = std::clamp(anchor, oldestAnchor, freshestAnchor);

    if (!source.isCausalLiveTrajectoryValid(
            event.generation,
            anchor,
            event.playbackRate,
            event.grainLengthFrames,
            plan.causalMarginSamples))
    {
        if (!isConservativelyCausal(
                anchor,
                event.playbackRate,
                event.grainLengthFrames,
                write,
                plan.causalMarginSamples))
        {
            return CausalReadFeasibility::causalViolation;
        }
        return CausalReadFeasibility::physicalRangeInvalid;
    }

    const auto lastPosition = CircularAudioBuffer::calculateLinearTrajectoryPosition(
        anchor, event.playbackRate, event.grainLengthFrames - 1);
    const auto oldestRead = std::min(anchor, lastPosition);
    const auto needed = std::max(
        static_cast<long double>(plan.causalMarginSamples),
        static_cast<long double>(write) - static_cast<long double>(oldestRead));
    if (!checkedCeilToFrames(static_cast<double>(needed),
            plan.requiredPrehistoryFrames))
    {
        return CausalReadFeasibility::causalViolation;
    }

    plan.sourceKind = CausalSourceKind::causalLive;
    plan.anchor = anchor;
    plan.historyRegion = {};
    plan.causalReadyWriteHead = std::max(
        event.eventSample + 1,
        plan.requiredPrehistoryFrames);
    plan.earliestValidWriteHead = plan.causalReadyWriteHead;
    return CausalReadFeasibility::valid;
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

    const auto extraPerFrame = playbackRate < 0.0
        ? 1.0 - playbackRate
        : std::max(0.0, playbackRate - 1.0);
    const auto extra = extraPerFrame
        * static_cast<double>(grainLengthFrames - 1);
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


}
