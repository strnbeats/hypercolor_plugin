#include "core/ProcessSpec.h"
#include "dsp/granular/CircularAudioBuffer.h"
#include "dsp/granular/GrainPool.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <type_traits>
#include <utility>

namespace
{
using hypercolor::core::ProcessSpec;
using hypercolor::dsp::granular::CircularAudioBuffer;
using hypercolor::dsp::granular::Grain;
using hypercolor::dsp::granular::GrainInit;
using hypercolor::dsp::granular::GrainPool;
using hypercolor::dsp::granular::GrainRenderAccess;
using hypercolor::dsp::granular::GrainStartResult;
using hypercolor::dsp::granular::GrainWindow;

constexpr double sampleRate = 44100.0;

static_assert(noexcept(std::declval<GrainPool&>().reclaimFinished()));
static_assert(noexcept(std::declval<GrainPool&>().reset()));
static_assert(noexcept(std::declval<const GrainPool&>().getActiveCount()));
static_assert(!std::is_constructible_v<GrainRenderAccess, Grain&>);
static_assert(!std::is_copy_constructible_v<GrainRenderAccess>);
static_assert(!std::is_move_constructible_v<GrainRenderAccess>);
static_assert(!std::is_convertible_v<GrainRenderAccess&, Grain&>);

template <typename Access>
concept HasInitialise = requires(
    Access& access,
    const GrainInit& init,
    const CircularAudioBuffer& source
)
{
    access.initialise(init, source);
};

template <typename Access>
concept HasReset = requires(Access& access)
{
    access.reset();
};

static_assert(!HasInitialise<GrainRenderAccess>);
static_assert(!HasReset<GrainRenderAccess>);

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

    [[nodiscard]] int getFailures() const noexcept
    {
        return failures;
    }

private:
    int failures = 0;
};

double durationForFrames(std::size_t frames)
{
    return static_cast<double>(frames) / sampleRate;
}

bool prepareAndFill(CircularAudioBuffer& buffer, std::size_t frames)
{
    if (!buffer.prepare(
            ProcessSpec { sampleRate, 64, 1 },
            durationForFrames(frames),
            durationForFrames(frames)))
    {
        return false;
    }

    for (std::size_t frameIndex = 0; frameIndex < frames; ++frameIndex)
    {
        const std::array frame { static_cast<float>(frameIndex) };

        if (!buffer.writeFrame(frame))
            return false;
    }

    return true;
}

GrainInit makeInit(
    const CircularAudioBuffer& buffer,
    double startPosition = 20.0,
    double playbackRate = 0.0,
    std::uint32_t durationFrames = 3
)
{
    return {
        buffer.captureSourceRegion(),
        startPosition,
        playbackRate,
        durationFrames,
        GrainWindow::hann
    };
}

void testCapacityDropAndNoStealing(TestContext& context)
{
    CircularAudioBuffer buffer;
    context.expect(prepareAndFill(buffer, 128), "pool source should prepare");

    GrainPool pool;
    const auto init = makeInit(buffer);

    for (std::size_t index = 0; index < GrainPool::maximumGrains; ++index)
    {
        context.expect(
            pool.tryStart(init, buffer) == GrainStartResult::started,
            "first 64 grains should start"
        );
    }

    context.expect(pool.getActiveCount() == 64, "active count should reach 64");
    context.expect(pool.getOccupiedCount() == 64, "occupied count should reach 64");
    context.expect(pool.isFull(), "pool should report full");

    const auto replacement = makeInit(buffer, 40.0, 0.0, 3);
    context.expect(
        pool.tryStart(replacement, buffer) == GrainStartResult::poolFull,
        "65th grain should be dropped"
    );

    std::size_t originalCount = 0;
    pool.forEachActive(
        [&](GrainRenderAccess& grain) noexcept
        {
            if (grain.getReadPosition() == 20.0)
                ++originalCount;
        }
    );
    context.expect(
        originalCount == GrainPool::maximumGrains,
        "full-pool attempt must not steal or mutate existing grains"
    );
}

