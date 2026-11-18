#include "core/ProcessSpec.h"
#include "dsp/granular/CausalReadPlan.h"
#include "dsp/granular/CircularAudioBuffer.h"
#include "dsp/granular/GrainPool.h"
#include "dsp/granular/GrainRenderer.h"
#include "dsp/granular/GrainScheduler.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <new>
#include <span>

namespace allocationProbe
{
std::atomic<bool> enabled { false };
std::atomic<std::size_t> count { 0 };
}

void* operator new(std::size_t size)
{
    if (allocationProbe::enabled.load(std::memory_order_relaxed))
        allocationProbe::count.fetch_add(1, std::memory_order_relaxed);
    if (auto* memory = std::malloc(size))
        return memory;
    throw std::bad_alloc {};
}

void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { ::operator delete(memory); }
void operator delete[](void* memory, std::size_t) noexcept { ::operator delete(memory); }

namespace
{
using namespace hypercolor::dsp::granular;
using hypercolor::core::ProcessSpec;

constexpr double sampleRate = 48000.0;
constexpr std::uint32_t margin = 2;

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

    void expectNear(double actual, double expected, double tolerance,
                    const char* message)
    {
        expect(std::abs(actual - expected) <= tolerance, message);
    }

    [[nodiscard]] int failureCount() const noexcept { return failures; }

private:
    int failures = 0;
};

bool prepareBuffer(
    CircularAudioBuffer& source,
    std::size_t capacity,
    std::size_t logical,
    std::size_t initialFrames
)
{
    const auto spec = ProcessSpec { sampleRate, 512, 1 };
    const auto capacitySeconds
        = (static_cast<double>(capacity) - 0.25) / sampleRate;
    const auto logicalSeconds
        = (static_cast<double>(logical) - 0.25) / sampleRate;
    if (!source.prepare(spec, capacitySeconds, logicalSeconds))
        return false;

    for (std::size_t i = 0; i < initialFrames; ++i)
    {
        const std::array frame {
            static_cast<float>(source.getAbsoluteWritePosition()) * 0.001f
        };
        if (!source.writeFrame(frame))
            return false;
    }
    return true;
}

bool writeRampFrame(CircularAudioBuffer& source)
{
    const std::array frame {
        static_cast<float>(source.getAbsoluteWritePosition()) * 0.001f
    };
    return source.writeFrame(frame);
}

CausalEventSnapshot makeEvent(
    const CircularAudioBuffer& source,
    double position,
    double rate,
    std::uint32_t frames,
    CausalSourcePolicy policy = CausalSourcePolicy::causalLiveOnly
)
{
    CausalEventSnapshot event;
    const auto write = source.getAbsoluteWritePosition();
    const auto captured = CausalReadPlanBuilder::captureEventSnapshot(
        policy,
        source.getGeneration(),
        write - 1,
        position,
        rate,
        frames,
        event
    );
    if (!captured)
        event = {};
    return event;
}

bool startLiveGrain(
    GrainPool& pool,
    const CircularAudioBuffer& source,
    const CausalReadPlan& plan,
    GrainWindow window = GrainWindow::triangle
)
{
    GrainInit init;
    init.startPosition = plan.anchor;
    init.playbackRate = plan.playbackRate;
    init.durationFrames = plan.grainLengthFrames;
    init.window = window;
    init.sourceKind = plan.sourceKind;
    init.sourceGeneration = plan.generation;
    init.causalMarginSamples = plan.causalMarginSamples;
    return pool.tryStart(init, source) == GrainStartResult::started;
}

