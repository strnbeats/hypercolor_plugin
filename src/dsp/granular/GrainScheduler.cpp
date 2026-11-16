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
        prepared = false;
        return false;
    }

    sampleRate = spec.sampleRate;
    scaledPhase = 0.0;
    resetRandomState();
    prepared = true;
    return true;
}

void GrainScheduler::reset() noexcept
{
    scaledPhase = 0.0;
    resetRandomState();
}

GrainSchedulerStepResult GrainScheduler::processSample(
    const GrainSchedulerParameters& parameters,
    const CircularAudioBuffer& source,
    GrainPool& pool
) noexcept
{
    if (!prepared)
        return GrainSchedulerStepResult::schedulerNotPrepared;

    if (hasEmittedEvent
        && samplesSinceLastEvent < std::numeric_limits<std::uint64_t>::max())
    {
        ++samplesSinceLastEvent;
    }

    if (!std::isfinite(parameters.densityGrainsPerSecond)
        || !std::isfinite(parameters.timingJitterAmount))
    {
        return GrainSchedulerStepResult::invalidParameters;
    }

    const auto density = std::clamp(
        parameters.densityGrainsPerSecond,
        minimumDensityGrainsPerSecond,
        maximumDensityGrainsPerSecond
    );

    if (density == 0.0)
        return GrainSchedulerStepResult::noEvent;

    // This is the requested normalized phase equation multiplied by
    // sampleRate: phase + density / sampleRate becomes scaledPhase + density.
    const auto accumulated = scaledPhase + density;
    const auto timingThreshold = calculateTimingThreshold(
        density,
        parameters.timingJitterAmount,
        parameters.character
    );
    const auto minimumSpacingSamples = static_cast<std::uint64_t>(
        std::floor(sampleRate / maximumDensityGrainsPerSecond)
    );
    const auto spacingAllowsEvent = !hasEmittedEvent
        || samplesSinceLastEvent >= minimumSpacingSamples;

    if (accumulated < timingThreshold || !spacingAllowsEvent)
    {
        scaledPhase = accumulated;
        return GrainSchedulerStepResult::noEvent;
    }

    // Product limits guarantee fewer than two events per sample. Consume the
    // event before validation so failed starts never create timing debt.
    scaledPhase = accumulated - sampleRate;
    hasEmittedEvent = true;
    samplesSinceLastEvent = 0;
    currentTimingRandomSigned = timingRandom.nextSignedNormalized();

    if (!source.isPrepared()
        || !sampleRatesMatch(source.getSampleRate(), sampleRate))
        return GrainSchedulerStepResult::sourceUnavailable;

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

    std::uint32_t durationFrames = 0;

    if (!calculateDurationFrames(grainSizeMilliseconds, durationFrames))
        return GrainSchedulerStepResult::invalidParameters;

    const auto sourceRegion = source.captureSourceRegion();

    if (!source.isSourceRegionCurrent(sourceRegion))
        return GrainSchedulerStepResult::sourceUnavailable;

    if (sourceRegion.isEmpty())
        return GrainSchedulerStepResult::insufficientHistory;

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

    // Every viable start attempt consumes exactly two values in a documented
    // order, even when an amount is zero. This keeps later variations stable
    // when only their depths change.
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
    const auto effectivePositionNormalized = std::clamp(
        basePositionNormalized + positionOffset,
        0.0,
        1.0
    );

    const auto magnitude = std::exp2(effectivePitchSemitones / 12.0);
    const auto playbackRate = parameters.reverse ? -magnitude : magnitude;

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
    const auto pan = std::clamp(
        spreadRandomSigned * stereoSpreadAmount
            * static_cast<double>(characterProfile.stereoSpreadScale)
        + panRandomSigned * maximumPanRandomDeviation * panRandomAmount
            * static_cast<double>(characterProfile.panRandomScale),
        -1.0,
        1.0
    );

    if (!std::isfinite(playbackRate) || !std::isfinite(pan))
        return GrainSchedulerStepResult::invalidParameters;

    double startPosition = 0.0;

    const auto positionResult = calculateStartPosition(
        sourceRegion,
        durationFrames,
        playbackRate,
        effectivePositionNormalized,
        startPosition
    );

    if (positionResult == StartPositionResult::insufficientHistory)
    {
        return GrainSchedulerStepResult::insufficientHistory;
    }

    if (positionResult == StartPositionResult::invalidRepresentation)
        return GrainSchedulerStepResult::invalidTrajectory;

    if (!source.isLinearTrajectoryValid(
            sourceRegion,
            startPosition,
            playbackRate,
            durationFrames
        ))
    {
        return GrainSchedulerStepResult::invalidTrajectory;
    }

    const GrainInit init {
        sourceRegion,
        startPosition,
        playbackRate,
        durationFrames,
        parameters.envelope,
        pan
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

bool GrainScheduler::isPrepared() const noexcept
{
    return prepared;
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