void testInvalidInitDoesNotOccupySlot(TestContext& context)
{
    CircularAudioBuffer buffer;
    context.expect(prepareAndFill(buffer, 128), "invalid-init source should prepare");

    GrainPool pool;
    auto invalid = makeInit(buffer);
    invalid.durationFrames = 2;
    context.expect(
        pool.tryStart(invalid, buffer) == GrainStartResult::invalidGrain,
        "invalid GrainInit should be rejected"
    );
    context.expect(pool.getActiveCount() == 0, "invalid grain must not be active");
    context.expect(pool.getOccupiedCount() == 0, "invalid grain must not occupy slot");
}

void testFinishedOccupancyAndReclaim(TestContext& context)
{
    CircularAudioBuffer buffer;
    context.expect(prepareAndFill(buffer, 128), "reclaim source should prepare");

    GrainPool pool;
    const auto init = makeInit(buffer);

    for (std::size_t index = 0; index < GrainPool::maximumGrains; ++index)
        context.expect(pool.tryStart(init, buffer) == GrainStartResult::started, "fill pool");

    bool finishedOne = false;
    pool.forEachActive(
        [&](GrainRenderAccess& grain) noexcept
        {
            if (!finishedOne)
            {
                grain.markFinished();
                finishedOne = true;
            }
        }
    );

    context.expect(pool.getActiveCount() == 63, "one grain should be finished");
    context.expect(
        pool.getOccupiedCount() == 64,
        "finished slot should remain occupied before reclaim"
    );
    context.expect(
        pool.tryStart(makeInit(buffer, 40.0), buffer)
            == GrainStartResult::poolFull,
        "finished slot must not be reused before reclaim"
    );

    pool.reclaimFinished();
    context.expect(pool.getOccupiedCount() == 63, "reclaim should free finished slot");
    context.expect(
        pool.tryStart(makeInit(buffer, 40.0), buffer)
            == GrainStartResult::started,
        "reclaimed slot should be reusable on a later start phase"
    );
    context.expect(pool.getActiveCount() == 64, "replacement should restore active count");
}

void testDeterministicSlotSelection(TestContext& context)
{
    CircularAudioBuffer buffer;
    context.expect(prepareAndFill(buffer, 128), "selection source should prepare");

    GrainPool pool;
    context.expect(
        pool.tryStart(makeInit(buffer, 20.0), buffer) == GrainStartResult::started,
        "first deterministic grain should start"
    );
    context.expect(
        pool.tryStart(makeInit(buffer, 30.0), buffer) == GrainStartResult::started,
        "second deterministic grain should start"
    );

    bool markedFirst = false;
    pool.forEachActive(
        [&](GrainRenderAccess& grain) noexcept
        {
            if (!markedFirst)
            {
                grain.markFinished();
                markedFirst = true;
            }
        }
    );
    pool.reclaimFinished();
    context.expect(
        pool.tryStart(makeInit(buffer, 40.0), buffer) == GrainStartResult::started,
        "replacement should start in first free slot"
    );

    std::array<double, 2> positions {};
    std::size_t count = 0;
    pool.forEachActive(
        [&](GrainRenderAccess& grain) noexcept
        {
            if (count < positions.size())
                positions[count++] = grain.getReadPosition();
        }
    );

    context.expect(count == 2, "two deterministic grains should remain");
    context.expect(
        positions[0] == 40.0 && positions[1] == 30.0,
        "linear search should deterministically reuse the lowest free slot"
    );
}