void renderTrajectoryCase(
    TestContext& context,
    double rate,
    double position,
    std::uint32_t frames
)
{
    CircularAudioBuffer source;
    context.expect(prepareBuffer(source, 4096, 2048, 2048),
                   "trajectory source prepares with recorded prehistory");
    const auto event = makeEvent(source, position, rate, frames);
    const auto plan = CausalReadPlanBuilder::buildAtLaunch(
        source, event, source.captureSourceRegion());
    context.expect(plan.isValid(), "feasible live trajectory has a valid plan");
    if (!plan.isValid())
        return;
    context.expect(plan.sourceKind == CausalSourceKind::causalLive,
                   "plan selects causal live source");
    context.expect(plan.historyRegion.isEmpty(),
                   "live plan does not freeze a history region");

    GrainPool pool;
    GrainRenderer renderer;
    context.expect(startLiveGrain(pool, source, plan),
                   "live plan initializes a live Grain");
    const auto targetFrame = std::min<std::uint32_t>(2, frames - 2);
    for (std::uint32_t frame = 0; frame <= targetFrame; ++frame)
    {
        if (frame > 0)
            context.expect(writeRampFrame(source), "new live sample is written");
        std::array<float, 1> output {};
        const auto rendered = renderer.renderSample(source, pool, output);
        context.expect(rendered == GrainRendererResult::success,
                       "Renderer reads live trajectory without degradation");
        if (frame == targetFrame)
        {
            const auto x = CircularAudioBuffer::calculateLinearTrajectoryPosition(
                plan.anchor, rate, frame);
            const auto phase = static_cast<double>(frame)
                / static_cast<double>(frames - 1);
            const auto triangle = 1.0 - std::abs(2.0 * phase - 1.0);
            context.expectNear(output[0], x * 0.001 * triangle, 2.0e-5,
                               "Renderer output uses actual interpolated live samples");
        }
    }

    pool.forEachActive([&](GrainRenderAccess& grain) noexcept
    {
        context.expect(grain.getSourceKind() == CausalSourceKind::causalLive,
                       "active Grain remains bound to Live source");
    });
}

void testForwardReversePositionAndSizes(TestContext& context)
{
    for (const auto rate : { 0.5, 1.0, 2.0, -1.0, -3.0 })
    {
        renderTrajectoryCase(context, rate, 0.0, 5);
        renderTrajectoryCase(context, rate, 1.0, 5);
    }
    renderTrajectoryCase(context, 0.5, 0.0, 3);
    renderTrajectoryCase(context, 2.0, 0.25, 128);
}

void testInsufficientReverseHistory(TestContext& context)
{
    CircularAudioBuffer source;
    context.expect(prepareBuffer(source, 128, 128, 12),
                   "short reverse history prepares");
    const auto event = makeEvent(source, 0.0, -1.0, 8);
    const auto plan = CausalReadPlanBuilder::buildAtLaunch(
        source, event, source.captureSourceRegion());
    context.expect(
        plan.feasibility == CausalReadFeasibility::insufficientHistory,
        "reverse Live is rejected until the full past trajectory exists");
}

void testCausalMarginAndInterpolationTaps(TestContext& context)
{
    CircularAudioBuffer source;
    context.expect(prepareBuffer(source, 64, 64, 100),
                   "wrapped source prepares and advances beyond capacity");
    float value = 0.0f;
    context.expect(source.tryReadCausalLive(
                       source.getGeneration(), 0, 98.0, margin, value),
                   "position at M=2 boundary has both available taps");
    context.expectNear(value, 0.098, 2.0e-6,
                       "boundary interpolation uses expected wrapped values");
    context.expect(!source.tryReadCausalLive(
                       source.getGeneration(), 0, 98.0001, margin, value),
                   "position beyond causal margin is rejected");
    context.expect(!source.tryReadCausalLive(
                       source.getGeneration(), 0,
                       static_cast<double>(source.getAbsoluteWritePosition()),
                       margin, value),
                   "future write-head read is rejected");
    const auto logical = source.getLogicalFrameRange();
    context.expect(source.tryReadCausalLive(
                       source.getGeneration(), 0,
                       static_cast<double>(logical.oldest), margin, value),
                   "tap pair beginning at logical oldest is allowed");
    context.expect(!source.tryReadCausalLive(
                       source.getGeneration(), 0,
                       static_cast<double>(logical.oldest) - 0.01,
                       margin, value),
                   "tap before logical oldest is rejected");
}

