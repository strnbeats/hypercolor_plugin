#pragma once

#include "core/ProcessSpec.h"
#include "dsp/granular/GrainPool.h"
#include "dsp/granular/GranularCharacter.h"
#include "dsp/granular/GranularRandom.h"

#include <cstdint>

namespace hypercolor::dsp::granular
{
struct GrainSchedulerParameters
{
    double densityGrainsPerSecond = 20.0;
    double grainSizeMilliseconds = 100.0;
    double positionNormalized = 0.0;
    double pitchSemitones = 0.0;
    bool reverse = false;
    GranularCharacter character = GranularCharacter::Musical;
    double pitchRandomAmount = 0.0;
    double positionRandomAmount = 0.0;
    double timingJitterAmount = 0.0;
    double stereoSpreadAmount = 0.0;
    double panRandomAmount = 0.0;
};

enum class GrainSchedulerStepResult : std::uint8_t
{
    noEvent,
    started,
    poolFull,
    insufficientHistory,
    invalidParameters,
    invalidTrajectory,
    traversalInProgress,
    schedulerNotPrepared,
    sourceUnavailable
};

class GrainScheduler final
{
public:
    static constexpr double minimumDensityGrainsPerSecond = 0.0;
    static constexpr double maximumDensityGrainsPerSecond = 120.0;
    static constexpr double minimumGrainSizeMilliseconds = 10.0;
    static constexpr double maximumGrainSizeMilliseconds = 500.0;
    static constexpr double minimumPitchSemitones = -24.0;
    static constexpr double maximumPitchSemitones = 24.0;
    static constexpr double maximumPitchRandomDeviationSemitones = 12.0;
    static constexpr double maximumPositionRandomDeviation = 0.25;
    static constexpr double maximumTimingJitterFraction = 0.45;
    static constexpr double maximumPanRandomDeviation = 1.0;
    static constexpr std::uint64_t defaultRandomSeed
        = 0x4859504552434F4CULL;

    explicit GrainScheduler(
        std::uint64_t randomSeed = defaultRandomSeed
    ) noexcept;
    ~GrainScheduler() = default;

    GrainScheduler(const GrainScheduler&) = delete;
    GrainScheduler& operator=(const GrainScheduler&) = delete;
    GrainScheduler(GrainScheduler&&) = delete;
    GrainScheduler& operator=(GrainScheduler&&) = delete;

    [[nodiscard]] bool prepare(const core::ProcessSpec& spec) noexcept;
    void reset() noexcept;

    [[nodiscard]] GrainSchedulerStepResult processSample(
        const GrainSchedulerParameters& parameters,
        const CircularAudioBuffer& source,
        GrainPool& pool
    ) noexcept;

    [[nodiscard]] bool isPrepared() const noexcept;

private:
    enum class StartPositionResult : std::uint8_t
    {
        success,
        insufficientHistory,
        invalidRepresentation
    };

    [[nodiscard]] bool calculateDurationFrames(
        double grainSizeMilliseconds,
        std::uint32_t& result
    ) const noexcept;

    [[nodiscard]] StartPositionResult calculateStartPosition(
        const CircularAudioBuffer::SourceRegion& sourceRegion,
        std::uint32_t durationFrames,
        double playbackRate,
        double positionNormalized,
        double& result
    ) const noexcept;

    void resetRandomState() noexcept;
    [[nodiscard]] double calculateTimingThreshold(
        double density,
        double timingJitterAmount,
        GranularCharacter character
    ) const noexcept;

    double sampleRate = 0.0;
    // Stores normalized phase multiplied by sampleRate. This scaled form is
    // algebraically equivalent to adding density / sampleRate per sample and
    // avoids avoidable rounding at exact event boundaries.
    double scaledPhase = 0.0;
    std::uint64_t initialRandomSeed = defaultRandomSeed;
    GranularRandom random;
    GranularRandom timingRandom;
    GranularRandom panRandom;
    double currentTimingRandomSigned = 0.0;
    std::uint64_t samplesSinceLastEvent = 0;
    bool hasEmittedEvent = false;
    bool prepared = false;
};
}