void testCountsMultipleDirectionsAndReset(TestContext& context)
{
    CircularAudioBuffer buffer;
    context.expect(prepareAndFill(buffer, 128), "multi-grain source should prepare");

    GrainPool pool;
    context.expect(
        pool.tryStart(makeInit(buffer, 20.0, 1.0, 4), buffer)
            == GrainStartResult::started,
        "forward grain should start"
    );
    context.expect(
        pool.tryStart(makeInit(buffer, 20.0, -1.0, 4), buffer)
            == GrainStartResult::started,
        "reverse grain should start"
    );
    context.expect(pool.getActiveCount() == 2, "two grains should be active");
    context.expect(pool.getOccupiedCount() == 2, "two slots should be occupied");

    bool sawForward = false;
    bool sawReverse = false;
    pool.forEachActive(
        [&](GrainRenderAccess& grain) noexcept
        {
            sawForward = sawForward || grain.getPlaybackRate() > 0.0;
            sawReverse = sawReverse || grain.getPlaybackRate() < 0.0;
            grain.advanceAfterRender();
        }
    );
    context.expect(sawForward && sawReverse, "forward and reverse should coexist");
    context.expect(pool.getActiveCount() == 2, "one advance should keep both active");

    pool.reset();
    context.expect(pool.getActiveCount() == 0, "reset should clear active count");
    context.expect(pool.getOccupiedCount() == 0, "reset should clear occupied count");
    context.expect(!pool.isFull(), "reset pool should not be full");
}

void testTraversalGuard(TestContext& context)
{
    CircularAudioBuffer buffer;
    context.expect(prepareAndFill(buffer, 128), "guard source should prepare");

    GrainPool pool;
    const auto init = makeInit(buffer);
    context.expect(
        pool.tryStart(init, buffer) == GrainStartResult::started,
        "guard grain should start"
    );

    auto nestedResult = GrainStartResult::started;
    bool observedTraversal = false;
    pool.forEachActive(
        [&](GrainRenderAccess&) noexcept
        {
            observedTraversal = pool.isTraversalInProgress();
            nestedResult = pool.tryStart(init, buffer);
            pool.reclaimFinished();
            pool.reset();
        }
    );

    context.expect(observedTraversal, "pool should expose active traversal state");
    context.expect(
        nestedResult == GrainStartResult::traversalInProgress,
        "structural start during traversal should be rejected"
    );
    context.expect(
        pool.getOccupiedCount() == 1,
        "structural methods during traversal must not mutate pool"
    );
    context.expect(
        !pool.isTraversalInProgress(),
        "traversal guard should clear after visitor loop"
    );
}

void testRenderAccessDataAndProgress(TestContext& context)
{
    CircularAudioBuffer buffer;
    context.expect(prepareAndFill(buffer, 128), "render-access source should prepare");

    GrainPool pool;
    const auto init = makeInit(buffer, 20.0, 0.5, 4);
    context.expect(
        pool.tryStart(init, buffer) == GrainStartResult::started,
        "render-access grain should start"
    );

    pool.forEachActive(
        [&](GrainRenderAccess& access) noexcept
        {
            context.expect(access.isActive(), "render access should expose active state");
            context.expect(
                access.getState() == hypercolor::dsp::granular::GrainState::active,
                "render access should expose GrainState"
            );
            context.expect(
                access.getSourceRegion().generation == init.sourceRegion.generation,
                "render access should expose source region"
            );
            context.expect(access.getReadPosition() == 20.0, "render position should match");
            context.expect(access.getPlaybackRate() == 0.5, "render rate should match");
            context.expect(access.getDurationFrames() == 4, "render duration should match");
            context.expect(access.getRenderedFrames() == 0, "render count should start at zero");
            context.expect(access.getWindow() == GrainWindow::hann, "window should match");
            context.expect(access.getWindowCoefficient() == 0.0f, "first Hann value should be zero");
            access.advanceAfterRender();
        }
    );

    pool.forEachActive(
        [&](GrainRenderAccess& access) noexcept
        {
            context.expect(access.getReadPosition() == 20.5, "advance should update position");
            context.expect(access.getRenderedFrames() == 1, "advance should update count");
            access.markFinished();
        }
    );

    context.expect(pool.getActiveCount() == 0, "markFinished should remove active state");
    context.expect(pool.getOccupiedCount() == 1, "finished grain should remain occupied");
}