void testOverwriteAndGenerationEndGrain(TestContext& context)
{
    CircularAudioBuffer source;
    context.expect(prepareBuffer(source, 64, 64, 64),
                   "overwrite source prepares");
    const auto event = makeEvent(source, 0.0, -1.0, 4);
    const auto plan = CausalReadPlanBuilder::buildAtLaunch(
        source, event, source.captureSourceRegion());
    context.expect(plan.isValid(), "short reverse Live plan is feasible");

    GrainPool pool;
    GrainRenderer renderer;
    context.expect(startLiveGrain(pool, source, plan),
                   "live grain starts before overwrite");
    auto remainedLiveBeforeOverwrite = false;
    pool.forEachActive([&](GrainRenderAccess& grain) noexcept
    {
        remainedLiveBeforeOverwrite
            = grain.getSourceKind() == CausalSourceKind::causalLive;
    });
    context.expect(remainedLiveBeforeOverwrite,
                   "grain source is fixed to Live at launch");
    for (int i = 0; i < 70; ++i)
        context.expect(writeRampFrame(source), "source advances through wraparound");

    std::array<float, 1> output { 1.0f };
    context.expect(renderer.renderSample(source, pool, output)
                       == GrainRendererResult::degraded,
                   "physical overwrite ends the Live grain");
    context.expect(output[0] == 0.0f,
                   "failed Live read emits no stale or substituted sample");
    context.expect(pool.getOccupiedCount() == 1,
                   "failed Live grain stays finished until normal reclaim");
    pool.reclaimFinished();
    context.expect(pool.getOccupiedCount() == 0,
                   "failed Live grain is reclaimed without source substitution");

    CircularAudioBuffer resetSource;
    context.expect(prepareBuffer(resetSource, 64, 64, 64),
                   "reset source prepares");
    const auto resetEvent = makeEvent(resetSource, 0.0, 1.0, 4);
    const auto resetPlan = CausalReadPlanBuilder::buildAtLaunch(
        resetSource, resetEvent, resetSource.captureSourceRegion());
    GrainPool resetPool;
    context.expect(startLiveGrain(resetPool, resetSource, resetPlan),
                   "Live grain starts before generation reset");
    resetSource.reset();
    output = { 1.0f };
    context.expect(renderer.renderSample(resetSource, resetPool, output)
                       == GrainRendererResult::degraded,
                   "generation reset invalidates Live reads");
    context.expect(resetPool.getOccupiedCount() == 1,
                   "generation failure leaves Grain ready for normal reclaim");
    resetPool.reclaimFinished();
    context.expect(resetPool.getOccupiedCount() == 0,
                   "generation-invalidated Grain is reclaimed");
}

void testSchedulerSelectsLiveAndNoRealtimeAllocation(TestContext& context)
{
    CircularAudioBuffer source;
    const auto spec = ProcessSpec { sampleRate, 64, 1 };
    context.expect(source.prepare(spec, 1.0, 0.5),
                   "Scheduler integration source prepares");
    GrainScheduler scheduler;
    context.expect(scheduler.prepare(spec), "Scheduler prepares");
    GrainPool pool;
    GrainRenderer renderer;
    GrainSchedulerParameters parameters;
    parameters.densityGrainsPerSecond = 120.0;
    parameters.grainSizeMilliseconds = 10.0;
    parameters.positionNormalized = 0.0;
    parameters.sourcePolicy
        = CausalSourcePolicy::livePreferredHistoryFallback;

    auto startedLive = false;
    allocationProbe::count.store(0, std::memory_order_relaxed);
    allocationProbe::enabled.store(true, std::memory_order_relaxed);
    for (int sample = 0; sample < 500; ++sample)
    {
        if (!writeRampFrame(source))
            continue;
        const auto result = scheduler.processSample(parameters, source, pool);
        if (result == GrainSchedulerStepResult::started && !startedLive)
        {
            pool.forEachActive([&](GrainRenderAccess& grain) noexcept
            {
                startedLive = grain.getSourceKind() == CausalSourceKind::causalLive;
            });
        }
        std::array<float, 1> output {};
        static_cast<void>(renderer.renderSample(source, pool, output));
        pool.reclaimFinished();
    }
    allocationProbe::enabled.store(false, std::memory_order_relaxed);

    context.expect(startedLive,
                   "production source policy launches fresh audio through Live");
    context.expect(allocationProbe::count.load(std::memory_order_relaxed) == 0,
                   "Live scheduling and rendering do not allocate");
}
}

int main()
{
    TestContext context;
    testForwardReversePositionAndSizes(context);
    testInsufficientReverseHistory(context);
    testCausalMarginAndInterpolationTaps(context);
    testOverwriteAndGenerationEndGrain(context);
    testSchedulerSelectsLiveAndNoRealtimeAllocation(context);

    if (context.failureCount() != 0)
        return 1;
    std::cout << "Granular Live Reader tests passed\n";
    return 0;
}
