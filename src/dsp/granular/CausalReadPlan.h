#pragma once

#include "dsp/granular/CircularAudioBuffer.h"

#include <cstdint>

namespace hypercolor::dsp::granular
{
enum class CausalSourcePolicy : std::uint8_t
{
    historyOnly,
    causalLiveOnly,
    historyThenLiveFallback
};

enum class CausalSourceKind : std::uint8_t
{
    history,
    causalLive
};

enum class CausalReadFeasibility : std::uint8_t
{
    valid,
    notPrepared,
    invalidEventSnapshot,
    invalidEventTimestamp,
    generationMismatch,
    insufficientHistory,
    logicalRangeInvalid,
    physicalRangeInvalid,
    causalViolation,
    fallbackNotAllowed,
    expired
};

// A bounded, allocation-free contract for one generated Scheduler event.
// The event snapshot is immutable while the event is pending. In particular,
// its effective Position, playback rate, grain length, jitter result and
// source policy must not be regenerated on retry.
struct CausalEventSnapshot
{
    CausalSourcePolicy sourcePolicy
        = CausalSourcePolicy::historyOnly;
    CircularAudioBuffer::Generation generation = 0;
    CircularAudioBuffer::AbsoluteFrame eventSample = 0;
    double positionNormalized = 0.0;
    double playbackRate = 1.0;
    std::uint32_t grainLengthFrames = 0;
};

struct CausalReadPlan
{
    CausalSourcePolicy sourcePolicy
        = CausalSourcePolicy::historyThenLiveFallback;
    CausalSourceKind sourceKind = CausalSourceKind::history;
    CircularAudioBuffer::Generation generation = 0;
    CircularAudioBuffer::AbsoluteFrame eventSample = 0;
    CircularAudioBuffer::AbsoluteFrame captureWriteExclusive = 0;
    CircularAudioBuffer::SourceRegion historyRegion {};
    double anchor = 0.0;
    double positionNormalized = 0.0;
    double playbackRate = 1.0;
    std::uint32_t grainLengthFrames = 0;
    std::uint32_t causalMarginSamples = 2;
    std::uint64_t requiredPrehistoryFrames = 0;
    std::uint64_t causalReadyWriteHead = 0;
    CircularAudioBuffer::AbsoluteFrame earliestValidWriteHead = 0;
    bool usedLiveFallback = false;
    CausalReadFeasibility feasibility
        = CausalReadFeasibility::invalidEventSnapshot;

    [[nodiscard]] bool isValid() const noexcept
    {
        return feasibility == CausalReadFeasibility::valid;
    }
};

class CausalReadPlanBuilder final
{
public:
    static constexpr std::uint32_t defaultCausalMarginSamples = 2;

    // Captures only immutable Event-Time metadata. SourceRegion and anchor
    // are intentionally deferred until launch planning.
    [[nodiscard]] static bool captureEventSnapshot(
        CausalSourcePolicy sourcePolicy,
        CircularAudioBuffer::Generation generation,
        CircularAudioBuffer::AbsoluteFrame eventSample,
        double positionNormalized,
        double playbackRate,
        std::uint32_t grainLengthFrames,
        CausalEventSnapshot& result
    ) noexcept;

    // Convenience overload for callers/tests that also want capture-time
    // source validation. It records metadata only; no SourceRegion is retained.
    [[nodiscard]] static bool captureEventSnapshot(
        const CircularAudioBuffer& source,
        CausalSourcePolicy sourcePolicy,
        CircularAudioBuffer::AbsoluteFrame eventSample,
        double positionNormalized,
        double playbackRate,
        std::uint32_t grainLengthFrames,
        CausalEventSnapshot& result,
        std::uint32_t causalMarginSamples = defaultCausalMarginSamples
    ) noexcept;

    // Captures one Launch-Time SourceRegion and resolves the plan against
    // that same region. Callers pass this plan's region and anchor onward.
    [[nodiscard]] static CausalReadPlan buildAtLaunch(
        const CircularAudioBuffer& source,
        const CausalEventSnapshot& event,
        const CircularAudioBuffer::SourceRegion& captureRegion
    ) noexcept;

    // Convenience overload: obtains the current Launch-Time region once.
    // The Scheduler integration uses the explicit-region overload above.
    [[nodiscard]] static CausalReadPlan buildAtLaunch(
        const CircularAudioBuffer& source,
        const CausalEventSnapshot& event
    ) noexcept;

    [[nodiscard]] static std::uint64_t requiredPrehistoryFrames(
        double playbackRate,
        std::uint32_t grainLengthFrames,
        std::uint32_t causalMarginSamples = defaultCausalMarginSamples
    ) noexcept;

    [[nodiscard]] static double calculateHistoryFreshEndpoint(
        const CircularAudioBuffer::SourceRegion& region
    ) noexcept;

    [[nodiscard]] static bool isConservativelyCausal(
        double startPosition,
        double playbackRate,
        std::uint32_t grainLengthFrames,
        CircularAudioBuffer::AbsoluteFrame writeExclusive,
        std::uint32_t causalMarginSamples
    ) noexcept;

private:
    [[nodiscard]] static bool isKnownPolicy(
        CausalSourcePolicy policy
    ) noexcept;

    [[nodiscard]] static bool isValidMargin(
        std::uint32_t causalMarginSamples
    ) noexcept;

    [[nodiscard]] static bool validEvent(const CausalEventSnapshot& event)
        noexcept;

    [[nodiscard]] static bool calculateEndpointRange(
        CircularAudioBuffer::AbsoluteFrame logicalOldest,
        CircularAudioBuffer::AbsoluteFrame writeExclusive,
        double playbackRate,
        std::uint32_t grainLengthFrames,
        std::uint32_t causalMarginSamples,
        double& oldestEndpoint,
        double& freshestEndpoint
    ) noexcept;

    [[nodiscard]] static bool calculateStartFromEndpoint(
        double endpoint,
        double playbackRate,
        std::uint32_t grainLengthFrames,
        double& startPosition
    ) noexcept;

    [[nodiscard]] static bool isCausalTrajectoryValid(
        const CircularAudioBuffer& source,
        double startPosition,
        double playbackRate,
        std::uint32_t grainLengthFrames,
        std::uint32_t causalMarginSamples
    ) noexcept;

    [[nodiscard]] static bool isFutureRangeValid(
        CircularAudioBuffer::AbsoluteFrame writeExclusive,
        std::uint64_t capacityFrames,
        std::uint64_t logicalLengthFrames,
        double startPosition,
        double playbackRate,
        std::uint32_t grainLengthFrames
    ) noexcept;

};
}