void testNestedTraversalGuard(TestContext& context)
{
    CircularAudioBuffer buffer;
    context.expect(prepareAndFill(buffer, 128), "nested source should prepare");

    GrainPool pool;
    context.expect(
        pool.tryStart(makeInit(buffer), buffer) == GrainStartResult::started,
        "nested traversal grain should start"
    );

    std::size_t outerVisits = 0;
    std::size_t nestedVisits = 0;
    pool.forEachActive(
        [&](GrainRenderAccess&) noexcept
        {
            ++outerVisits;
            pool.forEachActive(
                [&](GrainRenderAccess&) noexcept
                {
                    ++nestedVisits;
                }
            );
            context.expect(
                pool.isTraversalInProgress(),
                "outer traversal should remain active after rejected nesting"
            );
        }
    );

    context.expect(outerVisits == 1, "outer traversal should visit the grain once");
    context.expect(nestedVisits == 0, "nested traversal should be rejected");
    context.expect(
        !pool.isTraversalInProgress(),
        "guard should clear after outer traversal"
    );
}

void testCompleteLifecycle(TestContext& context)
{
    CircularAudioBuffer buffer;
    context.expect(prepareAndFill(buffer, 128), "lifecycle source should prepare");

    GrainPool pool;
    context.expect(
        pool.tryStart(makeInit(buffer, 20.0, 1.0, 3), buffer)
            == GrainStartResult::started,
        "lifecycle grain should start"
    );

    for (std::uint32_t frame = 0; frame < 3; ++frame)
    {
        pool.forEachActive(
            [](GrainRenderAccess& access) noexcept
            {
                access.advanceAfterRender();
            }
        );
    }

    context.expect(pool.getActiveCount() == 0, "grain should finish after N frames");
    context.expect(pool.getOccupiedCount() == 1, "finished slot should remain occupied");
    pool.reclaimFinished();
    context.expect(pool.getOccupiedCount() == 0, "reclaim should release finished slot");
    context.expect(
        pool.tryStart(makeInit(buffer, 40.0, 0.0, 3), buffer)
            == GrainStartResult::started,
        "reclaimed slot should be reusable"
    );
}

void testMultipleFinishesAndReclaim(TestContext& context)
{
    CircularAudioBuffer buffer;
    context.expect(prepareAndFill(buffer, 128), "multi-finish source should prepare");

    GrainPool pool;
    for (std::size_t index = 0; index < 3; ++index)
    {
        context.expect(
            pool.tryStart(makeInit(buffer, 20.0 + static_cast<double>(index)), buffer)
                == GrainStartResult::started,
            "multi-finish grain should start"
        );
    }

    pool.forEachActive(
        [](GrainRenderAccess& access) noexcept
        {
            access.markFinished();
        }
    );

    context.expect(pool.getActiveCount() == 0, "all grains should finish in one traversal");
    context.expect(pool.getOccupiedCount() == 3, "finished grains should remain occupied");
    pool.reclaimFinished();
    context.expect(pool.getOccupiedCount() == 0, "reclaim should release all finished grains");
}
}

int main()
{
    TestContext context;

    testCapacityDropAndNoStealing(context);
    testInvalidInitDoesNotOccupySlot(context);
    testFinishedOccupancyAndReclaim(context);
    testDeterministicSlotSelection(context);
    testCountsMultipleDirectionsAndReset(context);
    testTraversalGuard(context);
    testRenderAccessDataAndProgress(context);
    testNestedTraversalGuard(context);
    testCompleteLifecycle(context);
    testMultipleFinishesAndReclaim(context);

    if (context.getFailures() == 0)
    {
        std::cout << "All GrainPool tests passed.\n";
        return 0;
    }

    std::cerr << context.getFailures() << " test(s) failed.\n";
    return 1;
}
