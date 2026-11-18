#include "core/ProcessSpec.h"
#include "dsp/granular/CausalReadPlan.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>

namespace
{
using hypercolor::core::ProcessSpec;
using hypercolor::dsp::granular::CausalEventSnapshot;
using hypercolor::dsp::granular::CausalReadFeasibility;
using hypercolor::dsp::granular::CausalReadPlanBuilder;
using hypercolor::dsp::granular::CausalSourceKind;
using hypercolor::dsp::granular::CausalSourcePolicy;
using hypercolor::dsp::granular::CircularAudioBuffer;

class TestContext
{
public:
    void expect(bool condition, const char* message)
    {
        if (condition)
            return;

        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }

    void expectNear(double actual, double expected, double tolerance, const char* message)
    {
        expect(std::abs(actual - expected) <= tolerance, message);
    }

    [[nodiscard]] int getFailures() const noexcept
    {
        return failures;
    }

private:
    int failures = 0;
};

ProcessSpec makeSpec(double sampleRate = 48000.0)
{
    return { sampleRate, 512, 1 };
}

bool fill(CircularAudioBuffer& buffer, std::size_t count)
{
    const std::array frame { 0.25f };
    for (std::size_t index = 0; index < count; ++index)
    {
        if (!buffer.writeFrame(frame))
            return false;
    }

    return true;
}

bool prepareAndFill(
    CircularAudioBuffer& buffer,
    double sampleRate = 48000.0,
    double maximumSeconds = 1.0,
    double logicalSeconds = 0.5,
    std::size_t frames = 24000
)
{
    if (!buffer.prepare(
            makeSpec(sampleRate),
            maximumSeconds,
            logicalSeconds
        ))
    {
        return false;
    }

    return fill(buffer, frames);
}

void testWriteExclusiveAndInterpolation(TestContext& context)
{
    CircularAudioBuffer buffer;
    context.expect(
        prepareAndFill(buffer, 44100.0, 0.1, 0.1, 4),
        "small buffer should prepare and fill"
    );
    const auto region = buffer.captureSourceRegion();

    context.expect(
        buffer.getAbsoluteWritePosition() == 4,
        "write head should be exclusive"
    );
    context.expect(
        buffer.isLinearReadPositionValid(region, 2.0),
        "M=2 integer endpoint should retain both taps"
    );
    context.expect(
        buffer.isLinearReadPositionValid(region, std::nextafter(3.0, 0.0)),
        "position below newest-exclusive should be valid"
    );
    context.expect(
        !buffer.isLinearReadPositionValid(region, 3.0),
        "newest sample cannot be an interpolation first tap"
    );
    context.expect(
        !buffer.isLinearReadPositionValid(region, 4.0),
        "write-exclusive head cannot be read"
    );
    context.expect(
        !buffer.isLinearReadPositionValid(region, -0.01),
        "negative positions must be rejected"
    );
}

void testRequiredPrehistoryAndCeiling(TestContext& context)
{
    const auto required = [](double rate, std::uint32_t length)
    {
        return CausalReadPlanBuilder::requiredPrehistoryFrames(
            rate,
            length,
            2
        );
    };

    context.expect(required(-0.25, 480) == 601, "reverse includes write-head motion in prehistory");
    context.expect(required(-0.5, 480) == 721, "reverse half-rate prehistory includes render-time aging");
    context.expect(required(1.0, 480) == 2, "forward unity-rate needs margin");
    context.expect(required(2.0, 480) == 481, "forward double-rate retreats anchor");
    context.expect(required(4.0, 480) == 1439, "forward quadruple-rate uses ceil");
    context.expect(
        required(std::numeric_limits<double>::quiet_NaN(), 480) == 0,
        "invalid rate must not produce a prehistory"
    );
    context.expect(
        required(1.0, 0) == 0,
        "zero grain length must be rejected"
    );
    context.expect(
        required(1.0e300, 480) == 0,
        "unrepresentable prehistory must saturate to invalid"
    );
}

void testHistoryAndPositionMonotonicity(TestContext& context)
{
    CircularAudioBuffer buffer;
    context.expect(
        prepareAndFill(buffer),
        "history buffer should prepare and fill"
    );
    const auto writeHead = buffer.getAbsoluteWritePosition();

    CausalEventSnapshot newest;
    context.expect(
        CausalReadPlanBuilder::captureEventSnapshot(
            buffer,
            CausalSourcePolicy::historyOnly,
            writeHead - 1,
            0.0,
            1.0,
            64,
            newest
        ),
        "history event snapshot should be captured"
    );

    const auto newestPlan = CausalReadPlanBuilder::buildAtLaunch(
        buffer,
        newest
    );
    context.expect(newestPlan.isValid(), "newest history plan should be valid");
    context.expect(
        newestPlan.sourceKind == CausalSourceKind::history,
        "history-only policy must select history"
    );

    CausalEventSnapshot oldest;
    context.expect(
        CausalReadPlanBuilder::captureEventSnapshot(
            buffer,
            CausalSourcePolicy::historyOnly,
            writeHead - 1,
            1.0,
            1.0,
            64,
            oldest
        ),
        "oldest history event snapshot should be captured"
    );
    const auto oldestPlan = CausalReadPlanBuilder::buildAtLaunch(
        buffer,
        oldest
    );
    context.expect(oldestPlan.isValid(), "oldest history plan should be valid");
    context.expect(
        oldestPlan.anchor < newestPlan.anchor,
        "increasing Position must increase material age"
    );
}

void testLiveRatesAndDirections(TestContext& context)
{
    CircularAudioBuffer buffer;
    context.expect(
        prepareAndFill(buffer),
        "live buffer should prepare and fill"
    );
    const auto writeHead = buffer.getAbsoluteWritePosition();

    for (const auto rate : { 0.25, 0.5, 1.0, 2.0, 4.0, -0.25, -0.5, -1.0, -2.0, -4.0 })
    {
        CausalEventSnapshot event;
        context.expect(
            CausalReadPlanBuilder::captureEventSnapshot(
                buffer,
                CausalSourcePolicy::causalLiveOnly,
            writeHead - 1,
                0.0,
                rate,
                480,
                event
            ),
            "live event snapshot should be captured"
        );
        const auto plan = CausalReadPlanBuilder::buildAtLaunch(buffer, event);
        context.expect(plan.isValid(), "causal Live trajectory should be valid");
        context.expect(plan.sourceKind == CausalSourceKind::causalLive,
                       "Live-only policy must select the dynamic reader");
        context.expect(plan.historyRegion.isEmpty(),
                       "Live plan must not pin a History SourceRegion");
    }
}

void testPoliciesAndFallback(TestContext& context)
{
    CircularAudioBuffer startup;
    context.expect(
        startup.prepare(makeSpec(), 1.0, 0.5),
        "startup buffer should prepare"
    );

    context.expect(fill(startup, 1), "startup event frame should be written");

    CausalEventSnapshot event;
    context.expect(
        CausalReadPlanBuilder::captureEventSnapshot(
            startup,
            CausalSourcePolicy::historyOnly,
            0,
            0.0,
            1.0,
            64,
            event
        ),
        "empty Event-Time snapshot should still retain policy"
    );
    context.expect(fill(startup, 512), "startup history should become available");

    const auto deferredHistory = CausalReadPlanBuilder::buildAtLaunch(startup, event);
    context.expect(deferredHistory.isValid(),
                   "pending History event should use the now-available capture region");
    context.expect(deferredHistory.sourceKind == CausalSourceKind::history,
                   "History-only policy must remain History");

    event.sourcePolicy = CausalSourcePolicy::historyThenLiveFallback;
    const auto explicitHistoryFirst = CausalReadPlanBuilder::buildAtLaunch(startup, event);
    context.expect(explicitHistoryFirst.isValid(),
                   "explicit History-first policy should select available History");
    context.expect(explicitHistoryFirst.sourceKind == CausalSourceKind::history,
                   "History-first fallback policy prefers valid History");
}

void testGenerationAndWrapAround(TestContext& context)
{
    CircularAudioBuffer buffer;
    context.expect(
        prepareAndFill(buffer, 44100.0, 0.01, 0.005, 441),
        "wrap-around buffer should prepare and fill"
    );
    CausalEventSnapshot event;
    context.expect(
        CausalReadPlanBuilder::captureEventSnapshot(
            buffer,
            CausalSourcePolicy::historyOnly,
            buffer.getAbsoluteWritePosition() - 1,
            0.0,
            1.0,
            3,
            event
        ),
        "wrap-around snapshot should be captured"
    );
    buffer.reset();
    const auto stale = CausalReadPlanBuilder::buildAtLaunch(buffer, event);
    context.expect(
        stale.feasibility == CausalReadFeasibility::generationMismatch,
        "reset must invalidate the Event-Time generation"
    );

    context.expect(fill(buffer, 441), "buffer should refill after reset");
    CausalEventSnapshot fresh;
    context.expect(
        CausalReadPlanBuilder::captureEventSnapshot(
            buffer,
            CausalSourcePolicy::historyOnly,
            buffer.getAbsoluteWritePosition() - 1,
            0.0,
            1.0,
            3,
            fresh
        ),
        "fresh wrapped snapshot should be captured"
    );
    const auto plan = CausalReadPlanBuilder::buildAtLaunch(buffer, fresh);
    context.expect(plan.isValid(), "wrapped history should remain valid");
}

void testLogicalAndPhysicalBoundaries(TestContext& context)
{
    CircularAudioBuffer buffer;
    context.expect(
        prepareAndFill(buffer, 44100.0, 0.02, 0.005, 700),
        "bounded history buffer should prepare and fill"
    );

    const auto logical = buffer.getLogicalFrameRange();
    const auto physical = buffer.getPhysicalFrameRange();
    context.expect(
        logical.oldest >= physical.oldest,
        "logical history must not precede physical history"
    );

    CausalEventSnapshot event;
    context.expect(
        CausalReadPlanBuilder::captureEventSnapshot(
            buffer,
            CausalSourcePolicy::historyOnly,
            buffer.getAbsoluteWritePosition() - 1,
            1.0,
            -1.0,
            3,
            event
        ),
        "logical boundary snapshot should be captured"
    );
    const auto plan = CausalReadPlanBuilder::buildAtLaunch(buffer, event);
    context.expect(plan.isValid(), "logical boundary trajectory should be valid");

    const auto oldWrite = buffer.getAbsoluteWritePosition();
    context.expect(fill(buffer, 700), "buffer should advance beyond the snapshot");
    context.expect(
        buffer.getAbsoluteWritePosition() > oldWrite,
        "write head should advance for physical invalidation"
    );
    const auto retried = CausalReadPlanBuilder::buildAtLaunch(buffer, event);
    context.expect(retried.isValid(),
                   "pre-launch retry captures the current History region");
    context.expect(retried.historyRegion.newestExclusive
                       == buffer.getAbsoluteWritePosition(),
                   "retry must not reuse an Event-Time SourceRegion");
}

void testPlanDeterminismAndSnapshots(TestContext& context)
{
    CircularAudioBuffer buffer;
    context.expect(
        prepareAndFill(buffer),
        "determinism buffer should prepare and fill"
    );

    CausalEventSnapshot event;
    context.expect(
        CausalReadPlanBuilder::captureEventSnapshot(
            buffer,
            CausalSourcePolicy::historyThenLiveFallback,
            buffer.getAbsoluteWritePosition() - 1,
            0.25,
            2.0,
            128,
            event
        ),
        "determinism snapshot should be captured"
    );
    const auto first = CausalReadPlanBuilder::buildAtLaunch(buffer, event);
    const auto second = CausalReadPlanBuilder::buildAtLaunch(buffer, event);
    context.expect(
        first.feasibility == second.feasibility
            && first.sourceKind == second.sourceKind
            && std::bit_cast<std::uint64_t>(first.anchor)
                == std::bit_cast<std::uint64_t>(second.anchor)
            && first.earliestValidWriteHead == second.earliestValidWriteHead,
        "identical buffer and snapshot must produce identical plans"
    );
    context.expect(
        event.positionNormalized == 0.25
            && event.playbackRate == 2.0
            && event.grainLengthFrames == 128,
        "Event-Time parameters must remain unchanged across retries"
    );
}

void testSupportedSampleRates(TestContext& context)
{
    for (const auto rate : { 44100.0, 48000.0, 88200.0, 96000.0 })
    {
        CircularAudioBuffer buffer;
        context.expect(
            prepareAndFill(
                buffer,
                rate,
                0.2,
                0.1,
                static_cast<std::size_t>(rate * 0.1)
            ),
            "supported-rate buffer should prepare and fill"
        );
        CausalEventSnapshot event;
        context.expect(
            CausalReadPlanBuilder::captureEventSnapshot(
                buffer,
                CausalSourcePolicy::causalLiveOnly,
            buffer.getAbsoluteWritePosition() - 1,
                0.5,
                1.0,
                3,
                event
            ),
            "supported sample rate should capture a snapshot"
        );
        const auto plan = CausalReadPlanBuilder::buildAtLaunch(buffer, event);
        context.expect(plan.isValid(),
                       "Live plan should work at every supported sample rate");
        context.expect(plan.sourceKind == CausalSourceKind::causalLive,
                       "Live-only policy remains selected at every sample rate");
    }
}

void testContractValidationAndDelayedSnapshots(TestContext& context)
{
    CircularAudioBuffer buffer;
    context.expect(prepareAndFill(buffer), "contract buffer should prepare");
    const auto write = buffer.getAbsoluteWritePosition();

    CausalEventSnapshot event;
    context.expect(
        !CausalReadPlanBuilder::captureEventSnapshot(
            buffer, static_cast<CausalSourcePolicy>(255), write - 1,
            0.5, 1.0, 32, event, 2),
        "unknown source policy must be rejected");
    context.expect(
        !CausalReadPlanBuilder::captureEventSnapshot(
            buffer, CausalSourcePolicy::causalLiveOnly, write - 1,
            0.5, 1.0, 32, event, 1),
        "margin one must be rejected");
    context.expect(
        !CausalReadPlanBuilder::captureEventSnapshot(
            buffer, CausalSourcePolicy::causalLiveOnly, write,
            0.5, 1.0, 32, event, 2),
        "event at the write-exclusive head must be rejected");
    context.expect(
        CausalReadPlanBuilder::captureEventSnapshot(
            buffer, CausalSourcePolicy::historyOnly, write - 2,
            0.5, 1.0, 32, event, 2),
        "delayed Event-Time must be accepted without retaining a region");

    const auto eventSnapshot = event;
    context.expect(fill(buffer, 37), "buffer should advance before deferred capture");
    const auto launchRegion = buffer.captureSourceRegion();
    const auto plan = CausalReadPlanBuilder::buildAtLaunch(
        buffer, event, launchRegion);
    context.expect(plan.captureWriteExclusive == launchRegion.newestExclusive,
                   "plan records Capture-Time write-exclusive head");
    context.expect(plan.historyRegion.newestExclusive == launchRegion.newestExclusive,
                   "plan uses the Launch-Time history region");
    context.expect(event.eventSample == eventSnapshot.eventSample
                       && std::bit_cast<std::uint64_t>(event.positionNormalized)
                           == std::bit_cast<std::uint64_t>(eventSnapshot.positionNormalized),
                   "deferred capture leaves Event-Time metadata unchanged");
    context.expect(!plan.usedLiveFallback, "history plan must not report fallback");

    CausalEventSnapshot malformed = event;
    malformed.sourcePolicy = static_cast<CausalSourcePolicy>(254);
    context.expect(
        CausalReadPlanBuilder::buildAtLaunch(buffer, malformed).feasibility
            == CausalReadFeasibility::invalidEventSnapshot,
        "unknown policy in a pending event must be invalid");
}

double expectedProductionHistoryAnchor(
    const CircularAudioBuffer::SourceRegion& region,
    double position,
    double rate,
    std::uint32_t frames
)
{
    const auto oldest = static_cast<long double>(region.oldest);
    const auto upperExclusive = static_cast<long double>(region.newestExclusive) - 1.0L;
    const auto travel = std::abs(static_cast<long double>(rate))
        * static_cast<long double>(frames - 1);
    auto freshest = std::nextafter(
        static_cast<double>(upperExclusive),
        -std::numeric_limits<double>::infinity());
    const auto oldestEndpoint = oldest + travel;
    auto endpoint = static_cast<double>(static_cast<long double>(freshest)
        - static_cast<long double>(position)
            * (static_cast<long double>(freshest) - oldestEndpoint));
    if (static_cast<long double>(endpoint) < oldestEndpoint)
        endpoint = std::nextafter(endpoint, std::numeric_limits<double>::infinity());
    auto start = rate < 0.0
        ? static_cast<double>(endpoint)
        : static_cast<double>(static_cast<long double>(endpoint) - travel);
    if (static_cast<long double>(start) < oldest)
        start = std::nextafter(start, std::numeric_limits<double>::infinity());
    return start;
}

void testHistoryGeometryMatchesProduction(TestContext& context)
{
    CircularAudioBuffer buffer;
    context.expect(prepareAndFill(buffer, 48000.0, 1.0, 0.8, 38400),
                   "History geometry fixture should prepare");
    const auto region = buffer.captureSourceRegion();
    constexpr std::array positions { 0.0, 0.25, 0.5, 0.75, 1.0 };
    constexpr std::array rates { 0.5, 1.0, 2.0, -0.5, -1.0, -2.0 };
    constexpr std::array lengths { 3U, 480U, 12000U };

    for (const auto rate : rates)
    {
        for (const auto frames : lengths)
        {
            const auto travel = std::abs(rate) * static_cast<double>(frames - 1);
            const auto oldestEndpoint = static_cast<double>(region.oldest) + travel;
            const auto freshest = std::nextafter(
                static_cast<double>(region.newestExclusive) - 1.0,
                -std::numeric_limits<double>::infinity());
            if (freshest < oldestEndpoint)
                continue;

            for (const auto position : positions)
            {
                CausalEventSnapshot event;
                context.expect(
                    CausalReadPlanBuilder::captureEventSnapshot(
                        buffer, CausalSourcePolicy::historyOnly,
                        buffer.getAbsoluteWritePosition() - 1,
                        position, rate, frames, event),
                    "history snapshot should capture for valid geometry");
                const auto expected = expectedProductionHistoryAnchor(
                    region, position, rate, frames);
                const auto plan = CausalReadPlanBuilder::buildAtLaunch(
                    buffer, event, region);
                const auto productionTrajectoryValid
                    = buffer.isLinearTrajectoryValid(region, expected, rate, frames);
                context.expect(
                    plan.isValid() == productionTrajectoryValid,
                    "plan validity must match production physical trajectory preflight");
                if (!productionTrajectoryValid)
                {
                    context.expect(
                        plan.feasibility == CausalReadFeasibility::physicalRangeInvalid,
                        "predicted physical overwrite must be reported explicitly");
                    continue;
                }
                context.expectNear(plan.anchor, expected, 1.0e-9,
                                   "History anchor must match independent production formula");
                context.expect(
                    buffer.isLinearTrajectoryValid(
                        region, plan.anchor, rate, frames),
                    "both interpolation taps at every trajectory offset must remain valid");
                for (std::uint32_t offset = 0; offset < frames; ++offset)
                {
                    const auto x = CircularAudioBuffer::calculateLinearTrajectoryPosition(
                        plan.anchor, rate, offset);
                    context.expect(buffer.isLinearReadPositionValid(region, x),
                                   "each trajectory sample must retain both taps");
                }
            }
        }
    }

    CircularAudioBuffer shortBuffer;
    context.expect(shortBuffer.prepare(makeSpec(), 0.1, 0.1),
                   "short logical history fixture should prepare");
    context.expect(fill(shortBuffer, 3),
                   "short logical history fixture should contain three frames");
    const auto tooShortRegion = shortBuffer.captureSourceRegion();
    CausalEventSnapshot tooLarge;
    context.expect(
        CausalReadPlanBuilder::captureEventSnapshot(
            CausalSourcePolicy::historyOnly, tooShortRegion.generation,
            tooShortRegion.newestExclusive - 1, 0.5, 2.0, 3, tooLarge),
        "event metadata is independent from current History size");
    const auto tooShortPlan = CausalReadPlanBuilder::buildAtLaunch(
        shortBuffer, tooLarge, tooShortRegion);
    context.expect(
        tooShortPlan.feasibility == CausalReadFeasibility::insufficientHistory,
        "too-short Capture-Time region must not produce an anchor");
}

void testFeasibilityDistinguishesGrowingAndImpossibleRanges(TestContext& context)
{
    CircularAudioBuffer history;
    context.expect(history.prepare(makeSpec(), 0.1, 0.1),
                   "short logical history should prepare");
    context.expect(fill(history, 3), "history should begin with short valid region");
    const auto initialRegion = history.captureSourceRegion();

    CausalEventSnapshot growingHistory;
    context.expect(CausalReadPlanBuilder::captureEventSnapshot(
                       CausalSourcePolicy::historyOnly,
                       initialRegion.generation,
                       2, 0.5, 2.0, 3, growingHistory),
                   "History event snapshot should be valid");
    auto plan = CausalReadPlanBuilder::buildAtLaunch(
        history, growingHistory, initialRegion);
    context.expect(plan.feasibility == CausalReadFeasibility::insufficientHistory,
                   "currently short but eventually feasible History remains temporary");

    context.expect(fill(history, 100), "History range should grow");
    const auto grownRegion = history.captureSourceRegion();
    plan = CausalReadPlanBuilder::buildAtLaunch(history, growingHistory, grownRegion);
    context.expect(plan.feasibility == CausalReadFeasibility::valid,
                   "same Event-Time History trajectory should become valid after capture grows");

    CausalEventSnapshot impossibleHistory;
    context.expect(CausalReadPlanBuilder::captureEventSnapshot(
                       CausalSourcePolicy::historyOnly,
                       grownRegion.generation,
                       grownRegion.newestExclusive - 1,
                       0.5, 4.0, 10000, impossibleHistory),
                   "oversized History request metadata should remain structurally valid");
    plan = CausalReadPlanBuilder::buildAtLaunch(
        history, impossibleHistory, grownRegion);
    context.expect(plan.feasibility
                       == CausalReadFeasibility::trajectoryExceedsLogicalCapacity,
                   "History trajectory larger than maximum logical range is permanent");

    CircularAudioBuffer live;
    context.expect(live.prepare(makeSpec(), 0.1, 0.1),
                   "Live source should prepare");
    context.expect(fill(live, 3), "Live source should start with short prehistory");
    const auto liveGeneration = live.getGeneration();
    CausalEventSnapshot reverseLive;
    context.expect(CausalReadPlanBuilder::captureEventSnapshot(
                       CausalSourcePolicy::causalLiveOnly,
                       liveGeneration, 2, 0.5, -1.0, 3, reverseLive),
                   "Reverse Live snapshot should be structurally valid");
    plan = CausalReadPlanBuilder::buildAtLaunch(
        live, reverseLive, live.captureSourceRegion());
    context.expect(plan.feasibility == CausalReadFeasibility::insufficientHistory,
                   "Reverse Live may await enough causal prehistory");

    context.expect(fill(live, 100), "Live prehistory should accumulate");
    plan = CausalReadPlanBuilder::buildAtLaunch(
        live, reverseLive, live.captureSourceRegion());
    context.expect(plan.feasibility == CausalReadFeasibility::valid,
                   "Reverse Live should become valid once prehistory is sufficient");

    CausalEventSnapshot impossibleLive;
    context.expect(CausalReadPlanBuilder::captureEventSnapshot(
                       CausalSourcePolicy::causalLiveOnly,
                       liveGeneration,
                       live.getAbsoluteWritePosition() - 1,
                       0.5, 4.0, 10000, impossibleLive),
                   "oversized Live request metadata should be valid");
    plan = CausalReadPlanBuilder::buildAtLaunch(
        live, impossibleLive, live.captureSourceRegion());
    context.expect(plan.feasibility
                       == CausalReadFeasibility::trajectoryExceedsLogicalCapacity,
                   "Live trajectory outside maximum logical range is permanent");
}
}

int main()
{
    TestContext context;
    testWriteExclusiveAndInterpolation(context);
    testRequiredPrehistoryAndCeiling(context);
    testHistoryAndPositionMonotonicity(context);
    testLiveRatesAndDirections(context);
    testPoliciesAndFallback(context);
    testGenerationAndWrapAround(context);
    testLogicalAndPhysicalBoundaries(context);
    testPlanDeterminismAndSnapshots(context);
    testSupportedSampleRates(context);
    testContractValidationAndDelayedSnapshots(context);
    testHistoryGeometryMatchesProduction(context);
    testFeasibilityDistinguishesGrowingAndImpossibleRanges(context);

    if (context.getFailures() != 0)
    {
        std::cerr << context.getFailures() << " test assertion(s) failed\n";
        return 1;
    }

    std::cout << "CausalReadPlan tests passed\n";
    return 0;
}
