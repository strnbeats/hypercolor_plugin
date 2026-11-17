#include "core/ProcessSpec.h"
#include "dsp/granular/CircularAudioBuffer.h"
#include "dsp/granular/GrainScheduler.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <span>
#include <vector>

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
void operator delete[](void* memory) noexcept { ::operator delete(memory); }
void operator delete(void* memory, std::size_t) noexcept
{
    ::operator delete(memory);
}
void operator delete[](void* memory, std::size_t) noexcept
{
    ::operator delete[](memory);
}

namespace
{
using hypercolor::core::ProcessSpec;
using hypercolor::dsp::granular::CircularAudioBuffer;
using hypercolor::dsp::granular::GrainPool;
using hypercolor::dsp::granular::GrainRenderAccess;
using hypercolor::dsp::granular::GrainScheduler;
using hypercolor::dsp::granular::GrainSchedulerParameters;
using hypercolor::dsp::granular::GrainSchedulerStepResult;

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

    [[nodiscard]] int failureCount() const noexcept { return failures; }

private:
    int failures = 0;
};

ProcessSpec makeSpec(
    double sampleRate = 48000.0,
    std::uint32_t maximumBlockSize = 64
)
{
    return { sampleRate, maximumBlockSize, 1 };
}

bool writeSilence(CircularAudioBuffer& source) noexcept
{
    const std::array<float, 1> frame { 0.0f };
    return source.writeFrame(frame);
}

bool prepare(
    CircularAudioBuffer& source,
    const ProcessSpec& spec,
    double logicalDurationSeconds = 4.0
)
{
    return source.prepare(
        spec,
        CircularAudioBuffer::productMaximumDurationSeconds,
        logicalDurationSeconds
    );
}

GrainSchedulerParameters makeParameters(
    double density,
    double grainSizeMs
)
{
    GrainSchedulerParameters parameters;
    parameters.densityGrainsPerSecond = density;
    parameters.grainSizeMilliseconds = grainSizeMs;
    return parameters;
}

GrainSchedulerStepResult processSample(
    GrainScheduler& scheduler,
    CircularAudioBuffer& source,
    GrainPool& pool,
    const GrainSchedulerParameters& parameters
)
{
    if (!writeSilence(source))
        return GrainSchedulerStepResult::sourceUnavailable;
    return scheduler.processSample(parameters, source, pool);
}

std::size_t durationOfFirstActiveGrain(const GrainPool& pool)
{
    auto duration = std::size_t { 0 };
    const_cast<GrainPool&>(pool).forEachActive(
        [&](GrainRenderAccess& access) noexcept
        {
            if (duration == 0)
                duration = access.getDurationFrames();
        }
    );
    return duration;
}

void test100MsDensity20(TestContext& context)
{
    const auto spec = makeSpec();
    CircularAudioBuffer source;
    GrainScheduler scheduler;
    GrainPool pool;
    context.expect(prepare(source, spec), "100 ms source prepare");
    context.expect(scheduler.prepare(spec), "100 ms scheduler prepare");
    const auto parameters = makeParameters(20.0, 100.0);

    auto sawPending = false;
    auto started = std::size_t { 0 };
    auto firstStart = std::size_t { 0 };
    for (std::size_t sample = 0; sample < 11000; ++sample)
    {
        const auto result = processSample(
            scheduler,
            source,
            pool,
            sample < 2400 ? parameters : makeParameters(0.0, 100.0)
        );
        sawPending = sawPending
            || scheduler.getPendingEventCount() > 0
            || result == GrainSchedulerStepResult::insufficientHistory;
        if (result == GrainSchedulerStepResult::started)
        {
            if (started == 0)
                firstStart = sample;
            ++started;
        }
    }

    context.expect(sawPending, "100 ms event should enter pending state");
    context.expect(started == 1, "100 ms event should start exactly once");
    context.expect(
        firstStart >= 4800 && firstStart < 5000,
        "100 ms pending event should start at first valid history sample"
    );
    context.expect(
        scheduler.getPendingEventCount() == 0,
        "100 ms pending queue should drain after confirmation"
    );

    CircularAudioBuffer continuousSource;
    GrainScheduler continuousScheduler;
    GrainPool continuousPool;
    context.expect(
        prepare(continuousSource, spec),
        "continuous 100 ms source prepare"
    );
    context.expect(
        continuousScheduler.prepare(spec),
        "continuous 100 ms scheduler prepare"
    );
    auto continuousFirstStart = std::size_t { 0 };
    auto continuousStarted = std::size_t { 0 };
    for (std::size_t sample = 0; sample < 11000; ++sample)
    {
        const auto result = processSample(
                continuousScheduler,
                continuousSource,
                continuousPool,
                parameters
            );
        if (result == GrainSchedulerStepResult::started)
        {
            if (continuousStarted++ == 0)
                continuousFirstStart = sample;
        }
    }
    context.expect(
        continuousFirstStart >= 4750 && continuousFirstStart < 4900,
        "pending event should confirm while Density remains active"
    );
    context.expect(
        continuousPool.getOccupiedCount() == continuousStarted,
        "one pending event must not create duplicate pool entries"
    );
}

