#include "dsp/granular/GrainScheduler.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace hypercolor::dsp::granular
{
namespace
{
constexpr auto maximumExactDoubleInteger = std::uint64_t { 1 }
    << std::numeric_limits<double>::digits;
constexpr std::uint64_t timingSeedStream = 0x54494D494E47524EULL;
constexpr std::uint64_t panSeedStream = 0x50414E52414E444FULL;

bool sampleRatesMatch(double first, double second) noexcept
{
    const auto scale = std::max(std::abs(first), std::abs(second));
    return std::abs(first - second)
        <= std::numeric_limits<double>::epsilon() * scale;
}
}

GrainScheduler::GrainScheduler(std::uint64_t randomSeed) noexcept
    : initialRandomSeed(randomSeed),
      random(randomSeed),
      timingRandom(randomSeed ^ timingSeedStream),
      panRandom(randomSeed ^ panSeedStream)
{
    resetRandomState();
}

bool GrainScheduler::prepare(const core::ProcessSpec& spec) noexcept
{
    const auto validSpec = std::isfinite(spec.sampleRate)
        && spec.sampleRate >= CircularAudioBuffer::minimumSupportedSampleRate
        && spec.sampleRate <= CircularAudioBuffer::maximumSupportedSampleRate
        && spec.maximumBlockSize > 0
        && (spec.numChannels == 1 || spec.numChannels == 2);

    if (!validSpec)
    {
        sampleRate = 0.0;
        scaledPhase = 0.0;
        resetRandomState();
        pendingHead = 0;
        pendingCount = 0;
        pendingOverflowCount = 0;
        pendingExpiredCount = 0;
        pendingStaleCount = 0;
        sampleClock = 0;
        lastSuccessfulStartSample = 0;
        hasSuccessfulStart = false;
        prepared = false;
        return false;
    }

    sampleRate = spec.sampleRate;
    scaledPhase = 0.0;
    resetRandomState();
    pendingHead = 0;
    pendingCount = 0;
    pendingOverflowCount = 0;
    pendingExpiredCount = 0;
    pendingStaleCount = 0;
    sampleClock = 0;
    lastSuccessfulStartSample = 0;
    hasSuccessfulStart = false;
    prepared = true;
    return true;
}

void GrainScheduler::reset() noexcept
{
    scaledPhase = 0.0;
    resetRandomState();
    pendingHead = 0;
    pendingCount = 0;
    pendingOverflowCount = 0;
    pendingExpiredCount = 0;
    pendingStaleCount = 0;
    sampleClock = 0;
    lastSuccessfulStartSample = 0;
    hasSuccessfulStart = false;
}

GrainSchedulerStepResult GrainScheduler::processSample(
    const GrainSchedulerParameters& parameters,
    const CircularAudioBuffer& source,
    GrainPool& pool
) noexcept
{
    if (!prepared)
        return GrainSchedulerStepResult::schedulerNotPrepared;

    const auto currentSample = sampleClock;
    const auto finish = [this](GrainSchedulerStepResult value) noexcept
    {
        advanceSampleClock();
        return value;
    };

    if (hasEmittedEvent
        && samplesSinceLastEvent < std::numeric_limits<std::uint64_t>::max())
    {
        ++samplesSinceLastEvent;
    }

    if (!std::isfinite(parameters.densityGrainsPerSecond)
        || !std::isfinite(parameters.timingJitterAmount))
    {
        return finish(GrainSchedulerStepResult::invalidParameters);
    }

    bool expiredPending = false;
    ageAndExpirePendingEvents(currentSample, expiredPending);
    auto result = expiredPending
        ? GrainSchedulerStepResult::pendingExpired
        : GrainSchedulerStepResult::noEvent;

    // Discard only events that are both overdue and currently startable. This
    // keeps long history starvation observable through pendingExpired, while
    // allowing recovery to coalesce stale events without replaying them.
    auto staleDiscarded = false;
    // Before the first successful launch, retain the original head so the
    // first grain is not discarded merely because the source became valid a
    // few samples after its freshness window.  Once playback has started,
    // stale coalescing prevents an accumulated queue from becoming a burst.
    while (hasSuccessfulStart && pendingCount > 1)
    {
        const auto& pending = pendingEventAt(0);
        const auto availability = checkGrainStartAvailability(
            pending.request,
            source
        );

        if (availability == GrainSchedulerStepResult::started
            && isPendingEventTooLate(pending, currentSample))
        {
            removePendingHead();
            ++pendingStaleCount;
            staleDiscarded = true;
            continue;
        }

        break;
    }

    if (staleDiscarded)
        result = GrainSchedulerStepResult::pendingStale;

    // Retry at most one pending event per sample. The presence of a pending
    // head also prevents a newer event from bypassing it.
    auto pendingWasAttempted = false;
    if (pendingCount > 0)
    {
        pendingWasAttempted = true;
        const auto& pending = pendingEventAt(0);
        if (canStartPendingAtSample(pending, currentSample))
        {
            const auto pendingResult = attemptGrainStart(
                pending.request,
                source,
                pool
            );

            switch (pendingResult)
            {
                case GrainSchedulerStepResult::started:
                    removePendingHead();
                    recordSuccessfulStart(currentSample);
                    result = GrainSchedulerStepResult::started;
                    break;
                case GrainSchedulerStepResult::insufficientHistory:
                    result = GrainSchedulerStepResult::insufficientHistory;
                    break;
                case GrainSchedulerStepResult::noEvent:
                case GrainSchedulerStepResult::poolFull:
                case GrainSchedulerStepResult::invalidParameters:
                case GrainSchedulerStepResult::invalidTrajectory:
                case GrainSchedulerStepResult::traversalInProgress:
                case GrainSchedulerStepResult::schedulerNotPrepared:
                case GrainSchedulerStepResult::sourceUnavailable:
                case GrainSchedulerStepResult::pendingOverflow:
                case GrainSchedulerStepResult::pendingExpired:
                case GrainSchedulerStepResult::pendingStale:
                    // A pending event is only retained for temporary history
                    // starvation. Invalid trajectories, unavailable sources
                    // and pool/traversal failures are deterministic drops.
                    removePendingHead();
                    result = pendingResult;
                    break;
            }
        }
    }

    const auto density = std::clamp(
        parameters.densityGrainsPerSecond,
        minimumDensityGrainsPerSecond,
        maximumDensityGrainsPerSecond
    );

    if (density == 0.0)
        return finish(result);

    // This is the requested normalized phase equation multiplied by
    // sampleRate: phase + density / sampleRate becomes scaledPhase + density.
    const auto accumulated = scaledPhase + density;
    const auto timingThreshold = calculateTimingThreshold(
        density,
        parameters.timingJitterAmount,
        parameters.character
    );
    const auto minimumEventSpacingSamples =
        calculateMinimumSpacingSamples(maximumDensityGrainsPerSecond);
    const auto spacingAllowsEvent = !hasEmittedEvent
        || samplesSinceLastEvent >= minimumEventSpacingSamples;

    if (accumulated < timingThreshold || !spacingAllowsEvent)
    {
        scaledPhase = accumulated;
        return finish(result);
    }

    // Product limits guarantee fewer than two events per sample. Consume the
    // event before validation so a queued event cannot create timing debt.
    scaledPhase = accumulated - sampleRate;
    hasEmittedEvent = true;
    samplesSinceLastEvent = 0;
    currentTimingRandomSigned = timingRandom.nextSignedNormalized();

    PreparedGrainRequest request;
    const auto preparationResult = prepareGrainRequest(parameters, request);
    if (preparationResult != GrainSchedulerStepResult::started)
        return finish(preparationResult);

    // Timing Jitter may intentionally move adjacent events closer together
    // than the nominal Density period.  The scheduler's public guarantee is
    // the discrete maximum-density spacing, so pending catch-up must use the
    // same bound rather than the current event's nominal period.
    const auto minimumSpacingSamples = calculateMinimumSpacingSamples(
        maximumDensityGrainsPerSecond
    );

    // Preserve global chronology. A current event is queued whenever an older
    // pending event was present, even if that older event was rejected for a
    // deterministic reason on this sample.
    if (pendingWasAttempted || pendingCount > 0)
    {
        if (!enqueuePendingEvent(
                request,
                source,
                currentSample,
                minimumSpacingSamples
            ))
        {
            return finish(GrainSchedulerStepResult::pendingOverflow);
        }

        return finish(result);
    }

    if (!canStartAtSample(currentSample, minimumSpacingSamples))
    {
        if (!enqueuePendingEvent(
                request,
                source,
                currentSample,
                minimumSpacingSamples
            ))
        {
            return finish(GrainSchedulerStepResult::pendingOverflow);
        }

        return finish(result);
    }

    const auto currentResult = attemptGrainStart(request, source, pool);
    if (currentResult == GrainSchedulerStepResult::insufficientHistory)
    {
        if (!enqueuePendingEvent(
                request,
                source,
                currentSample,
                minimumSpacingSamples
            ))
        {
            return finish(GrainSchedulerStepResult::pendingOverflow);
        }

        return finish(result == GrainSchedulerStepResult::started
            ? GrainSchedulerStepResult::started
            : GrainSchedulerStepResult::insufficientHistory);
    }

    if (currentResult == GrainSchedulerStepResult::started)
    {
        recordSuccessfulStart(currentSample);
        return finish(GrainSchedulerStepResult::started);
    }

    return finish(result == GrainSchedulerStepResult::started
        ? result
        : currentResult);
}

bool GrainScheduler::isPrepared() const noexcept
{
    return prepared;
}

std::size_t GrainScheduler::getPendingEventCount() const noexcept
{
    return pendingCount;
}

std::uint64_t GrainScheduler::getPendingOverflowCount() const noexcept
{
    return pendingOverflowCount;
}

std::uint64_t GrainScheduler::getPendingExpiredCount() const noexcept
{
    return pendingExpiredCount;
}

std::uint64_t GrainScheduler::getPendingStaleCount() const noexcept
{
    return pendingStaleCount;
}

bool GrainScheduler::getPendingEventSnapshot(
    std::size_t offset,
    CausalEventSnapshot& result
) const noexcept
{
    if (offset >= pendingCount)
        return false;

    const auto& event = pendingEventAt(offset);
    if (!event.hasCausalSnapshot)
        return false;

    result = event.causalSnapshot;
    return true;
}

GrainSchedulerStepResult GrainScheduler::prepareGrainRequest(
    const GrainSchedulerParameters& parameters,
    PreparedGrainRequest& result
) noexcept
{
    if (!std::isfinite(parameters.grainSizeMilliseconds)
        || !std::isfinite(parameters.positionNormalized)
        || !std::isfinite(parameters.pitchSemitones)
        || !std::isfinite(parameters.pitchRandomAmount)
        || !std::isfinite(parameters.positionRandomAmount)
        || !std::isfinite(parameters.stereoSpreadAmount)
        || !std::isfinite(parameters.panRandomAmount))
    {
        return GrainSchedulerStepResult::invalidParameters;
    }

    const auto grainSizeMilliseconds = std::clamp(
        parameters.grainSizeMilliseconds,
        minimumGrainSizeMilliseconds,
        maximumGrainSizeMilliseconds
    );
    const auto basePositionNormalized = std::clamp(
        parameters.positionNormalized,
        0.0,
        1.0
    );
    const auto basePitchSemitones = std::clamp(
        parameters.pitchSemitones,
        minimumPitchSemitones,
        maximumPitchSemitones
    );

    if (!calculateDurationFrames(
            grainSizeMilliseconds,
            result.durationFrames
        ))
    {
        return GrainSchedulerStepResult::invalidParameters;
    }

    const auto& characterProfile = getGranularCharacterProfile(
        parameters.character
    );
    const auto pitchRandomAmount = std::clamp(
        parameters.pitchRandomAmount,
        0.0,
        1.0
    );
    const auto positionRandomAmount = std::clamp(
        parameters.positionRandomAmount,
        0.0,
        1.0
    );

    // Consume each variation stream once per generated event. The resulting
    // request is latched in a pending slot if history is not ready yet, so a
    // retry cannot create a second random variation for the same event.
    const auto pitchRandomSigned = random.nextSignedNormalized();
    const auto positionRandomSigned = random.nextSignedNormalized();
    const auto spreadRandomSigned = panRandom.nextSignedNormalized();
    const auto panRandomSigned = panRandom.nextSignedNormalized();
    const auto pitchOffset = pitchRandomSigned
        * maximumPitchRandomDeviationSemitones
        * pitchRandomAmount
        * static_cast<double>(characterProfile.pitchRandomScale);
    const auto positionOffset = positionRandomSigned
        * maximumPositionRandomDeviation
        * positionRandomAmount
        * static_cast<double>(characterProfile.positionRandomScale);
    const auto effectivePitchSemitones = std::clamp(
        basePitchSemitones + pitchOffset,
        minimumPitchSemitones,
        maximumPitchSemitones
    );
    result.positionNormalized = std::clamp(
        basePositionNormalized + positionOffset,
        0.0,
        1.0
    );

    const auto magnitude = std::exp2(effectivePitchSemitones / 12.0);
    result.playbackRate = parameters.reverse ? -magnitude : magnitude;

    const auto stereoSpreadAmount = std::clamp(
        parameters.stereoSpreadAmount,
        0.0,
        1.0
    );
    const auto panRandomAmount = std::clamp(
        parameters.panRandomAmount,
        0.0,
        1.0
    );
    result.pan = std::clamp(
        spreadRandomSigned * stereoSpreadAmount
            * static_cast<double>(characterProfile.stereoSpreadScale)
        + panRandomSigned * maximumPanRandomDeviation * panRandomAmount
            * static_cast<double>(characterProfile.panRandomScale),
        -1.0,
        1.0
    );
    result.envelope = parameters.envelope;

    if (!std::isfinite(result.playbackRate)
        || !std::isfinite(result.pan))
    {
        return GrainSchedulerStepResult::invalidParameters;
    }

    return GrainSchedulerStepResult::started;
}

GrainSchedulerStepResult GrainScheduler::attemptGrainStart(
    const PreparedGrainRequest& request,
    const CircularAudioBuffer& source,
    GrainPool& pool
) noexcept
{
    const auto availability = checkGrainStartAvailability(request, source);
    if (availability != GrainSchedulerStepResult::started)
        return availability;

    const auto sourceRegion = source.captureSourceRegion();
    double startPosition = 0.0;
    const auto positionResult = calculateStartPosition(
        sourceRegion,
        request.durationFrames,
        request.playbackRate,
        request.positionNormalized,
        startPosition
    );

    if (positionResult != StartPositionResult::success)
    {
        return positionResult == StartPositionResult::insufficientHistory
            ? GrainSchedulerStepResult::insufficientHistory
            : GrainSchedulerStepResult::invalidTrajectory;
    }

    const GrainInit init {
        sourceRegion,
        startPosition,
        request.playbackRate,
        request.durationFrames,
        request.envelope,
        request.pan
    };

    switch (pool.tryStart(init, source))
    {
        case GrainStartResult::started:
            return GrainSchedulerStepResult::started;
        case GrainStartResult::poolFull:
            return GrainSchedulerStepResult::poolFull;
        case GrainStartResult::invalidGrain:
            return GrainSchedulerStepResult::invalidTrajectory;
        case GrainStartResult::traversalInProgress:
            return GrainSchedulerStepResult::traversalInProgress;
    }

    return GrainSchedulerStepResult::invalidTrajectory;
}

GrainSchedulerStepResult GrainScheduler::checkGrainStartAvailability(
    const PreparedGrainRequest& request,
    const CircularAudioBuffer& source
) const noexcept
{
    if (!source.isPrepared()
        || !sampleRatesMatch(source.getSampleRate(), sampleRate))
    {
        return GrainSchedulerStepResult::sourceUnavailable;
    }

    const auto sourceRegion = source.captureSourceRegion();
    if (!source.isSourceRegionCurrent(sourceRegion))
        return GrainSchedulerStepResult::sourceUnavailable;

    if (sourceRegion.isEmpty())
        return GrainSchedulerStepResult::insufficientHistory;

    double startPosition = 0.0;
    const auto positionResult = calculateStartPosition(
        sourceRegion,
        request.durationFrames,
        request.playbackRate,
        request.positionNormalized,
        startPosition
    );

    if (positionResult == StartPositionResult::insufficientHistory)
        return GrainSchedulerStepResult::insufficientHistory;

    if (positionResult == StartPositionResult::invalidRepresentation)
        return GrainSchedulerStepResult::invalidTrajectory;

    if (!source.isLinearTrajectoryValid(
            sourceRegion,
            startPosition,
            request.playbackRate,
            request.durationFrames
        ))
    {
        return GrainSchedulerStepResult::invalidTrajectory;
    }

    return GrainSchedulerStepResult::started;
}

bool GrainScheduler::enqueuePendingEvent(
    const PreparedGrainRequest& request,
    const CircularAudioBuffer& source,
    std::uint64_t generationSample,
    std::uint64_t minimumSpacingSamples
) noexcept
{
    auto overflowed = false;
    if (pendingCount >= pendingEventCapacity)
    {
        ++pendingOverflowCount;
        // Keep the oldest events. This preserves the global event order and
        // lets the stale policy decide deterministically how to catch up.
        const auto newestIndex = (
            pendingHead + pendingEventCapacity - 1
        ) % pendingEventCapacity;
        pendingEvents[newestIndex] = {};
        --pendingCount;
        overflowed = true;
    }

    const auto index = (pendingHead + pendingCount) % pendingEventCapacity;
    pendingEvents[index].request = request;
    pendingEvents[index].hasCausalSnapshot =
        CausalReadPlanBuilder::captureDelayedEventSnapshot(
            source,
            source.captureSourceRegion(),
            CausalSourcePolicy::historyThenLiveFallback,
            generationSample,
            request.positionNormalized,
            request.playbackRate,
            request.durationFrames,
            pendingEvents[index].causalSnapshot
        );
    pendingEvents[index].generationSample = generationSample;
    pendingEvents[index].minimumSpacingSamples
        = std::max<std::uint64_t>(1, minimumSpacingSamples);
    ++pendingCount;
    return !overflowed;
}

void GrainScheduler::removePendingHead() noexcept
{
    if (pendingCount == 0)
        return;

    pendingEvents[pendingHead] = {};
    pendingHead = (pendingHead + 1) % pendingEventCapacity;
    --pendingCount;
}

void GrainScheduler::ageAndExpirePendingEvents(
    std::uint64_t currentSample,
    bool& expired
) noexcept
{
    expired = false;
    const auto maximumAge = static_cast<std::uint64_t>(std::ceil(
        sampleRate * pendingEventMaximumAgeSeconds
    ));

    while (pendingCount > 0
           && (currentSample < pendingEventAt(0).generationSample
               || currentSample - pendingEventAt(0).generationSample
                   >= maximumAge))
    {
        removePendingHead();
        ++pendingExpiredCount;
        expired = true;
    }
}

bool GrainScheduler::isPendingEventTooLate(
    const PendingEvent& event,
    std::uint64_t currentSample
) const noexcept
{
    if (currentSample < event.generationSample)
        return true;

    const auto elapsed = currentSample - event.generationSample;
    const auto minimumSpacing = std::max<std::uint64_t>(
        1,
        event.minimumSpacingSamples
    );
    const auto latenessLimit = minimumSpacing >
            (std::numeric_limits<std::uint64_t>::max() / 2)
        ? std::numeric_limits<std::uint64_t>::max()
        : minimumSpacing * 2;
    return elapsed > latenessLimit;
}

bool GrainScheduler::canStartAtSample(
    std::uint64_t currentSample,
    std::uint64_t minimumSpacingSamples
) const noexcept
{
    if (!hasSuccessfulStart || currentSample < lastSuccessfulStartSample)
        return true;

    const auto elapsed = currentSample - lastSuccessfulStartSample;
    return elapsed >= std::max<std::uint64_t>(
        1,
        minimumSpacingSamples
    );
}

bool GrainScheduler::canStartPendingAtSample(
    const PendingEvent& event,
    std::uint64_t currentSample
) const noexcept
{
    if (currentSample < event.generationSample)
        return false;

    const auto spacing = std::max<std::uint64_t>(
        1,
        event.minimumSpacingSamples
    );
    const auto earliestStart = event.generationSample >
            (std::numeric_limits<std::uint64_t>::max() - spacing)
        ? std::numeric_limits<std::uint64_t>::max()
        : event.generationSample + spacing;

    return currentSample >= earliestStart
        && canStartAtSample(currentSample, spacing);
}

void GrainScheduler::recordSuccessfulStart(
    std::uint64_t currentSample
) noexcept
{
    lastSuccessfulStartSample = currentSample;
    hasSuccessfulStart = true;
}

void GrainScheduler::advanceSampleClock() noexcept
{
    if (sampleClock < std::numeric_limits<std::uint64_t>::max())
        ++sampleClock;
}

std::uint64_t GrainScheduler::calculateMinimumSpacingSamples(
    double density
) const noexcept
{
    if (!std::isfinite(density) || density <= 0.0)
        return 1;

    const auto boundedDensity = std::clamp(
        density,
        minimumDensityGrainsPerSecond,
        maximumDensityGrainsPerSecond
    );
    const auto exactSpacing = sampleRate / boundedDensity;

    if (!std::isfinite(exactSpacing) || exactSpacing <= 0.0)
        return 1;

    return std::max<std::uint64_t>(
        1,
        static_cast<std::uint64_t>(std::floor(exactSpacing))
    );
}

GrainScheduler::PendingEvent& GrainScheduler::pendingEventAt(
    std::size_t offset
) noexcept
{
    return pendingEvents[(pendingHead + offset) % pendingEventCapacity];
}

const GrainScheduler::PendingEvent& GrainScheduler::pendingEventAt(
    std::size_t offset
) const noexcept
{
    return pendingEvents[(pendingHead + offset) % pendingEventCapacity];
}

void GrainScheduler::resetRandomState() noexcept
{
    random.reset(initialRandomSeed);
    timingRandom.reset(initialRandomSeed ^ timingSeedStream);
    panRandom.reset(initialRandomSeed ^ panSeedStream);
    currentTimingRandomSigned = timingRandom.nextSignedNormalized();
    samplesSinceLastEvent = 0;
    hasEmittedEvent = false;
}

double GrainScheduler::calculateTimingThreshold(
    double density,
    double timingJitterAmount,
    GranularCharacter character
) const noexcept
{
    const auto amount = std::clamp(timingJitterAmount, 0.0, 1.0);
    const auto& profile = getGranularCharacterProfile(character);
    const auto requestedAmplitude = sampleRate
        * maximumTimingJitterFraction
        * amount
        * static_cast<double>(profile.timingJitterScale);

    // Adjacent absolute event offsets may differ by twice the amplitude.
    // This bound leaves at least the 120 grains/s phase-domain spacing. It
    // naturally collapses to zero as Density approaches its maximum.
    const auto orderingAmplitudeLimit = 0.5 * sampleRate
        * std::max(
            0.0,
            1.0 - density / maximumDensityGrainsPerSecond
        );
    const auto amplitude = std::min(
        requestedAmplitude,
        orderingAmplitudeLimit
    );

    return sampleRate + currentTimingRandomSigned * amplitude;
}

bool GrainScheduler::calculateDurationFrames(
    double grainSizeMilliseconds,
    std::uint32_t& result
) const noexcept
{
    result = 0;

    const auto exactFrames = static_cast<long double>(
            grainSizeMilliseconds
        ) * static_cast<long double>(sampleRate) / 1000.0L;
    const auto roundedFrames = std::round(exactFrames);

    if (!std::isfinite(roundedFrames)
        || roundedFrames < 0.0L
        || roundedFrames
            > static_cast<long double>(
                std::numeric_limits<std::uint32_t>::max()
            ))
    {
        return false;
    }

    result = std::max(
        Grain::minimumDurationFrames,
        static_cast<std::uint32_t>(roundedFrames)
    );
    return true;
}

GrainScheduler::StartPositionResult
GrainScheduler::calculateStartPosition(
    const CircularAudioBuffer::SourceRegion& sourceRegion,
    std::uint32_t durationFrames,
    double playbackRate,
    double positionNormalized,
    double& result
) const noexcept
{
    result = 0.0;

    if (sourceRegion.isEmpty()
        || durationFrames < Grain::minimumDurationFrames
        || !std::isfinite(playbackRate)
        || !std::isfinite(positionNormalized))
    {
        return StartPositionResult::invalidRepresentation;
    }

    const auto oldest = static_cast<long double>(sourceRegion.oldest);
    const auto newestExclusive = static_cast<long double>(
        sourceRegion.newestExclusive
    );
    const auto travel = std::abs(static_cast<long double>(playbackRate))
        * static_cast<long double>(durationFrames - 1);
    const auto oldestFreshEndpoint = oldest + travel;
    const auto upperFreshEndpointExclusive = newestExclusive - 1.0L;

    if (!std::isfinite(travel)
        || !std::isfinite(oldestFreshEndpoint)
        || !std::isfinite(upperFreshEndpointExclusive)
        || oldestFreshEndpoint >= upperFreshEndpointExclusive)
    {
        return StartPositionResult::insufficientHistory;
    }

    const auto upperAsDouble = static_cast<double>(
        upperFreshEndpointExclusive
    );

    if (!std::isfinite(upperAsDouble))
        return StartPositionResult::invalidRepresentation;

    auto freshestEndpoint = std::nextafter(
        upperAsDouble,
        -std::numeric_limits<double>::infinity()
    );

    if (!std::isfinite(freshestEndpoint)
        || static_cast<long double>(freshestEndpoint)
            >= upperFreshEndpointExclusive
        || static_cast<long double>(freshestEndpoint)
            < oldestFreshEndpoint)
    {
        return StartPositionResult::invalidRepresentation;
    }

    const auto selectedEndpoint = static_cast<long double>(freshestEndpoint)
        - static_cast<long double>(positionNormalized)
            * (static_cast<long double>(freshestEndpoint)
                - oldestFreshEndpoint);
    auto representableEndpoint = static_cast<double>(selectedEndpoint);

    if (!std::isfinite(representableEndpoint))
        return StartPositionResult::invalidRepresentation;

    if (static_cast<long double>(representableEndpoint)
        < oldestFreshEndpoint)
    {
        representableEndpoint = std::nextafter(
            representableEndpoint,
            std::numeric_limits<double>::infinity()
        );
    }

    if (!std::isfinite(representableEndpoint)
        || representableEndpoint > freshestEndpoint
        || static_cast<long double>(representableEndpoint)
            < oldestFreshEndpoint
        || static_cast<long double>(representableEndpoint)
            >= upperFreshEndpointExclusive)
    {
        return StartPositionResult::invalidRepresentation;
    }

    const auto startAsLongDouble = playbackRate < 0.0
        ? static_cast<long double>(representableEndpoint)
        : static_cast<long double>(representableEndpoint) - travel;
    auto start = static_cast<double>(startAsLongDouble);

    if (!std::isfinite(start))
        return StartPositionResult::invalidRepresentation;

    if (static_cast<long double>(start) < oldest)
    {
        start = std::nextafter(
            start,
            std::numeric_limits<double>::infinity()
        );
    }

    if (!std::isfinite(start)
        || start < 0.0
        || start >= static_cast<double>(maximumExactDoubleInteger)
        || static_cast<long double>(start) < oldest)
    {
        return StartPositionResult::invalidRepresentation;
    }

    result = start;
    return StartPositionResult::success;
}
}
