#pragma once

#include "core/ProcessSpec.h"
#include "dsp/granular/GrainPool.h"
#include "dsp/granular/GranularCharacter.h"
#include "dsp/granular/GranularRandom.h"
#include "dsp/granular/CausalReadPlan.h"

#include <array>
#include <cstddef>
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
    GrainWindow envelope = GrainWindow::hann;
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
    sourceUnavailable,
    pendingOverflow,
    pendingExpired,
    pendingStale
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
    static constexpr std::size_t pendingEventCapacity = 8;
    static constexpr double pendingEventMaximumAgeSeconds = 4.0;

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

    [[nodiscard]] std::size_t getPendingEventCount() const noexcept;
    [[nodiscard]] std::uint64_t getPendingOverflowCount() const noexcept;
    [[nodiscard]] std::uint64_t getPendingExpiredCount() const noexcept;
    [[nodiscard]] std::uint64_t getPendingStaleCount() const noexcept;
    [[nodiscard]] bool getPendingEventSnapshot(
        std::size_t offset,
        CausalEventSnapshot& result
    ) const noexcept;


private:
    struct PreparedGrainRequest
    {
        double positionNormalized = 0.0;
        double playbackRate = 1.0;
        double pan = 0.0;
        std::uint32_t durationFrames = 0;
        GrainWindow envelope = GrainWindow::hann;
    };

    struct PendingEvent
    {
        PreparedGrainRequest request {};
        CausalEventSnapshot event {};
        std::uint64_t minimumSpacingSamples = 1;
    };

    [[nodiscard]] GrainSchedulerStepResult prepareGrainRequest(
        const GrainSchedulerParameters& parameters,
        PreparedGrainRequest& result
    ) noexcept;

    [[nodiscard]] GrainSchedulerStepResult attemptGrainStart(
        const PreparedGrainRequest& request,
        const CausalEventSnapshot& event,
        const CircularAudioBuffer& source,
        GrainPool& pool
    ) noexcept;

    [[nodiscard]] GrainSchedulerStepResult launchPlannedGrain(
        const PreparedGrainRequest& request,
        const CausalReadPlan& plan,
        const CircularAudioBuffer& source,
        GrainPool& pool
    ) noexcept;

    [[nodiscard]] GrainSchedulerStepResult checkGrainStartAvailability(
        const CausalEventSnapshot& event,
        const CircularAudioBuffer& source,
        const CircularAudioBuffer::SourceRegion& captureRegion,
        CausalReadPlan& plan
    ) const noexcept;

    [[nodiscard]] bool enqueuePendingEvent(
        const PreparedGrainRequest& request,
        const CausalEventSnapshot& event,
        std::uint64_t minimumSpacingSamples
    ) noexcept;

    void removePendingHead() noexcept;
    void ageAndExpirePendingEvents(
        std::uint64_t currentSample,
        bool& expired
    ) noexcept;
    [[nodiscard]] bool isPendingEventTooLate(
        const PendingEvent& event,
        std::uint64_t currentSample
    ) const noexcept;
    [[nodiscard]] bool canStartAtSample(
        std::uint64_t currentSample,
        std::uint64_t minimumSpacingSamples
    ) const noexcept;
    [[nodiscard]] bool canStartPendingAtSample(
        const PendingEvent& event,
        std::uint64_t currentSample
    ) const noexcept;
    void recordSuccessfulStart(std::uint64_t currentSample) noexcept;
    void advanceSampleClock() noexcept;
    [[nodiscard]] std::uint64_t calculateMinimumSpacingSamples(
        double density
    ) const noexcept;

    [[nodiscard]] PendingEvent& pendingEventAt(
        std::size_t offset
    ) noexcept;

    [[nodiscard]] const PendingEvent& pendingEventAt(
        std::size_t offset
    ) const noexcept;

    [[nodiscard]] bool calculateDurationFrames(
        double grainSizeMilliseconds,
        std::uint32_t& result
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
    std::array<PendingEvent, pendingEventCapacity> pendingEvents {};
    std::size_t pendingHead = 0;
    std::size_t pendingCount = 0;
    std::uint64_t pendingOverflowCount = 0;
    std::uint64_t pendingExpiredCount = 0;
    std::uint64_t pendingStaleCount = 0;
    std::uint64_t sampleClock = 0;
    std::uint64_t lastSuccessfulStartSample = 0;
    bool hasSuccessfulStart = false;
};
}