void test500MsDensity20(TestContext& context)
{
    const auto spec = makeSpec();
    CircularAudioBuffer source;
    GrainScheduler scheduler;
    GrainPool pool;
    context.expect(prepare(source, spec), "500 ms source prepare");
    context.expect(scheduler.prepare(spec), "500 ms scheduler prepare");
    const auto parameters = makeParameters(20.0, 500.0);
    auto started = std::size_t { 0 };
    auto firstStart = std::size_t { 0 };

    for (std::size_t sample = 0; sample < 30000; ++sample)
    {
        const auto result = processSample(
            scheduler,
            source,
            pool,
            sample < 2400 ? parameters : makeParameters(0.0, 500.0)
        );
        if (result == GrainSchedulerStepResult::started)
        {
            if (started == 0)
                firstStart = sample;
            ++started;
        }
    }

    context.expect(started == 1, "500 ms event should start exactly once");
    context.expect(
        firstStart >= 24000 && firstStart < 24500,
        "500 ms event should wait until 500 ms of history exists"
    );
    context.expect(
        scheduler.getPendingOverflowCount() == 0,
        "single 500 ms pending event should not overflow"
    );
}

void test120DensityOverflow(TestContext& context)
{
    const auto spec = makeSpec();
    CircularAudioBuffer source;
    GrainScheduler scheduler;
    GrainPool pool;
    context.expect(prepare(source, spec), "overflow source prepare");
    context.expect(scheduler.prepare(spec), "overflow scheduler prepare");
    const auto parameters = makeParameters(120.0, 500.0);

    for (std::size_t sample = 0; sample < 8000; ++sample)
        static_cast<void>(processSample(scheduler, source, pool, parameters));

    context.expect(
        scheduler.getPendingEventCount() == GrainScheduler::pendingEventCapacity,
        "pending queue must remain bounded at capacity"
    );
    context.expect(
        scheduler.getPendingOverflowCount() > 0,
        "overflow must be counted instead of silently hidden"
    );
}

void testExpiration(TestContext& context)
{
    const auto spec = makeSpec();
    CircularAudioBuffer source;
    GrainScheduler scheduler;
    GrainPool pool;
    context.expect(
        prepare(source, spec, 0.2),
        "short logical source prepare"
    );
    context.expect(scheduler.prepare(spec), "expiration scheduler prepare");
    const auto parameters = makeParameters(120.0, 500.0);

    for (std::size_t sample = 0; sample < 2400; ++sample)
        static_cast<void>(processSample(scheduler, source, pool, parameters));

    const auto pendingBefore = scheduler.getPendingEventCount();
    for (std::size_t sample = 0; sample < 4 * 48000 + 100; ++sample)
        static_cast<void>(processSample(
            scheduler,
            source,
            pool,
            makeParameters(0.0, 500.0)
        ));

    context.expect(pendingBefore > 0, "expiration scenario should queue events");
    context.expect(
        scheduler.getPendingExpiredCount() > 0,
        "stale pending events must expire deterministically"
    );
    context.expect(
        scheduler.getPendingEventCount() <= GrainScheduler::pendingEventCapacity,
        "expiration must preserve bounded capacity"
    );
}

void testParameterChangeLatchesPendingEvent(TestContext& context)
{
    const auto spec = makeSpec();
    CircularAudioBuffer source;
    GrainScheduler scheduler;
    GrainPool pool;
    context.expect(prepare(source, spec), "latching source prepare");
    context.expect(scheduler.prepare(spec), "latching scheduler prepare");

    const auto original = makeParameters(20.0, 500.0);
    for (std::size_t sample = 0; sample < 2400; ++sample)
        static_cast<void>(processSample(scheduler, source, pool, original));

    const auto changed = makeParameters(0.0, 10.0);
    auto started = false;
    for (std::size_t sample = 2400; sample < 30000; ++sample)
    {
        if (processSample(scheduler, source, pool, changed)
            == GrainSchedulerStepResult::started)
        {
            started = true;
            break;
        }
    }

    context.expect(started, "pending event should survive parameter change");
    context.expect(
        durationOfFirstActiveGrain(pool) == 24000,
        "pending event must retain its original Grain Size"
    );
    context.expect(
        scheduler.getPendingEventCount() == 0,
        "confirmed pending event must be removed exactly once"
    );
}

