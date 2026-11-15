#include "core/ProcessSpec.h"
#include "dsp/granular/CircularAudioBuffer.h"
#include "dsp/granular/GrainRenderer.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <utility>

namespace
{
using hypercolor::core::ProcessSpec;
using hypercolor::dsp::granular::CircularAudioBuffer;
using hypercolor::dsp::granular::GrainInit;
using hypercolor::dsp::granular::GrainPool;
using hypercolor::dsp::granular::GrainRenderAccess;
using hypercolor::dsp::granular::GrainRenderer;
using hypercolor::dsp::granular::GrainRendererResult;
using hypercolor::dsp::granular::GrainStartResult;
using hypercolor::dsp::granular::GrainWindow;

constexpr double sampleRate = 44100.0;
constexpr std::size_t defaultCapacityFrames = 128;

static_assert(noexcept(
    std::declval<const GrainRenderer&>().renderSample(
        std::declval<const CircularAudioBuffer&>(),
        std::declval<GrainPool&>(),
        std::declval<std::span<float>>()
    )
));

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

bool prepareBuffer(
    CircularAudioBuffer& buffer,
    std::uint32_t numChannels,
    std::size_t capacityFrames = defaultCapacityFrames
)
{
    return buffer.prepare(
        ProcessSpec { sampleRate, 64, numChannels },
        durationForFrames(capacityFrames),
        durationForFrames(capacityFrames)
    );
}

bool fillMonoRamp(
    CircularAudioBuffer& buffer,
    std::size_t frames = defaultCapacityFrames,
    float offset = 0.0f
)
{
    if (!prepareBuffer(buffer, 1, frames))
        return false;

    for (std::size_t frame = 0; frame < frames; ++frame)
    {
        const std::array input {
            offset + static_cast<float>(frame)
        };

        if (!buffer.writeFrame(input))
            return false;
    }

    return true;
}

bool fillStereoRamp(
    CircularAudioBuffer& buffer,
    std::size_t frames = defaultCapacityFrames
)
{
    if (!prepareBuffer(buffer, 2, frames))
        return false;

    for (std::size_t frame = 0; frame < frames; ++frame)
    {
        const auto index = static_cast<float>(frame);
        const std::array input { index, 100.0f + 2.0f * index };

        if (!buffer.writeFrame(input))
            return false;
    }

    return true;
}

bool fillStereoConstant(
    CircularAudioBuffer& buffer,
    float left,
    float right,
    std::size_t frames = defaultCapacityFrames
)
{
    if (!prepareBuffer(buffer, 2, frames))
        return false;

    for (std::size_t frame = 0; frame < frames; ++frame)
    {
        const std::array input { left, right };

        if (!buffer.writeFrame(input))
            return false;
    }

    return true;
}

bool writeFollowUpFrame(CircularAudioBuffer& buffer)
{
    if (buffer.getNumChannels() == 2)
    {
        const std::array input { 0.0f, 0.0f };
        return buffer.writeFrame(input);
    }

    const std::array input { 0.0f };
    return buffer.writeFrame(input);
}

GrainInit makeInit(
    const CircularAudioBuffer& source,
    double startPosition,
    double playbackRate,
    std::uint32_t durationFrames
)
{
    return {
        source.captureSourceRegion(),
        startPosition,
        playbackRate,
        durationFrames,
        GrainWindow::hann
    };
}

bool startGrain(
    GrainPool& pool,
    const CircularAudioBuffer& source,
    double startPosition,
    double playbackRate,
    std::uint32_t durationFrames
)
{
    return pool.tryStart(
        makeInit(source, startPosition, playbackRate, durationFrames),
        source
    ) == GrainStartResult::started;
}

struct ActiveSnapshot
{
    double readPosition = 0.0;
    std::uint32_t renderedFrames = 0;
    std::uint32_t durationFrames = 0;
    bool found = false;
};

ActiveSnapshot captureFirstActive(GrainPool& pool)
{
    ActiveSnapshot snapshot;
    pool.forEachActive(
        [&](GrainRenderAccess& access) noexcept
        {
            if (snapshot.found)
                return;

            snapshot.readPosition = access.getReadPosition();
            snapshot.renderedFrames = access.getRenderedFrames();
            snapshot.durationFrames = access.getDurationFrames();
            snapshot.found = true;
        }
    );
    return snapshot;
}

void testNoActiveGrains(TestContext& context)
{
    CircularAudioBuffer source;
    context.expect(fillMonoRamp(source), "empty renderer source should prepare");

    GrainPool pool;
    const GrainRenderer renderer;
    std::array output { 99.0f };
    context.expect(
        renderer.renderSample(source, pool, output)
            == GrainRendererResult::success,
        "empty pool should render successfully"
    );
    context.expect(output[0] == 0.0f, "empty pool should produce zero wet output");
    context.expect(pool.getOccupiedCount() == 0, "empty render should not mutate pool");
}

void testMonoFractionalLifecycle(TestContext& context)
{
    CircularAudioBuffer source;
    context.expect(fillMonoRamp(source), "mono lifecycle source should prepare");

    GrainPool pool;
    context.expect(
        startGrain(pool, source, 10.5, 0.0, 3),
        "mono fractional grain should start"
    );

    const GrainRenderer renderer;
    std::array output { -1.0f };
    context.expect(
        renderer.renderSample(source, pool, output)
            == GrainRendererResult::success,
        "first mono frame should render"
    );
    context.expect(output[0] == 0.0f, "first Hann frame should be exactly zero");

    auto snapshot = captureFirstActive(pool);
    context.expect(snapshot.found, "grain should remain active after first frame");
    context.expect(snapshot.renderedFrames == 1, "first frame should advance exactly once");
    context.expectNear(
        snapshot.readPosition,
        10.5,
        0.0,
        "stationary fractional grain should retain its position"
    );

    context.expect(writeFollowUpFrame(source), "mono middle follow-up write should succeed");
    context.expect(
        renderer.renderSample(source, pool, output)
            == GrainRendererResult::success,
        "middle mono frame should render"
    );
    context.expectNear(
        output[0],
        10.5,
        1.0e-6,
        "fractional interpolation should be multiplied by the middle Hann value"
    );
    snapshot = captureFirstActive(pool);
    context.expect(snapshot.renderedFrames == 2, "middle frame should advance exactly once");

    context.expect(writeFollowUpFrame(source), "mono final follow-up write should succeed");
    context.expect(
        renderer.renderSample(source, pool, output)
            == GrainRendererResult::success,
        "final mono frame should render"
    );
    context.expect(output[0] == 0.0f, "last Hann frame should be exactly zero");
    context.expect(pool.getActiveCount() == 0, "grain should finish after exactly three frames");
    context.expect(pool.getOccupiedCount() == 1, "Renderer must not reclaim finished slots");

    pool.reclaimFinished();
    context.expect(pool.getOccupiedCount() == 0, "explicit reclaim should release finished slot");
}

void testStereoFractionalRead(TestContext& context)
{
    CircularAudioBuffer source;
    context.expect(fillStereoRamp(source), "stereo source should prepare");

    GrainPool pool;
    context.expect(
        startGrain(pool, source, 10.5, 0.0, 3),
        "stereo fractional grain should start"
    );

    const GrainRenderer renderer;
    std::array output { -1.0f, -1.0f };
    context.expect(
        renderer.renderSample(source, pool, output)
            == GrainRendererResult::success,
        "stereo first frame should render"
    );
    context.expect(writeFollowUpFrame(source), "stereo follow-up write should succeed");
    context.expect(
        renderer.renderSample(source, pool, output)
            == GrainRendererResult::success,
        "stereo middle frame should render"
    );
    context.expectNear(output[0], 10.5, 1.0e-6, "left channel interpolation should match");
    context.expectNear(output[1], 121.0, 1.0e-6, "right channel interpolation should match");
}

std::array<float, 5> renderFiveFrameGrain(
    TestContext& context,
    double startPosition,
    double playbackRate
)
{
    CircularAudioBuffer source;
    context.expect(fillMonoRamp(source), "direction source should prepare");

    GrainPool pool;
    context.expect(
        startGrain(pool, source, startPosition, playbackRate, 5),
        "direction grain should start"
    );

    const GrainRenderer renderer;
    std::array<float, 5> result {};

    for (std::size_t frame = 0; frame < result.size(); ++frame)
    {
        if (frame > 0)
        {
            context.expect(
                writeFollowUpFrame(source),
                "direction follow-up write should succeed"
            );
        }

        std::array output { -1.0f };
        context.expect(
            renderer.renderSample(source, pool, output)
                == GrainRendererResult::success,
            "direction frame should render"
        );
        result[frame] = output[0];
    }

    context.expect(pool.getActiveCount() == 0, "five-frame grain should finish exactly");
    return result;
}

void testForwardReverseAndHann(TestContext& context)
{
    const auto forward = renderFiveFrameGrain(context, 10.0, 1.0);
    const auto reverse = renderFiveFrameGrain(context, 14.0, -1.0);
    constexpr std::array hann { 0.0, 0.5, 1.0, 0.5, 0.0 };

    for (std::size_t frame = 0; frame < hann.size(); ++frame)
    {
        const auto forwardPosition = 10.0 + static_cast<double>(frame);
        const auto reversePosition = 14.0 - static_cast<double>(frame);
        context.expectNear(
            forward[frame],
            forwardPosition * hann[frame],
            1.0e-6,
            "forward render should use the expected position and Hann value"
        );
        context.expectNear(
            reverse[frame],
            reversePosition * hann[frame],
            1.0e-6,
            "reverse render should use the expected position and Hann value"
        );
    }
}

void testSummingWithoutNormalisation(TestContext& context)
{
    CircularAudioBuffer source;
    context.expect(fillMonoRamp(source), "sum source should prepare");

    GrainPool pool;
    context.expect(startGrain(pool, source, 10.0, 0.0, 3), "N=3 sum grain should start");
    context.expect(startGrain(pool, source, 10.0, 0.0, 5), "N=5 sum grain should start");

    const GrainRenderer renderer;
    std::array output { -1.0f };
    context.expect(
        renderer.renderSample(source, pool, output)
            == GrainRendererResult::success,
        "sum first frame should render"
    );
    context.expect(writeFollowUpFrame(source), "sum follow-up write should succeed");
    context.expect(
        renderer.renderSample(source, pool, output)
            == GrainRendererResult::success,
        "sum middle frame should render"
    );
    context.expectNear(
        output[0],
        15.0,
        1.0e-6,
        "voices with Hann values 1.0 and 0.5 should sum without normalisation"
    );
}

void testMaximumVoiceCount(TestContext& context)
{
    CircularAudioBuffer source;
    context.expect(fillMonoRamp(source), "maximum voice source should prepare");

    GrainPool pool;

    for (std::size_t voice = 0; voice < GrainPool::maximumGrains; ++voice)
    {
        context.expect(
            startGrain(pool, source, 10.0, 0.0, 3),
            "all 64 renderer grains should start"
        );
    }

    const GrainRenderer renderer;
    std::array output { -1.0f };
    context.expect(
        renderer.renderSample(source, pool, output)
            == GrainRendererResult::success,
        "64 first frames should render"
    );
    context.expect(writeFollowUpFrame(source), "64-voice follow-up write should succeed");
    context.expect(
        renderer.renderSample(source, pool, output)
            == GrainRendererResult::success,
        "64 middle frames should render"
    );
    context.expectNear(output[0], 640.0, 1.0e-5, "64 voices should sum without scaling");
    context.expect(pool.getActiveCount() == 64, "all voices should remain active after two frames");
}

void testInvalidConfigurations(TestContext& context)
{
    const GrainRenderer renderer;
    CircularAudioBuffer unprepared;
    GrainPool emptyPool;
    std::array unpreparedOutput { 7.0f };
    context.expect(
        renderer.renderSample(unprepared, emptyPool, unpreparedOutput)
            == GrainRendererResult::invalidConfiguration,
        "unprepared source should be rejected"
    );
    context.expect(
        unpreparedOutput[0] == 7.0f,
        "unprepared source should leave output unchanged"
    );

    CircularAudioBuffer monoSource;
    context.expect(fillMonoRamp(monoSource), "mismatch source should prepare");
    GrainPool pool;
    context.expect(
        startGrain(pool, monoSource, 10.0, 0.0, 3),
        "mismatch grain should start"
    );

    std::array stereoOutput { 5.0f, 6.0f };
    context.expect(
        renderer.renderSample(monoSource, pool, stereoOutput)
            == GrainRendererResult::invalidConfiguration,
        "mono source with stereo output should be rejected"
    );
    context.expect(
        stereoOutput[0] == 5.0f && stereoOutput[1] == 6.0f,
        "channel mismatch should leave output unchanged"
    );
    auto snapshot = captureFirstActive(pool);
    context.expect(
        snapshot.found && snapshot.renderedFrames == 0,
        "channel mismatch should not traverse or advance grains"
    );

    std::span<float> emptyOutput;
    context.expect(
        renderer.renderSample(monoSource, pool, emptyOutput)
            == GrainRendererResult::invalidConfiguration,
        "empty output span should be rejected"
    );
    snapshot = captureFirstActive(pool);
    context.expect(
        snapshot.found && snapshot.renderedFrames == 0,
        "empty span should leave pool unchanged"
    );
}

void testResetCausesFailedRead(TestContext& context)
{
    CircularAudioBuffer source;
    context.expect(fillMonoRamp(source), "reset source should prepare");

    GrainPool pool;
    context.expect(startGrain(pool, source, 10.0, 0.0, 5), "stale grain should start");
    source.reset();

    const GrainRenderer renderer;
    std::array output { 9.0f };
    context.expect(
        renderer.renderSample(source, pool, output)
            == GrainRendererResult::degraded,
        "stale SourceRegion should degrade rendering"
    );
    context.expect(output[0] == 0.0f, "failed read should contribute no audio");
    context.expect(pool.getActiveCount() == 0, "failed grain should be marked finished");
    context.expect(pool.getOccupiedCount() == 1, "failed grain should remain occupied");

    pool.reclaimFinished();
    context.expect(pool.getOccupiedCount() == 0, "failed grain should reclaim explicitly");
}

bool fillStereoWithNonFinite(
    CircularAudioBuffer& source,
    float nonFiniteValue
)
{
    if (!prepareBuffer(source, 2))
        return false;

    for (std::size_t frame = 0; frame < defaultCapacityFrames; ++frame)
    {
        const auto index = static_cast<float>(frame);
        const auto right = frame == 10 ? nonFiniteValue : 100.0f + index;
        const std::array input { index, right };

        if (!source.writeFrame(input))
            return false;
    }

    return true;
}

void runNonFiniteMaterialCase(
    TestContext& context,
    float nonFiniteValue,
    const char* preparationMessage,
    const char* degradationMessage
)
{
    CircularAudioBuffer source;
    context.expect(
        fillStereoWithNonFinite(source, nonFiniteValue),
        preparationMessage
    );

    GrainPool pool;
    context.expect(
        startGrain(pool, source, 8.0, 2.0, 3),
        "non-finite material grain should start"
    );

    const GrainRenderer renderer;
    std::array output { -1.0f, -1.0f };
    context.expect(
        renderer.renderSample(source, pool, output)
            == GrainRendererResult::success,
        "finite first trajectory frame should render"
    );
    context.expect(writeFollowUpFrame(source), "non-finite follow-up write should succeed");
    output = { 3.0f, 4.0f };
    context.expect(
        renderer.renderSample(source, pool, output)
            == GrainRendererResult::degraded,
        degradationMessage
    );
    context.expect(
        output[0] == 0.0f && output[1] == 0.0f,
        "one non-finite stereo channel must discard the entire grain frame"
    );
    context.expect(
        std::isfinite(output[0]) && std::isfinite(output[1]),
        "non-finite material must never reach output"
    );
    context.expect(pool.getActiveCount() == 0, "non-finite grain should finish immediately");
}

void testNonFiniteMaterial(TestContext& context)
{
    runNonFiniteMaterialCase(
        context,
        std::numeric_limits<float>::quiet_NaN(),
        "NaN source should prepare",
        "NaN material should degrade rendering"
    );
    runNonFiniteMaterialCase(
        context,
        std::numeric_limits<float>::infinity(),
        "Inf source should prepare",
        "Inf material should degrade rendering"
    );
}

void testFloatConversionOverflow(TestContext& context)
{
    CircularAudioBuffer source;
    context.expect(
        fillStereoConstant(
            source,
            std::numeric_limits<float>::max(),
            1.0f
        ),
        "float-overflow source should prepare"
    );

    GrainPool pool;
    context.expect(startGrain(pool, source, 10.0, 0.0, 3), "first huge grain should start");
    context.expect(startGrain(pool, source, 10.0, 0.0, 3), "second huge grain should start");

    const GrainRenderer renderer;
    std::array output { -1.0f, -1.0f };
    context.expect(
        renderer.renderSample(source, pool, output)
            == GrainRendererResult::success,
        "zero-window huge frame should render"
    );
    context.expect(writeFollowUpFrame(source), "huge-value follow-up write should succeed");
    context.expect(
        renderer.renderSample(source, pool, output)
            == GrainRendererResult::degraded,
        "unrepresentable float sum should degrade rendering"
    );
    context.expect(
        output[0] == 0.0f && output[1] == 0.0f,
        "one unrepresentable channel should zero the complete stereo frame"
    );
    context.expect(
        std::isfinite(output[0]) && std::isfinite(output[1]),
        "overflow protection must emit only finite values"
    );

    const auto snapshot = captureFirstActive(pool);
    context.expect(
        snapshot.found && snapshot.renderedFrames == 2,
        "valid source reads should advance even when final float conversion fails"
    );
}

void testInvalidAndValidGrainsTogether(TestContext& context)
{
    CircularAudioBuffer source;
    context.expect(fillMonoRamp(source, 64), "mixed-validity source should prepare");

    GrainPool pool;
    context.expect(startGrain(pool, source, 10.0, 0.0, 5), "soon-stale grain should start");

    source.reset();

    for (std::size_t frame = 0; frame < 64; ++frame)
    {
        const std::array input { 100.0f + static_cast<float>(frame) };
        context.expect(source.writeFrame(input), "refilled mixed-validity source write");
    }

    context.expect(startGrain(pool, source, 10.0, 0.0, 3), "fresh grain should start");

    const GrainRenderer renderer;
    std::array output { -1.0f };
    context.expect(
        renderer.renderSample(source, pool, output)
            == GrainRendererResult::degraded,
        "one stale voice should report degraded while fresh voice renders"
    );
    context.expect(pool.getActiveCount() == 1, "fresh grain should remain active");
    context.expect(pool.getOccupiedCount() == 2, "stale grain should await reclaim");
    auto snapshot = captureFirstActive(pool);
    context.expect(
        snapshot.found && snapshot.renderedFrames == 1,
        "fresh grain should advance despite another voice failure"
    );

    context.expect(writeFollowUpFrame(source), "fresh grain follow-up write should succeed");
    context.expect(
        renderer.renderSample(source, pool, output)
            == GrainRendererResult::success,
        "remaining fresh voice should render normally"
    );
    context.expectNear(output[0], 110.0, 1.0e-6, "fresh voice should produce its middle frame");
}
}

int main()
{
    TestContext context;

    testNoActiveGrains(context);
    testMonoFractionalLifecycle(context);
    testStereoFractionalRead(context);
    testForwardReverseAndHann(context);
    testSummingWithoutNormalisation(context);
    testMaximumVoiceCount(context);
    testInvalidConfigurations(context);
    testResetCausesFailedRead(context);
    testNonFiniteMaterial(context);
    testFloatConversionOverflow(context);
    testInvalidAndValidGrainsTogether(context);

    if (context.getFailures() == 0)
    {
        std::cout << "All GrainRenderer tests passed.\n";
        return 0;
    }

    std::cerr << context.getFailures() << " test(s) failed.\n";
    return 1;
}
