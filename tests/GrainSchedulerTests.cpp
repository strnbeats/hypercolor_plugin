#include "core/ProcessSpec.h"
#include "dsp/granular/CircularAudioBuffer.h"
#include "dsp/granular/GrainScheduler.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <utility>
#include <vector>

namespace
{
using hypercolor::core::ProcessSpec;
using hypercolor::dsp::granular::CircularAudioBuffer;
using hypercolor::dsp::granular::GrainInit;
using hypercolor::dsp::granular::GrainPool;
using hypercolor::dsp::granular::GrainRenderAccess;
using hypercolor::dsp::granular::GrainScheduler;
using hypercolor::dsp::granular::GrainSchedulerParameters;
using hypercolor::dsp::granular::GrainSchedulerStepResult;
using hypercolor::dsp::granular::GrainStartResult;
using hypercolor::dsp::granular::GrainWindow;

constexpr double defaultSampleRate = 44100.0;

static_assert(noexcept(
    std::declval<GrainScheduler&>().prepare(std::declval<ProcessSpec>())
));
static_assert(noexcept(std::declval<GrainScheduler&>().reset()));
static_assert(noexcept(std::declval<const GrainScheduler&>().isPrepared()));
static_assert(noexcept(
    std::declval<GrainScheduler&>().processSample(
        std::declval<const GrainSchedulerParameters&>(),
        std::declval<const CircularAudioBuffer&>(),
        std::declval<GrainPool&>()
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

ProcessSpec makeSpec(
    double sampleRate = defaultSampleRate,
    std::uint32_t maximumBlockSize = 1024,
    std::uint32_t numChannels = 1
)
{
    return { sampleRate, maximumBlockSize, numChannels };
}

bool writeFrame(CircularAudioBuffer& buffer, float value = 0.0f)
{
    if (buffer.getNumChannels() == 2)
    {
        const std::array frame { value, -value };
        return buffer.writeFrame(frame);
    }

    const std::array frame { value };
    return buffer.writeFrame(frame);
}

bool prepareSource(
    CircularAudioBuffer& buffer,
    const ProcessSpec& spec,
    double maximumSeconds,
    double logicalSeconds,
    std::uint64_t historyFrames
)
{
    if (!buffer.prepare(spec, maximumSeconds, logicalSeconds))
        return false;

    for (std::uint64_t frame = 0; frame < historyFrames; ++frame)
    {
        if (!writeFrame(buffer, static_cast<float>(frame % 1024)))
            return false;
    }

    return true;
}

struct GrainSnapshot
{
    CircularAudioBuffer::SourceRegion sourceRegion;
    double readPosition = 0.0;
    double playbackRate = 0.0;
    std::uint32_t durationFrames = 0;
    std::uint32_t renderedFrames = 0;
    GrainWindow window = GrainWindow::hann;
    bool captured = false;
};

GrainSnapshot captureFirstActive(GrainPool& pool)
{
    GrainSnapshot snapshot;
    pool.forEachActive(
        [&](GrainRenderAccess& access) noexcept
        {
            if (snapshot.captured)
                return;

            snapshot.sourceRegion = access.getSourceRegion();
            snapshot.readPosition = access.getReadPosition();
            snapshot.playbackRate = access.getPlaybackRate();
            snapshot.durationFrames = access.getDurationFrames();
            snapshot.renderedFrames = access.getRenderedFrames();
            snapshot.window = access.getWindow();
            snapshot.captured = true;
        }
    );
    return snapshot;
}

GrainSchedulerStepResult runUntilEvent(
    GrainScheduler& scheduler,
    CircularAudioBuffer& source,
    GrainPool& pool,
    const GrainSchedulerParameters& parameters,
    std::uint64_t maximumSamples,
    std::uint64_t* eventSample = nullptr
)
{
    for (std::uint64_t sample = 0; sample < maximumSamples; ++sample)
    {
        if (!writeFrame(source, static_cast<float>(sample % 1024)))
            return GrainSchedulerStepResult::sourceUnavailable;

        const auto result = scheduler.processSample(parameters, source, pool);

        if (result != GrainSchedulerStepResult::noEvent)
        {
            if (eventSample != nullptr)
                *eventSample = sample;

            return result;
        }
    }

    return GrainSchedulerStepResult::noEvent;
}

void testPreparationAndDefaults(TestContext& context)
{
    GrainSchedulerParameters parameters;
    context.expect(
        parameters.densityGrainsPerSecond == 20.0,
        "default Density should be 20"
    );
    context.expect(
        parameters.grainSizeMilliseconds == 100.0,
        "default Grain Size should be 100 ms"
    );
    context.expect(parameters.positionNormalized == 0.0, "default Position");
    context.expect(parameters.pitchSemitones == 0.0, "default Pitch");
    context.expect(!parameters.reverse, "default Reverse should be false");

    GrainScheduler scheduler;
    context.expect(!scheduler.isPrepared(), "scheduler should start unprepared");
    context.expect(
        !scheduler.prepare(makeSpec(44099.0)),
        "unsupported sample rate should fail"
    );
    context.expect(!scheduler.isPrepared(), "failed prepare should be transactional");
    context.expect(
        !scheduler.prepare(makeSpec(defaultSampleRate, 0)),
        "zero maximum block size should fail"
    );
    context.expect(
        !scheduler.prepare(makeSpec(defaultSampleRate, 1024, 3)),
        "unsupported channel count should fail"
    );
    context.expect(scheduler.prepare(makeSpec()), "valid scheduler prepare");
    context.expect(scheduler.isPrepared(), "prepared state should be retained");
}

void testTimingDensitiesAndSampleRates(TestContext& context)
{
    constexpr std::array sampleRates { 44100.0, 48000.0, 96000.0, 192000.0 };
    constexpr std::array densities { 1.0, 10.0, 37.5, 120.0 };

    for (const auto sampleRate : sampleRates)
    {
        for (const auto density : densities)
        {
            const auto spec = makeSpec(sampleRate);
            CircularAudioBuffer source;
            context.expect(
                prepareSource(
                    source,
                    spec,
                    1.0,
                    1.0,
                    static_cast<std::uint64_t>(sampleRate * 0.02)
                ),
                "timing source should prepare"
            );

            GrainScheduler scheduler;
            GrainPool pool;
            context.expect(scheduler.prepare(spec), "timing scheduler prepare");

            GrainSchedulerParameters parameters;
            parameters.densityGrainsPerSecond = density;
            parameters.grainSizeMilliseconds = 10.0;

            std::uint64_t started = 0;
            const auto totalSamples = static_cast<std::uint64_t>(
                sampleRate * 2.0
            );

            for (std::uint64_t sample = 0; sample < totalSamples; ++sample)
            {
                context.expect(writeFrame(source), "timing input write");
                const auto result = scheduler.processSample(
                    parameters,
                    source,
                    pool
                );

                if (result == GrainSchedulerStepResult::started)
                    ++started;

                context.expect(
                    result == GrainSchedulerStepResult::noEvent
                        || result == GrainSchedulerStepResult::started,
                    "valid timing event should start"
                );
                pool.reset();
            }

            context.expect(
                started == static_cast<std::uint64_t>(density * 2.0),
                "two seconds should produce exact scheduled count"
            );
        }
    }
}

void testFirstEventZeroDensityAndReset(TestContext& context)
{
    const auto spec = makeSpec(48000.0);
    CircularAudioBuffer source;
    context.expect(
        prepareSource(source, spec, 1.0, 1.0, 4800),
        "phase source should prepare"
    );
    GrainScheduler scheduler;
    GrainPool pool;
    context.expect(scheduler.prepare(spec), "phase scheduler prepare");

    GrainSchedulerParameters parameters;
    parameters.densityGrainsPerSecond = 10.0;
    parameters.grainSizeMilliseconds = 10.0;

    for (std::uint64_t sample = 0; sample < 4799; ++sample)
    {
        context.expect(writeFrame(source), "first-event write");
        context.expect(
            scheduler.processSample(parameters, source, pool)
                == GrainSchedulerStepResult::noEvent,
            "first event must not be immediate"
        );
    }

    GrainSchedulerParameters zeroDensity = parameters;
    zeroDensity.densityGrainsPerSecond = 0.0;
    for (std::size_t sample = 0; sample < 100; ++sample)
    {
        context.expect(writeFrame(source), "zero-density write");
        context.expect(
            scheduler.processSample(zeroDensity, source, pool)
                == GrainSchedulerStepResult::noEvent,
            "zero Density should not emit"
        );
    }

    context.expect(writeFrame(source), "preserved-phase write");
    context.expect(
        scheduler.processSample(parameters, source, pool)
            == GrainSchedulerStepResult::started,
        "zero Density should preserve accumulated phase"
    );

    pool.reset();
    for (std::size_t sample = 0; sample < 2400; ++sample)
    {
        context.expect(writeFrame(source), "pre-reset phase write");
        (void) scheduler.processSample(parameters, source, pool);
    }
    scheduler.reset();

    for (std::size_t sample = 0; sample < 4799; ++sample)
    {
        context.expect(writeFrame(source), "post-reset write");
        context.expect(
            scheduler.processSample(parameters, source, pool)
                == GrainSchedulerStepResult::noEvent,
            "reset should clear timing phase"
        );
    }
    context.expect(writeFrame(source), "post-reset event write");
    context.expect(
        scheduler.processSample(parameters, source, pool)
            == GrainSchedulerStepResult::started,
        "full interval should be required after reset"
    );
}

void testDensityChangesAndInvalidDensity(TestContext& context)
{
    const auto spec = makeSpec(48000.0);
    CircularAudioBuffer source;
    context.expect(
        prepareSource(source, spec, 1.0, 1.0, 4800),
        "density-change source should prepare"
    );
    GrainScheduler scheduler;
    GrainPool pool;
    context.expect(scheduler.prepare(spec), "density-change scheduler prepare");

    GrainSchedulerParameters parameters;
    parameters.densityGrainsPerSecond = 10.0;
    parameters.grainSizeMilliseconds = 10.0;

    for (std::size_t sample = 0; sample < 2400; ++sample)
    {
        context.expect(writeFrame(source), "density phase write");
        context.expect(
            scheduler.processSample(parameters, source, pool)
                == GrainSchedulerStepResult::noEvent,
            "half phase should not emit"
        );
    }

    auto invalid = parameters;
    invalid.densityGrainsPerSecond = std::numeric_limits<double>::quiet_NaN();
    context.expect(writeFrame(source), "invalid-density write");
    context.expect(
        scheduler.processSample(invalid, source, pool)
            == GrainSchedulerStepResult::invalidParameters,
        "NaN Density should be rejected"
    );

    parameters.densityGrainsPerSecond = 20.0;
    for (std::size_t sample = 0; sample < 1199; ++sample)
    {
        context.expect(writeFrame(source), "changed-density write");
        context.expect(
            scheduler.processSample(parameters, source, pool)
                == GrainSchedulerStepResult::noEvent,
            "changed Density should integrate per sample"
        );
    }
    context.expect(writeFrame(source), "changed-density event write");
    context.expect(
        scheduler.processSample(parameters, source, pool)
            == GrainSchedulerStepResult::started,
        "invalid Density must not corrupt prior phase"
    );

    scheduler.reset();
    invalid.densityGrainsPerSecond = std::numeric_limits<double>::infinity();
    context.expect(writeFrame(source), "infinite-density write");
    context.expect(
        scheduler.processSample(invalid, source, pool)
            == GrainSchedulerStepResult::invalidParameters,
        "infinite Density should be rejected"
    );
}

std::vector<std::uint64_t> collectEventIndices(
    std::uint32_t blockSize,
    std::uint64_t totalSamples
)
{
    const auto spec = makeSpec(48000.0, 1024);
    CircularAudioBuffer source;
    GrainScheduler scheduler;
    GrainPool pool;
    std::vector<std::uint64_t> events;

    if (!prepareSource(source, spec, 1.0, 1.0, 2400)
        || !scheduler.prepare(spec))
    {
        return events;
    }

    for (std::uint64_t blockStart = 0;
         blockStart < totalSamples;
         blockStart += blockSize)
    {
        const auto blockEnd = std::min<std::uint64_t>(
            totalSamples,
            blockStart + blockSize
        );

        for (auto sample = blockStart; sample < blockEnd; ++sample)
        {
            GrainSchedulerParameters parameters;
            parameters.grainSizeMilliseconds = 10.0;
            parameters.densityGrainsPerSecond = sample < totalSamples / 3
                ? 10.0
                : (sample < totalSamples * 2 / 3 ? 37.5 : 120.0);

            if (!writeFrame(source))
                return {};

            const auto result = scheduler.processSample(
                parameters,
                source,
                pool
            );

            if (result == GrainSchedulerStepResult::started)
                events.push_back(sample);

            pool.reset();
        }
    }

    return events;
}

void testBlockIndependence(TestContext& context)
{
    constexpr std::uint64_t totalSamples = 96000;
    const auto reference = collectEventIndices(1, totalSamples);
    context.expect(!reference.empty(), "block reference should contain events");

    for (const auto blockSize : { 7u, 64u, 511u, 1024u, 2048u, 4097u })
    {
        context.expect(
            collectEventIndices(blockSize, totalSamples) == reference,
            "event indices must be independent of block partition"
        );
    }
}

GrainSnapshot startAndCapture(
    TestContext& context,
    const GrainSchedulerParameters& parameters,
    double sampleRate = defaultSampleRate,
    double historySeconds = 3.0
)
{
    const auto spec = makeSpec(sampleRate);
    CircularAudioBuffer source;
    GrainScheduler scheduler;
    GrainPool pool;
    context.expect(
        prepareSource(
            source,
            spec,
            4.0,
            4.0,
            static_cast<std::uint64_t>(sampleRate * historySeconds)
        ),
        "parameter source should prepare"
    );
    context.expect(scheduler.prepare(spec), "parameter scheduler prepare");

    auto fastParameters = parameters;
    fastParameters.densityGrainsPerSecond = 120.0;
    context.expect(
        runUntilEvent(
            scheduler,
            source,
            pool,
            fastParameters,
            static_cast<std::uint64_t>(sampleRate / 120.0) + 2
        ) == GrainSchedulerStepResult::started,
        "parameter grain should start"
    );
    return captureFirstActive(pool);
}

struct RenderedSchedulerTrajectory
{
    GrainSnapshot initial;
    std::vector<double> positions;
    bool started = false;
    bool allPositionsMatched = true;
    bool allReadsValid = true;
    bool finishedAfterDuration = false;
};

RenderedSchedulerTrajectory renderScheduledTrajectory(
    TestContext& context,
    const GrainSchedulerParameters& requestedParameters
)
{
    const auto spec = makeSpec();
    CircularAudioBuffer source;
    GrainScheduler scheduler;
    GrainPool pool;
    RenderedSchedulerTrajectory result;

    context.expect(
        prepareSource(source, spec, 4.0, 4.0, 132300),
        "rendered scheduler trajectory source should prepare"
    );
    context.expect(
        scheduler.prepare(spec),
        "rendered scheduler trajectory scheduler should prepare"
    );

    auto parameters = requestedParameters;
    parameters.densityGrainsPerSecond = 120.0;
    const auto startResult = runUntilEvent(
        scheduler,
        source,
        pool,
        parameters,
        400
    );
    result.started = startResult == GrainSchedulerStepResult::started;
    context.expect(result.started, "scheduler trajectory should start");

    if (!result.started)
        return result;

    result.initial = captureFirstActive(pool);
    result.positions.reserve(result.initial.durationFrames);
    auto stoppedParameters = parameters;
    stoppedParameters.densityGrainsPerSecond = 0.0;

    for (std::uint32_t frame = 0;
         frame < result.initial.durationFrames;
         ++frame)
    {
        if (frame > 0)
        {
            context.expect(
                writeFrame(source),
                "scheduler trajectory follow-up write should succeed"
            );
            context.expect(
                scheduler.processSample(stoppedParameters, source, pool)
                    == GrainSchedulerStepResult::noEvent,
                "zero Density should not add grains during trajectory test"
            );
        }

        bool visited = false;
        pool.forEachActive(
            [&](GrainRenderAccess& access) noexcept
            {
                visited = true;
                const auto expected
                    = CircularAudioBuffer::calculateLinearTrajectoryPosition(
                        result.initial.readPosition,
                        result.initial.playbackRate,
                        frame
                    );
                const auto position = access.getReadPosition();
                result.positions.push_back(position);
                result.allPositionsMatched
                    = result.allPositionsMatched
                    && std::bit_cast<std::uint64_t>(position)
                        == std::bit_cast<std::uint64_t>(expected);

                float sample = 0.0f;
                result.allReadsValid = result.allReadsValid
                    && source.tryReadLinear(
                        access.getSourceRegion(),
                        0,
                        position,
                        sample
                    );
                access.advanceAfterRender();
            }
        );
        context.expect(visited, "scheduled grain should remain active for N frames");
    }

    result.finishedAfterDuration = pool.getActiveCount() == 0
        && pool.getOccupiedCount() == 1;
    pool.reclaimFinished();
    context.expect(
        pool.getOccupiedCount() == 0,
        "finished scheduled grain should be reclaimable"
    );
    return result;
}

void testActualScheduledTrajectories(TestContext& context)
{
    GrainSchedulerParameters parameters;
    parameters.grainSizeMilliseconds = 500.0;
    parameters.pitchSemitones = -22.0;
    parameters.positionNormalized = 0.0;
    parameters.reverse = false;
    const auto forwardBoundary = renderScheduledTrajectory(context, parameters);
    context.expect(
        forwardBoundary.allPositionsMatched,
        "forward boundary trajectory should use the validated direct formula"
    );
    context.expect(
        forwardBoundary.allReadsValid,
        "forward Position=0 Pitch=-22 should keep every read valid"
    );
    context.expect(
        forwardBoundary.finishedAfterDuration,
        "forward boundary grain should finish after exactly N frames"
    );

    parameters.pitchSemitones = -23.0;
    parameters.positionNormalized = 1.0;
    parameters.reverse = true;
    const auto reverseBoundary = renderScheduledTrajectory(context, parameters);
    context.expect(
        reverseBoundary.allPositionsMatched,
        "reverse boundary trajectory should use the validated direct formula"
    );
    context.expect(
        reverseBoundary.allReadsValid,
        "reverse Position=1 Pitch=-23 should keep every read valid"
    );
    context.expect(
        reverseBoundary.finishedAfterDuration,
        "reverse boundary grain should finish after exactly N frames"
    );

    parameters.grainSizeMilliseconds = 100.0;
    parameters.pitchSemitones = 7.0;
    parameters.positionNormalized = 0.5;
    parameters.reverse = false;
    const auto forward = renderScheduledTrajectory(context, parameters);
    parameters.reverse = true;
    const auto reverse = renderScheduledTrajectory(context, parameters);

    context.expect(
        forward.positions.size() == reverse.positions.size(),
        "forward and reverse trajectories should have equal duration"
    );

    if (forward.positions.size() == reverse.positions.size())
    {
        for (std::size_t frame = 0; frame < forward.positions.size(); ++frame)
        {
            const auto forwardPosition = forward.positions[frame];
            const auto reversePosition
                = reverse.positions[reverse.positions.size() - 1 - frame];
            const auto scale = std::max(
                std::abs(forwardPosition),
                std::abs(reversePosition)
            );
            const auto tolerance = 8.0
                * std::numeric_limits<double>::epsilon()
                * std::max(1.0, scale);
            context.expectNear(
                forwardPosition,
                reversePosition,
                tolerance,
                "forward and reverse should traverse the same segment"
            );
        }
    }
}

void testGrainSizePitchPositionAndDirection(TestContext& context)
{
    GrainSchedulerParameters parameters;
    parameters.grainSizeMilliseconds = 10.0;
    auto snapshot = startAndCapture(context, parameters);
    context.expect(snapshot.captured, "10 ms grain should be captured");
    context.expect(snapshot.durationFrames == 441, "10 ms should round to 441 frames");

    parameters.grainSizeMilliseconds = 500.0;
    snapshot = startAndCapture(context, parameters);
    context.expect(snapshot.durationFrames == 22050, "500 ms duration conversion");

    parameters.grainSizeMilliseconds = 100.0;
    for (const auto pitch : { -24.0, 0.0, 24.0 })
    {
        parameters.pitchSemitones = pitch;
        parameters.reverse = false;
        snapshot = startAndCapture(context, parameters);
        context.expectNear(
            snapshot.playbackRate,
            std::exp2(pitch / 12.0),
            1.0e-12,
            "Pitch should convert to playback rate"
        );

        parameters.reverse = true;
        snapshot = startAndCapture(context, parameters);
        context.expectNear(
            snapshot.playbackRate,
            -std::exp2(pitch / 12.0),
            1.0e-12,
            "Reverse should negate playback rate"
        );
    }

    parameters.pitchSemitones = 0.0;
    parameters.reverse = false;
    double previousPosition = std::numeric_limits<double>::infinity();

    for (const auto position : { 0.0, 0.5, 1.0 })
    {
        parameters.positionNormalized = position;
        snapshot = startAndCapture(context, parameters);
        context.expect(
            snapshot.readPosition < previousPosition,
            "Position should move monotonically from fresh to old"
        );
        previousPosition = snapshot.readPosition;
    }

    for (const auto position : { 0.0, 0.5, 1.0 })
    {
        parameters.positionNormalized = position;
        parameters.reverse = false;
        const auto forward = startAndCapture(context, parameters);
        parameters.reverse = true;
        const auto reverse = startAndCapture(context, parameters);
        const auto forwardLast
            = CircularAudioBuffer::calculateLinearTrajectoryPosition(
                forward.readPosition,
                forward.playbackRate,
                forward.durationFrames - 1
            );
        const auto reverseLast
            = CircularAudioBuffer::calculateLinearTrajectoryPosition(
                reverse.readPosition,
                reverse.playbackRate,
                reverse.durationFrames - 1
            );

        context.expectNear(
            forward.readPosition,
            reverseLast,
            1.0e-8,
            "forward start should equal reverse end"
        );
        context.expectNear(
            forwardLast,
            reverse.readPosition,
            1.0e-8,
            "forward end should equal reverse start"
        );
    }
}

void testParameterLatchingAndClamping(TestContext& context)
{
    const auto spec = makeSpec();
    CircularAudioBuffer source;
    GrainScheduler scheduler;
    GrainPool pool;
    context.expect(
        prepareSource(source, spec, 4.0, 4.0, 132300),
        "latching source should prepare"
    );
    context.expect(scheduler.prepare(spec), "latching scheduler prepare");

    GrainSchedulerParameters parameters;
    parameters.densityGrainsPerSecond = 1000.0;
    parameters.grainSizeMilliseconds = -100.0;
    parameters.positionNormalized = -1.0;
    parameters.pitchSemitones = -100.0;
    context.expect(
        runUntilEvent(scheduler, source, pool, parameters, 400)
            == GrainSchedulerStepResult::started,
        "finite lower out-of-range parameters should clamp"
    );
    const auto original = captureFirstActive(pool);
    context.expect(original.durationFrames == 441, "Grain Size lower clamp");
    context.expectNear(original.playbackRate, 0.25, 0.0, "Pitch lower clamp");

    GrainSchedulerParameters changed;
    changed.densityGrainsPerSecond = 0.0;
    changed.grainSizeMilliseconds = 500.0;
    changed.positionNormalized = 1.0;
    changed.pitchSemitones = 24.0;
    changed.reverse = true;
    context.expect(writeFrame(source), "latching follow-up write");
    context.expect(
        scheduler.processSample(changed, source, pool)
            == GrainSchedulerStepResult::noEvent,
        "zero Density should not create replacement grain"
    );
    const auto retained = captureFirstActive(pool);
    context.expect(
        retained.durationFrames == original.durationFrames,
        "active grain duration must remain latched"
    );
    context.expectNear(
        retained.readPosition,
        original.readPosition,
        0.0,
        "active grain position must remain latched"
    );
    context.expectNear(
        retained.playbackRate,
        original.playbackRate,
        0.0,
        "active grain rate must remain latched"
    );

    pool.reset();
    scheduler.reset();
    parameters.grainSizeMilliseconds = 1000.0;
    parameters.positionNormalized = 2.0;
    parameters.pitchSemitones = 100.0;
    context.expect(
        runUntilEvent(scheduler, source, pool, parameters, 400)
            == GrainSchedulerStepResult::started,
        "finite upper out-of-range parameters should clamp"
    );
    const auto upper = captureFirstActive(pool);
    context.expect(upper.durationFrames == 22050, "Grain Size upper clamp");
    context.expectNear(upper.playbackRate, 4.0, 0.0, "Pitch upper clamp");
}

void testInsufficientHistoryAndPhysicalOverwrite(TestContext& context)
{
    const auto spec = makeSpec();
    GrainSchedulerParameters parameters;
    parameters.densityGrainsPerSecond = 120.0;
    parameters.grainSizeMilliseconds = 500.0;
    parameters.pitchSemitones = 24.0;

    CircularAudioBuffer emptySource;
    context.expect(
        emptySource.prepare(spec, 1.0, 1.0),
        "empty source should prepare"
    );
    GrainScheduler emptyScheduler;
    GrainPool emptyPool;
    context.expect(emptyScheduler.prepare(spec), "empty scheduler prepare");
    GrainSchedulerStepResult emptyResult = GrainSchedulerStepResult::noEvent;
    for (std::size_t sample = 0; sample < 368; ++sample)
        emptyResult = emptyScheduler.processSample(parameters, emptySource, emptyPool);
    context.expect(
        emptyResult == GrainSchedulerStepResult::insufficientHistory,
        "empty SourceRegion should reject event"
    );

    CircularAudioBuffer shortSource;
    context.expect(
        shortSource.prepare(spec, 3.0, 3.0),
        "short source should prepare"
    );
    GrainScheduler shortScheduler;
    GrainPool shortPool;
    context.expect(shortScheduler.prepare(spec), "short scheduler prepare");
    context.expect(
        runUntilEvent(shortScheduler, shortSource, shortPool, parameters, 400)
            == GrainSchedulerStepResult::insufficientHistory,
        "trajectory longer than history should be rejected"
    );

    CircularAudioBuffer fullSource;
    context.expect(
        prepareSource(fullSource, spec, 1.0, 1.0, 44100),
        "full overwrite source should prepare"
    );
    GrainScheduler overwriteScheduler;
    GrainPool overwritePool;
    context.expect(overwriteScheduler.prepare(spec), "overwrite scheduler prepare");
    parameters.pitchSemitones = -24.0;
    parameters.positionNormalized = 1.0;
    context.expect(
        runUntilEvent(overwriteScheduler, fullSource, overwritePool, parameters, 400)
            == GrainSchedulerStepResult::invalidTrajectory,
        "future physical overwrite should reject old slow trajectory"
    );
}

void testBufferResetReprepareAndLargePositions(TestContext& context)
{
    const auto spec = makeSpec();
    CircularAudioBuffer source;
    context.expect(
        prepareSource(source, spec, 1.0, 1.0, 4410),
        "reset source should prepare"
    );
    GrainScheduler scheduler;
    GrainPool pool;
    context.expect(scheduler.prepare(spec), "reset scheduler prepare");
    GrainSchedulerParameters parameters;
    parameters.densityGrainsPerSecond = 120.0;
    parameters.grainSizeMilliseconds = 10.0;

    for (std::size_t sample = 0; sample < 367; ++sample)
    {
        context.expect(writeFrame(source), "pre-buffer-reset write");
        context.expect(
            scheduler.processSample(parameters, source, pool)
                == GrainSchedulerStepResult::noEvent,
            "phase should approach event"
        );
    }
    source.reset();
    context.expect(
        scheduler.processSample(parameters, source, pool)
            == GrainSchedulerStepResult::insufficientHistory,
        "buffer reset should invalidate available history"
    );

    scheduler.reset();
    context.expect(
        source.prepare(makeSpec(48000.0), 1.0, 1.0),
        "buffer reprepare should succeed"
    );
    GrainSchedulerStepResult mismatch = GrainSchedulerStepResult::noEvent;
    for (std::size_t sample = 0; sample < 368; ++sample)
        mismatch = scheduler.processSample(parameters, source, pool);
    context.expect(
        mismatch == GrainSchedulerStepResult::sourceUnavailable,
        "sample-rate mismatch should be rejected"
    );

    CircularAudioBuffer largeSource;
    context.expect(
        prepareSource(largeSource, spec, 1.0, 1.0, 1000000),
        "large-position source should prepare"
    );
    GrainScheduler largeScheduler;
    GrainPool largePool;
    context.expect(largeScheduler.prepare(spec), "large-position scheduler prepare");
    context.expect(
        runUntilEvent(largeScheduler, largeSource, largePool, parameters, 400)
            == GrainSchedulerStepResult::started,
        "large supported absolute position should start"
    );
    const auto large = captureFirstActive(largePool);
    context.expect(large.readPosition > 900000.0, "large absolute position retained");
    context.expect(
        largeSource.isLinearTrajectoryValid(
            large.sourceRegion,
            large.readPosition,
            large.playbackRate,
            large.durationFrames
        ),
        "large absolute trajectory should remain valid"
    );
}

void fillPool(
    TestContext& context,
    GrainPool& pool,
    const CircularAudioBuffer& source
)
{
    const auto region = source.captureSourceRegion();
    const GrainInit init {
        region,
        static_cast<double>(region.newestExclusive - 2),
        0.0,
        3,
        GrainWindow::hann
    };

    for (std::size_t index = 0; index < GrainPool::maximumGrains; ++index)
    {
        context.expect(
            pool.tryStart(init, source) == GrainStartResult::started,
            "pool setup grain should start"
        );
    }
}

void testPoolFullEventLossAndTraversalConflict(TestContext& context)
{
    const auto spec = makeSpec();
    CircularAudioBuffer source;
    context.expect(
        prepareSource(source, spec, 2.0, 2.0, 44100),
        "pool integration source should prepare"
    );
    GrainScheduler scheduler;
    GrainPool pool;
    context.expect(scheduler.prepare(spec), "pool integration scheduler prepare");
    fillPool(context, pool, source);

    GrainSchedulerParameters parameters;
    parameters.densityGrainsPerSecond = 120.0;
    parameters.grainSizeMilliseconds = 10.0;
    context.expect(
        runUntilEvent(scheduler, source, pool, parameters, 400)
            == GrainSchedulerStepResult::poolFull,
        "full pool should drop scheduled event"
    );

    bool finishedOne = false;
    pool.forEachActive(
        [&](GrainRenderAccess& access) noexcept
        {
            if (!finishedOne)
            {
                access.markFinished();
                finishedOne = true;
            }
        }
    );
    pool.reclaimFinished();
    context.expect(writeFrame(source), "post-full input write");
    context.expect(
        scheduler.processSample(parameters, source, pool)
            == GrainSchedulerStepResult::noEvent,
        "dropped event must not retry after reclaim"
    );
    context.expect(
        runUntilEvent(scheduler, source, pool, parameters, 400)
            == GrainSchedulerStepResult::started,
        "freed slot should be used only at next timing event"
    );

    pool.reset();
    scheduler.reset();
    const auto region = source.captureSourceRegion();
    const GrainInit traversalGrain {
        region,
        static_cast<double>(region.newestExclusive - 2),
        0.0,
        3,
        GrainWindow::hann
    };
    context.expect(
        pool.tryStart(traversalGrain, source) == GrainStartResult::started,
        "traversal setup grain should start"
    );

    for (std::size_t sample = 0; sample < 367; ++sample)
    {
        context.expect(writeFrame(source), "pre-traversal timing write");
        context.expect(
            scheduler.processSample(parameters, source, pool)
                == GrainSchedulerStepResult::noEvent,
            "traversal phase should approach event"
        );
    }

    context.expect(writeFrame(source), "traversal event input write");
    auto traversalResult = GrainSchedulerStepResult::noEvent;
    pool.forEachActive(
        [&](GrainRenderAccess&) noexcept
        {
            traversalResult = scheduler.processSample(parameters, source, pool);
        }
    );
    context.expect(
        traversalResult == GrainSchedulerStepResult::traversalInProgress,
        "pool traversal conflict should be reported"
    );
}

void testInvalidInputsAndUnpreparedObjects(TestContext& context)
{
    const auto spec = makeSpec();
    CircularAudioBuffer source;
    context.expect(
        prepareSource(source, spec, 3.0, 3.0, 88200),
        "invalid-input source should prepare"
    );
    GrainPool pool;
    GrainScheduler unprepared;
    GrainSchedulerParameters parameters;
    parameters.densityGrainsPerSecond = 120.0;
    context.expect(
        unprepared.processSample(parameters, source, pool)
            == GrainSchedulerStepResult::schedulerNotPrepared,
        "unprepared scheduler should reject processing"
    );

    CircularAudioBuffer unavailableSource;
    GrainScheduler sourceScheduler;
    context.expect(sourceScheduler.prepare(spec), "source-status scheduler prepare");
    GrainSchedulerStepResult unavailable = GrainSchedulerStepResult::noEvent;
    for (std::size_t sample = 0; sample < 368; ++sample)
        unavailable = sourceScheduler.processSample(parameters, unavailableSource, pool);
    context.expect(
        unavailable == GrainSchedulerStepResult::sourceUnavailable,
        "unprepared source should be reported"
    );

    const auto testInvalidEventParameter = [&](auto change, const char* message)
    {
        GrainScheduler scheduler;
        GrainPool localPool;
        context.expect(scheduler.prepare(spec), "invalid parameter scheduler prepare");
        auto invalid = parameters;
        invalid.grainSizeMilliseconds = 10.0;
        change(invalid);
        const auto result = runUntilEvent(
            scheduler,
            source,
            localPool,
            invalid,
            400
        );
        context.expect(result == GrainSchedulerStepResult::invalidParameters, message);
    };

    testInvalidEventParameter(
        [](GrainSchedulerParameters& value)
        {
            value.grainSizeMilliseconds = std::numeric_limits<double>::quiet_NaN();
        },
        "NaN Grain Size should be rejected"
    );
    testInvalidEventParameter(
        [](GrainSchedulerParameters& value)
        {
            value.grainSizeMilliseconds = std::numeric_limits<double>::infinity();
        },
        "infinite Grain Size should be rejected"
    );
    testInvalidEventParameter(
        [](GrainSchedulerParameters& value)
        {
            value.positionNormalized = std::numeric_limits<double>::quiet_NaN();
        },
        "NaN Position should be rejected"
    );
    testInvalidEventParameter(
        [](GrainSchedulerParameters& value)
        {
            value.positionNormalized = std::numeric_limits<double>::infinity();
        },
        "infinite Position should be rejected"
    );
    testInvalidEventParameter(
        [](GrainSchedulerParameters& value)
        {
            value.pitchSemitones = std::numeric_limits<double>::quiet_NaN();
        },
        "NaN Pitch should be rejected"
    );
    testInvalidEventParameter(
        [](GrainSchedulerParameters& value)
        {
            value.pitchSemitones = std::numeric_limits<double>::infinity();
        },
        "infinite Pitch should be rejected"
    );
}
}

int main()
{
    TestContext context;

    testPreparationAndDefaults(context);
    testTimingDensitiesAndSampleRates(context);
    testFirstEventZeroDensityAndReset(context);
    testDensityChangesAndInvalidDensity(context);
    testBlockIndependence(context);
    testGrainSizePitchPositionAndDirection(context);
    testActualScheduledTrajectories(context);
    testParameterLatchingAndClamping(context);
    testInsufficientHistoryAndPhysicalOverwrite(context);
    testBufferResetReprepareAndLargePositions(context);
    testPoolFullEventLossAndTraversalConflict(context);
    testInvalidInputsAndUnpreparedObjects(context);

    if (context.getFailures() == 0)
    {
        std::cout << "All GrainScheduler tests passed.\n";
        return 0;
    }

    std::cerr << context.getFailures() << " test(s) failed.\n";
    return 1;
}