void testNoAudioPathAllocations(TestContext& context)
{
    const auto spec = makeSpec();
    CircularAudioBuffer source;
    GrainScheduler scheduler;
    GrainPool pool;
    context.expect(prepare(source, spec), "allocation source prepare");
    context.expect(scheduler.prepare(spec), "allocation scheduler prepare");
    const auto parameters = makeParameters(120.0, 100.0);

    allocationProbe::count.store(0, std::memory_order_relaxed);
    allocationProbe::enabled.store(true, std::memory_order_release);
    for (std::size_t sample = 0; sample < 20000; ++sample)
        static_cast<void>(processSample(scheduler, source, pool, parameters));
    allocationProbe::enabled.store(false, std::memory_order_release);

    context.expect(
        allocationProbe::count.load(std::memory_order_relaxed) == 0,
        "pending scheduler must not allocate after prepare"
    );
}

struct TemporalRun
{
    std::vector<std::uint64_t> starts;
    std::uint64_t overflow = 0;
    std::uint64_t expired = 0;
    std::uint64_t stale = 0;
    std::size_t maximumPending = 0;
    std::size_t firstStartedDuration = 0;
};

TemporalRun runTemporalScenario(
    double density,
    double grainSizeMs,
    std::size_t totalSamples,
    double logicalDurationSeconds = 4.0,
    std::size_t restoreLogicalDurationAt = std::numeric_limits<std::size_t>::max(),
    std::size_t densityActiveSamples = std::numeric_limits<std::size_t>::max()
)
{
    const auto spec = makeSpec();
    CircularAudioBuffer source;
    GrainScheduler scheduler;
    GrainPool pool;
    TemporalRun run;

    if (!prepare(source, spec, logicalDurationSeconds)
        || !scheduler.prepare(spec))
        return run;

    for (std::size_t sample = 0; sample < totalSamples; ++sample)
    {
        if (sample == restoreLogicalDurationAt)
            static_cast<void>(source.setLogicalDurationSeconds(4.0));

        if (!writeSilence(source))
            break;

        const auto parameters = makeParameters(
            sample < densityActiveSamples ? density : 0.0,
            grainSizeMs
        );
        if (scheduler.processSample(parameters, source, pool)
            == GrainSchedulerStepResult::started)
        {
            run.starts.push_back(sample);
            if (run.firstStartedDuration == 0)
                run.firstStartedDuration = durationOfFirstActiveGrain(pool);
        }

        run.maximumPending = std::max(
            run.maximumPending,
            scheduler.getPendingEventCount()
        );

        pool.reset();
    }

    run.overflow = scheduler.getPendingOverflowCount();
    run.expired = scheduler.getPendingExpiredCount();
    run.stale = scheduler.getPendingStaleCount();
    return run;
}

void expectNoBurst(
    TestContext& context,
    const TemporalRun& run,
    const char* message
)
{
    constexpr std::uint64_t minimumSpacing = 48000 / 120;
    for (std::size_t index = 1; index < run.starts.size(); ++index)
    {
        context.expect(
            run.starts[index] > run.starts[index - 1]
                && run.starts[index] - run.starts[index - 1]
                    >= minimumSpacing,
            message
        );
    }
}

