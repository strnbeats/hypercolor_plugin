#include "core/ProcessSpec.h"
#include "dsp/granular/CircularAudioBuffer.h"
#include "dsp/granular/Grain.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <utility>

namespace
{
using hypercolor::core::ProcessSpec;
using hypercolor::dsp::granular::CircularAudioBuffer;
using hypercolor::dsp::granular::Grain;
using hypercolor::dsp::granular::GrainInit;
using hypercolor::dsp::granular::GrainInitResult;
using hypercolor::dsp::granular::GrainState;
using hypercolor::dsp::granular::GrainWindow;

constexpr double sampleRate = 44100.0;

static_assert(noexcept(std::declval<Grain&>().advanceAfterRender()));
static_assert(noexcept(std::declval<Grain&>().markFinished()));
static_assert(noexcept(std::declval<Grain&>().reset()));
static_assert(noexcept(std::declval<const Grain&>().getWindowCoefficient()));

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

    void expectNear(
        double actual,
        double expected,
        double tolerance,
        const char* message
    )
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

double durationForFrames(std::size_t frames)
{
    return static_cast<double>(frames) / sampleRate;
}

bool prepareAndFill(
    CircularAudioBuffer& buffer,
    std::size_t capacityFrames,
    std::size_t logicalFrames = 0
)
{
    const auto logical = logicalFrames == 0
        ? capacityFrames
        : logicalFrames;

    if (!buffer.prepare(
            ProcessSpec { sampleRate, 64, 1 },
            durationForFrames(capacityFrames),
            durationForFrames(logical)))
    {
        return false;
    }

    for (std::size_t frameIndex = 0;
         frameIndex < capacityFrames;
         ++frameIndex)
    {
        const std::array frame { static_cast<float>(frameIndex) };

        if (!buffer.writeFrame(frame))
            return false;
    }

    return true;
}

GrainInit makeInit(
    const CircularAudioBuffer& buffer,
    double startPosition = 10.0,
    double playbackRate = 1.0,
    std::uint32_t durationFrames = 4
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

void testDefaultAndInitialisation(TestContext& context)
{
    Grain grain;
    context.expect(
        grain.getState() == GrainState::inactive,
        "default grain should be inactive"
    );
    context.expect(!grain.isActive(), "default grain should not be active");
    context.expect(!grain.isFinished(), "default grain should not be finished");
    context.expectNear(grain.getReadPosition(), 0.0, 0.0, "default position");
    context.expect(grain.getDurationFrames() == 0, "default duration");

    CircularAudioBuffer buffer;
    context.expect(prepareAndFill(buffer, 32), "source buffer should prepare");
    const auto init = makeInit(buffer, 10.0, 1.0, 4);

    context.expect(
        grain.initialise(init, buffer) == GrainInitResult::success,
        "valid forward grain should initialise"
    );
    context.expect(grain.isActive(), "initialised grain should be active");
    context.expectNear(grain.getReadPosition(), 10.0, 0.0, "start position");
    context.expectNear(grain.getPlaybackRate(), 1.0, 0.0, "playback rate");
    context.expect(grain.getDurationFrames() == 4, "duration should latch");
    context.expect(grain.getRenderedFrames() == 0, "render count starts at zero");
    context.expect(grain.getWindow() == GrainWindow::hann, "window should latch");
    context.expect(
        grain.getSourceRegion().generation == init.sourceRegion.generation,
        "source generation should latch"
    );
}

void testForwardReverseAndStationary(TestContext& context)
{
    CircularAudioBuffer buffer;
    context.expect(prepareAndFill(buffer, 32), "trajectory buffer should prepare");

    for (const auto rate : { 1.0, -1.0, 0.0 })
    {
        Grain grain;
        const auto init = makeInit(buffer, 10.0, rate, 4);
        context.expect(
            grain.initialise(init, buffer) == GrainInitResult::success,
            "forward/reverse/stationary grain should initialise"
        );
        context.expectNear(
            grain.getPlaybackRate(),
            rate,
            0.0,
            "requested rate should be retained"
        );
    }
}

void testInvalidInitialisation(TestContext& context)
{
    CircularAudioBuffer buffer;
    context.expect(prepareAndFill(buffer, 32), "validation buffer should prepare");

    auto expectInvalidParameters = [&](GrainInit init, const char* message)
    {
        Grain grain;
        context.expect(
            grain.initialise(init, buffer)
                == GrainInitResult::invalidParameters,
            message
        );
        context.expect(
            grain.getState() == GrainState::inactive,
            "parameter failure must remain transactional"
        );
    };

    auto init = makeInit(buffer);
    init.startPosition = std::numeric_limits<double>::quiet_NaN();
    expectInvalidParameters(init, "NaN start position should be rejected");

    init = makeInit(buffer);
    init.playbackRate = std::numeric_limits<double>::infinity();
    expectInvalidParameters(init, "infinite playback rate should be rejected");

    init = makeInit(buffer);
    init.window = static_cast<GrainWindow>(255);
    expectInvalidParameters(init, "unknown window should be rejected");

    for (const auto duration : { 0u, 1u, 2u })
    {
        init = makeInit(buffer);
        init.durationFrames = duration;
        expectInvalidParameters(init, "duration below three should be rejected");
    }

    Grain emptySourceGrain;
    init = makeInit(buffer);
    init.sourceRegion = {};
    context.expect(
        emptySourceGrain.initialise(init, buffer)
            == GrainInitResult::invalidSourceTrajectory,
        "empty SourceRegion should be rejected"
    );
    context.expect(
        emptySourceGrain.getState() == GrainState::inactive,
        "source failure must remain transactional"
    );

    Grain invalidTrajectoryGrain;
    init = makeInit(buffer, 30.0, 1.0, 4);
    context.expect(
        invalidTrajectoryGrain.initialise(init, buffer)
            == GrainInitResult::invalidSourceTrajectory,
        "trajectory beyond fixed source upper bound should be rejected"
    );

    const auto staleRegion = buffer.captureSourceRegion();
    buffer.reset();
    Grain staleGenerationGrain;
    init = { staleRegion, 10.0, 1.0, 4, GrainWindow::hann };
    context.expect(
        staleGenerationGrain.initialise(init, buffer)
            == GrainInitResult::invalidSourceTrajectory,
        "stale source generation should be rejected"
    );
}

void testExactTrajectory(
    TestContext& context,
    CircularAudioBuffer& buffer,
    double playbackRate,
    const std::array<double, 4>& expectedPositions,
    const char* initialisationMessage,
    const char* positionMessage
)
{
    Grain grain;
    const auto init = makeInit(buffer, 10.0, playbackRate, 4);
    context.expect(
        grain.initialise(init, buffer) == GrainInitResult::success,
        initialisationMessage
    );

    for (std::size_t frame = 0; frame < expectedPositions.size(); ++frame)
    {
        context.expect(grain.isActive(), "grain should remain active before frame advance");
        context.expectNear(
            grain.getReadPosition(),
            expectedPositions[frame],
            1.0e-12,
            positionMessage
        );
        grain.advanceAfterRender();
    }

    context.expect(grain.isFinished(), "grain should finish after exactly N frames");
    context.expect(grain.getRenderedFrames() == 4, "render count should equal N");
    context.expectNear(
        grain.getReadPosition(),
        expectedPositions.back(),
        1.0e-12,
        "grain must retain p(N - 1) after finishing"
    );

    grain.advanceAfterRender();
    context.expect(grain.getRenderedFrames() == 4, "finished grain must not advance count");
    context.expectNear(
        grain.getReadPosition(),
        expectedPositions.back(),
        1.0e-12,
        "finished grain must not advance position"
    );
}

void testExactPositionsAndLifecycle(TestContext& context)
{
    CircularAudioBuffer buffer;
    context.expect(prepareAndFill(buffer, 32), "position buffer should prepare");

    testExactTrajectory(
        context,
        buffer,
        1.0,
        { 10.0, 11.0, 12.0, 13.0 },
        "rate 1.0 grain should initialise",
        "rate 1.0 position should follow p(k)"
    );
    testExactTrajectory(
        context,
        buffer,
        0.5,
        { 10.0, 10.5, 11.0, 11.5 },
        "rate 0.5 grain should initialise",
        "rate 0.5 position should follow p(k)"
    );
    testExactTrajectory(
        context,
        buffer,
        -1.0,
        { 10.0, 9.0, 8.0, 7.0 },
        "rate -1.0 grain should initialise",
        "rate -1.0 position should follow p(k)"
    );
}

void testHannWindow(TestContext& context)
{
    CircularAudioBuffer buffer;
    context.expect(prepareAndFill(buffer, 32), "window buffer should prepare");

    Grain threeFrameGrain;
    context.expect(
        threeFrameGrain.initialise(
            makeInit(buffer, 10.0, 0.0, 3),
            buffer
        ) == GrainInitResult::success,
        "three-frame Hann grain should initialise"
    );
    context.expect(
        threeFrameGrain.getWindowCoefficient() == 0.0f,
        "N=3 first coefficient should be exactly zero"
    );
    threeFrameGrain.advanceAfterRender();
    context.expectNear(
        threeFrameGrain.getWindowCoefficient(),
        1.0,
        1.0e-6,
        "N=3 middle coefficient should be one"
    );
    threeFrameGrain.advanceAfterRender();
    context.expect(
        threeFrameGrain.getWindowCoefficient() == 0.0f,
        "N=3 last coefficient should be exactly zero"
    );

    Grain fourFrameGrain;
    context.expect(
        fourFrameGrain.initialise(
            makeInit(buffer, 10.0, 0.0, 4),
            buffer
        ) == GrainInitResult::success,
        "four-frame Hann grain should initialise"
    );
    constexpr std::array expected { 0.0, 0.75, 0.75, 0.0 };

    for (const auto coefficient : expected)
    {
        context.expectNear(
            fourFrameGrain.getWindowCoefficient(),
            coefficient,
            1.0e-6,
            "N=4 Hann coefficient should match"
        );
        fourFrameGrain.advanceAfterRender();
    }

    context.expect(
        fourFrameGrain.getWindowCoefficient() == 0.0f,
        "finished grain coefficient should be zero"
    );
}

void testResetLogicalDurationAndSourceInvalidation(TestContext& context)
{
    CircularAudioBuffer buffer;
    context.expect(
        prepareAndFill(buffer, 32, 16),
        "logical-duration source should prepare"
    );

    Grain grain;
    const auto init = makeInit(buffer, 20.0, 1.0, 4);
    context.expect(
        grain.initialise(init, buffer) == GrainInitResult::success,
        "grain should initialise before logical change"
    );
    const auto latchedRegion = grain.getSourceRegion();

    context.expect(
        buffer.setLogicalDurationSeconds(durationForFrames(8)),
        "logical duration should shrink"
    );
    context.expect(
        grain.getSourceRegion().oldest == latchedRegion.oldest
            && grain.getSourceRegion().newestExclusive
                == latchedRegion.newestExclusive,
        "logical change must not alter latched SourceRegion"
    );
    context.expect(
        buffer.isSourceRegionCurrent(grain.getSourceRegion()),
        "latched SourceRegion should remain current after logical change"
    );

    buffer.reset();
    float sample = 0.0f;
    context.expect(
        !buffer.tryReadLinear(
            grain.getSourceRegion(),
            0,
            grain.getReadPosition(),
            sample
        ),
        "buffer reset should invalidate the grain source"
    );
    grain.markFinished();
    context.expect(grain.isFinished(), "invalid source should allow safe finish");

    grain.reset();
    context.expect(
        grain.getState() == GrainState::inactive,
        "reset should return grain to inactive"
    );
    context.expect(grain.getSourceRegion().isEmpty(), "reset should clear source region");
    context.expect(grain.getDurationFrames() == 0, "reset should clear duration");
    context.expect(grain.getRenderedFrames() == 0, "reset should clear render count");
    context.expect(grain.getWindowCoefficient() == 0.0f, "inactive window should be zero");
}
}

int main()
{
    TestContext context;

    testDefaultAndInitialisation(context);
    testForwardReverseAndStationary(context);
    testInvalidInitialisation(context);
    testExactPositionsAndLifecycle(context);
    testHannWindow(context);
    testResetLogicalDurationAndSourceInvalidation(context);

    if (context.getFailures() == 0)
    {
        std::cout << "All Grain tests passed.\n";
        return 0;
    }

    std::cerr << context.getFailures() << " test(s) failed.\n";
    return 1;
}