void testTemporalSchedulerIntegrity(TestContext& context)
{
    const auto longSlow = runTemporalScenario(20.0, 500.0, 80000);
    const auto longFast = runTemporalScenario(120.0, 500.0, 80000);
    const auto shortFast = runTemporalScenario(120.0, 100.0, 30000);

    context.expect(longSlow.starts.size() > 1,
                   "500 ms / 20 Density should recover and continue");
    context.expect(longFast.starts.size() > 1,
                   "500 ms / 120 Density should recover and continue");
    context.expect(shortFast.starts.size() > 1,
                   "100 ms / 120 Density should recover and continue");
    context.expect(longFast.overflow > 0,
                   "500 ms / 120 should report bounded pending overflow");
    context.expect(longFast.stale > 0,
                   "recovery should coalesce stale pending events");
    context.expect(
        longFast.maximumPending <= GrainScheduler::pendingEventCapacity,
        "pending queue must remain bounded during recovery"
    );
    expectNoBurst(context, longSlow,
                  "500 ms / 20 recovery must not burst");
    expectNoBurst(context, longFast,
                  "500 ms / 120 recovery must not burst");
    expectNoBurst(context, shortFast,
                  "100 ms / 120 recovery must not burst");

    const auto prolonged = runTemporalScenario(
        20.0,
        500.0,
        4 * 48000 + 4000,
        0.2,
        std::numeric_limits<std::size_t>::max(),
        2400
    );
    context.expect(prolonged.expired > 0,
                   "long history starvation must expire old events");
    context.expect(prolonged.starts.empty(),
                   "insufficient history must not create phantom starts");

    const auto recovered = runTemporalScenario(
        20.0,
        500.0,
        80000,
        0.2,
        15000
    );
    context.expect(recovered.starts.size() > 1,
                   "pending queue should resume after history recovery");
    expectNoBurst(context, recovered,
                  "history recovery must preserve minimum start spacing");
}

void testTemporalParameterTransitions(TestContext& context)
{
    const auto spec = makeSpec();
    CircularAudioBuffer source;
    GrainScheduler scheduler;
    GrainPool pool;
    context.expect(prepare(source, spec, 0.2),
                   "transition source prepare");
    context.expect(scheduler.prepare(spec),
                   "transition scheduler prepare");

    const auto oldParameters = makeParameters(120.0, 500.0);
    const auto newParameters = makeParameters(20.0, 100.0);
    auto starts = std::vector<std::uint64_t> {};
    auto firstDuration = std::size_t { 0 };

    for (std::size_t sample = 0; sample < 80000; ++sample)
    {
        if (sample == 15000)
            static_cast<void>(source.setLogicalDurationSeconds(4.0));

        if (!writeSilence(source))
            break;

        const auto parameters = sample < 12000
            ? oldParameters
            : newParameters;
        if (scheduler.processSample(parameters, source, pool)
            == GrainSchedulerStepResult::started)
        {
            starts.push_back(sample);
            if (firstDuration == 0)
                firstDuration = durationOfFirstActiveGrain(pool);
        }
        pool.reset();
    }

    context.expect(!starts.empty(),
                   "500 ms / 120 to 100 ms / 20 must resume");
    context.expect(firstDuration == 24000,
                   "older pending event must retain its 500 ms parameters");
    for (std::size_t index = 1; index < starts.size(); ++index)
        context.expect(
            starts[index] - starts[index - 1] >= 48000 / 120,
            "parameter transition must not create a burst"
        );

    source.reset();
    context.expect(prepare(source, spec, 0.2),
                   "reverse transition source prepare");
    scheduler.reset();
    const auto shortParameters = makeParameters(120.0, 100.0);
    const auto longParameters = makeParameters(20.0, 500.0);
    starts.clear();
    firstDuration = 0;

    for (std::size_t sample = 0; sample < 80000; ++sample)
    {
        if (sample == 6000)
            static_cast<void>(source.setLogicalDurationSeconds(4.0));

        if (!writeSilence(source))
            break;

        const auto parameters = sample < 3000
            ? shortParameters
            : longParameters;
        if (scheduler.processSample(parameters, source, pool)
            == GrainSchedulerStepResult::started)
        {
            starts.push_back(sample);
            if (firstDuration == 0)
                firstDuration = durationOfFirstActiveGrain(pool);
        }
        pool.reset();
    }

    context.expect(!starts.empty(),
                   "100 ms / 120 to 500 ms / 20 must resume");
    context.expect(firstDuration == 4800,
                   "older pending event must retain its 100 ms parameters");
    for (std::size_t index = 1; index < starts.size(); ++index)
        context.expect(
            starts[index] - starts[index - 1] >= 48000 / 120,
            "reverse parameter transition must not create a burst"
        );
}
}

int main()
{
    TestContext context;
    test100MsDensity20(context);
    test500MsDensity20(context);
    test120DensityOverflow(context);
    testExpiration(context);
    testParameterChangeLatchesPendingEvent(context);
    testNoAudioPathAllocations(context);
    testTemporalSchedulerIntegrity(context);
    testTemporalParameterTransitions(context);

    if (context.failureCount() != 0)
        return 1;

    std::cout << "All GrainScheduler pending tests passed.\n";
    return 0;
}
