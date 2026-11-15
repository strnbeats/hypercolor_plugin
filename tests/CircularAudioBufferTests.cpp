#include "core/ProcessSpec.h"
#include "dsp/granular/CircularAudioBuffer.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>

namespace
{
using hypercolor::core::ProcessSpec;
using hypercolor::dsp::granular::CircularAudioBuffer;

constexpr double sampleRate = 44100.0;

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

    template <typename Actual, typename Expected>
    void expectEqual(
        Actual actual,
        Expected expected,
        const char* message
    )
    {
        expect(actual == static_cast<Actual>(expected), message);
    }

    void expectNear(
        float actual,
        float expected,
        float tolerance,
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
    double rate = sampleRate,
    std::uint32_t channels = 1,
    std::uint32_t maximumBlockSize = 512
)
{
    return { rate, maximumBlockSize, channels };
}

double durationForFrames(std::size_t frames, double rate = sampleRate)
{
    return static_cast<double>(frames) / rate;
}

bool writeMono(CircularAudioBuffer& buffer, float sample)
{
    const std::array frame { sample };
    return buffer.writeFrame(frame);
}

bool writeStereo(
    CircularAudioBuffer& buffer,
    float left,
    float right
)
{
    const std::array frame { left, right };
    return buffer.writeFrame(frame);
}

void testProductConfigurationAndPreparation(TestContext& context)
{
    constexpr std::array rates {
        44100.0,
        48000.0,
        96000.0,
        192000.0
    };

    for (const auto rate : rates)
    {
        for (const auto channels : { 1u, 2u })
        {
            CircularAudioBuffer buffer;
            context.expect(
                buffer.prepare(
                    makeSpec(rate, channels),
                    CircularAudioBuffer::productMaximumDurationSeconds
                ),
                "product configuration should prepare"
            );
            context.expectEqual(
                buffer.getCapacityFrames(),
                static_cast<std::size_t>(rate * 10.0),
                "ten-second capacity should follow sample rate"
            );
            context.expectEqual(
                buffer.getLogicalLengthFrames(),
                static_cast<std::size_t>(rate * 4.0),
                "default logical duration should be four seconds"
            );
            context.expectEqual(
                buffer.getNumChannels(),
                channels,
                "prepared channel count should be retained"
            );
        }
    }

    CircularAudioBuffer buffer;
    context.expect(
        !buffer.prepare(makeSpec(0.0), 10.0, 4.0),
        "zero sample rate should be rejected"
    );
    context.expect(
        !buffer.prepare(makeSpec(44099.0), 10.0, 4.0),
        "sample rates below the supported range should be rejected"
    );
    context.expect(
        !buffer.prepare(makeSpec(192001.0), 10.0, 4.0),
        "sample rates above the supported range should be rejected"
    );
    context.expect(
        !buffer.prepare(
            makeSpec(std::numeric_limits<double>::quiet_NaN()),
            10.0,
            4.0
        ),
        "non-finite sample rate should be rejected"
    );
    context.expect(
        !buffer.prepare(makeSpec(sampleRate, 0), 10.0, 4.0),
        "zero channels should be rejected"
    );
    context.expect(
        !buffer.prepare(makeSpec(sampleRate, 3), 10.0, 4.0),
        "channel layouts above stereo should be rejected"
    );
    context.expect(
        !buffer.prepare(makeSpec(sampleRate, 1, 0), 10.0, 4.0),
        "zero maximum block size should be rejected"
    );
    context.expect(
        !buffer.prepare(makeSpec(), 0.0, 0.0),
        "zero durations should be rejected"
    );
    context.expect(
        !buffer.prepare(makeSpec(), 4.0, 5.0),
        "logical duration above capacity should be rejected"
    );
    context.expect(
        !buffer.prepare(makeSpec(), 10.01, 4.0),
        "maximum duration above the product limit should be rejected"
    );
    context.expect(
        !buffer.prepare(
            makeSpec(),
            std::numeric_limits<double>::infinity(),
            4.0
        ),
        "non-finite duration should be rejected"
    );
    context.expect(!buffer.isPrepared(), "invalid prepare should be safe");
}

void testStartupInterpolationAndChannels(TestContext& context)
{
    CircularAudioBuffer mono;
    context.expect(
        mono.prepare(makeSpec(), durationForFrames(4), durationForFrames(4)),
        "small mono buffer should prepare"
    );

    auto region = mono.captureSourceRegion();
    float result = 123.0f;
    context.expect(region.isEmpty(), "new buffer should have no history");
    context.expect(
        !mono.tryReadLinear(region, 0, 0.0, result),
        "empty history should not be readable"
    );
    context.expectNear(result, 0.0f, 0.0f, "failed read should clear result");

    context.expect(writeMono(mono, 0.0f), "first mono frame should write");
    region = mono.captureSourceRegion();
    context.expect(
        !mono.isLinearReadPositionValid(region, 0.0),
        "one sample is insufficient for two interpolation taps"
    );

    context.expect(writeMono(mono, 10.0f), "second mono frame should write");
    context.expect(writeMono(mono, 20.0f), "third mono frame should write");
    context.expect(writeMono(mono, 30.0f), "fourth mono frame should write");
    region = mono.captureSourceRegion();

    context.expect(
        mono.tryReadLinear(region, 0, 1.0, result),
        "integer position with a reserved second tap should read"
    );
    context.expectNear(result, 10.0f, 1.0e-6f, "integer read should match");
    context.expect(
        mono.tryReadLinear(region, 0, 1.5, result),
        "fractional position should read"
    );
    context.expectNear(result, 15.0f, 1.0e-6f, "linear interpolation should match");
    context.expect(
        mono.isLinearReadPositionValid(
            region,
            std::nextafter(3.0, 0.0)
        ),
        "fraction below the strict upper bound should be valid"
    );
    context.expect(
        !mono.isLinearReadPositionValid(region, 3.0),
        "newest sample cannot be the first interpolation tap"
    );
    context.expect(
        !mono.isLinearReadPositionValid(region, -0.000001),
        "negative fractional position should be rejected"
    );
    context.expect(
        !mono.isLinearReadPositionValid(
            region,
            std::numeric_limits<double>::quiet_NaN()
        ),
        "NaN read position should be rejected"
    );
    context.expect(
        !mono.isLinearReadPositionValid(
            region,
            std::numeric_limits<double>::infinity()
        ),
        "infinite read position should be rejected"
    );
    context.expect(
        !mono.tryReadLinear(region, 1, 1.0, result),
        "channel outside the prepared layout should be rejected"
    );

    CircularAudioBuffer stereo;
    context.expect(
        stereo.prepare(
            makeSpec(sampleRate, 2),
            durationForFrames(3),
            durationForFrames(3)
        ),
        "small stereo buffer should prepare"
    );
    context.expect(writeStereo(stereo, 1.0f, 10.0f), "stereo frame 1");
    context.expect(writeStereo(stereo, 3.0f, 30.0f), "stereo frame 2");
    const auto stereoRegion = stereo.captureSourceRegion();
    context.expect(
        stereo.tryReadLinear(stereoRegion, 0, 0.5, result),
        "left channel should read"
    );
    context.expectNear(result, 2.0f, 1.0e-6f, "left interpolation should match");
    context.expect(
        stereo.tryReadLinear(stereoRegion, 1, 0.5, result),
        "right channel should read"
    );
    context.expectNear(result, 20.0f, 1.0e-6f, "right interpolation should match");
}

void testWraparoundAndPhysicalValidity(TestContext& context)
{
    CircularAudioBuffer buffer;
    context.expect(
        buffer.prepare(makeSpec(), durationForFrames(4), durationForFrames(4)),
        "wraparound buffer should prepare"
    );

    for (int sample = 0; sample < 4; ++sample)
        context.expect(writeMono(buffer, static_cast<float>(sample * 10)), "initial write");

    const auto oldRegion = buffer.captureSourceRegion();
    context.expect(writeMono(buffer, 40.0f), "wraparound write should succeed");
    const auto physical = buffer.getPhysicalFrameRange();
    context.expectEqual(physical.oldest, 1u, "overwritten frame should leave range");
    context.expectEqual(physical.newestExclusive, 5u, "write position should advance");

    float result = 0.0f;
    context.expect(
        !buffer.tryReadLinear(oldRegion, 0, 0.0, result),
        "overwritten source tap should be rejected"
    );
    context.expect(
        !buffer.tryReadLinear(
            oldRegion,
            0,
            std::nextafter(1.0, 0.0),
            result
        ),
        "fraction immediately below physical oldest should be rejected"
    );
    context.expect(
        buffer.tryReadLinear(oldRegion, 0, 1.0, result),
        "physical oldest should be a valid first tap"
    );
    context.expect(
        buffer.tryReadLinear(oldRegion, 0, 1.5, result),
        "remaining part of an old region may still be read"
    );
    context.expectNear(result, 15.0f, 1.0e-6f, "old region data should remain correct");

    const auto newRegion = buffer.captureSourceRegion();
    context.expect(
        buffer.tryReadLinear(newRegion, 0, 3.5, result),
        "linear read should cross the physical wrap boundary"
    );
    context.expectNear(result, 35.0f, 1.0e-6f, "wrapped interpolation should match");
}

void testLogicalDurationAndSourceLifetime(TestContext& context)
{
    CircularAudioBuffer buffer;
    context.expect(
        buffer.prepare(makeSpec(), durationForFrames(10), durationForFrames(4)),
        "logical-duration buffer should prepare"
    );

    for (int sample = 0; sample < 10; ++sample)
        context.expect(writeMono(buffer, static_cast<float>(sample)), "history write");

    const auto originalCapacity = buffer.getCapacityFrames();
    const auto originalRegion = buffer.captureSourceRegion();
    context.expectEqual(originalRegion.oldest, 6u, "four-frame logical oldest");
    context.expectEqual(originalRegion.newestExclusive, 10u, "logical newest");

    context.expect(
        buffer.setLogicalDurationSeconds(durationForFrames(2)),
        "logical duration should shrink without reprepare"
    );
    context.expectEqual(
        buffer.getCapacityFrames(),
        originalCapacity,
        "logical shrink must not change physical capacity"
    );
    const auto smallRegion = buffer.captureSourceRegion();
    context.expectEqual(smallRegion.oldest, 8u, "shrunk logical oldest");

    float result = 0.0f;
    context.expect(
        buffer.tryReadLinear(originalRegion, 0, 6.0, result),
        "logical shrink must not invalidate an existing SourceRegion"
    );

    context.expect(
        buffer.setLogicalDurationSeconds(durationForFrames(10)),
        "logical duration should expand without reprepare"
    );
    const auto expandedRegion = buffer.captureSourceRegion();
    context.expectEqual(expandedRegion.oldest, 0u, "expanded range may expose retained history");
    context.expect(
        buffer.isSourceRegionCurrent(originalRegion),
        "logical changes must preserve region generation"
    );
    context.expect(
        !buffer.setLogicalDurationSeconds(durationForFrames(11)),
        "logical duration beyond physical capacity should be rejected"
    );
    context.expectEqual(
        buffer.getLogicalLengthFrames(),
        10u,
        "rejected logical duration must leave the current value unchanged"
    );
    context.expect(
        !buffer.setLogicalDurationSeconds(
            std::numeric_limits<double>::quiet_NaN()
        ),
        "non-finite logical duration should be rejected"
    );
}

void testTrajectoryValidity(TestContext& context)
{
    CircularAudioBuffer full;
    context.expect(
        full.prepare(makeSpec(), durationForFrames(8), durationForFrames(8)),
        "trajectory buffer should prepare"
    );

    for (int sample = 0; sample < 8; ++sample)
        context.expect(writeMono(full, static_cast<float>(sample)), "trajectory history write");

    const auto region = full.captureSourceRegion();
    context.expect(
        full.isLinearTrajectoryValid(region, 0.0, 1.0, 7),
        "forward playback matching write speed should remain valid"
    );
    context.expect(
        !full.isLinearTrajectoryValid(region, 0.0, 0.5, 2),
        "forward playback slower than writing should detect overwrite"
    );
    context.expect(
        full.isLinearTrajectoryValid(region, 0.0, 2.0, 4),
        "bounded fast forward trajectory should be valid"
    );
    context.expect(
        !full.isLinearTrajectoryValid(region, 0.0, 2.0, 5),
        "forward trajectory crossing the fixed source upper bound should fail"
    );
    context.expect(
        full.isLinearTrajectoryValid(region, 6.0, -1.0, 4),
        "bounded reverse trajectory should be valid"
    );
    context.expect(
        !full.isLinearTrajectoryValid(region, 6.0, -1.0, 5),
        "reverse trajectory caught by overwrite should fail"
    );
    context.expect(
        full.isLinearTrajectoryValid(
            region,
            std::nextafter(7.0, 0.0),
            0.0,
            1
        ),
        "fraction below strict source upper bound should be valid"
    );
    context.expect(
        !full.isLinearTrajectoryValid(region, 7.0, 0.0, 1),
        "strict source upper bound should reserve the second tap"
    );
    context.expect(
        !full.isLinearTrajectoryValid(region, 0.0, 1.0, 0),
        "zero-length trajectory should be rejected"
    );
    context.expect(
        !full.isLinearTrajectoryValid(
            region,
            0.0,
            std::numeric_limits<double>::quiet_NaN(),
            1
        ),
        "non-finite playback rate should be rejected"
    );

    CircularAudioBuffer partial;
    context.expect(
        partial.prepare(makeSpec(), durationForFrames(8), durationForFrames(8)),
        "partial trajectory buffer should prepare"
    );
    for (int sample = 0; sample < 4; ++sample)
        context.expect(writeMono(partial, static_cast<float>(sample)), "partial history write");

    const auto partialRegion = partial.captureSourceRegion();
    context.expect(
        partial.isLinearTrajectoryValid(partialRegion, 0.0, 0.0, 5),
        "stationary trajectory should remain valid until overwrite starts"
    );
    context.expect(
        !partial.isLinearTrajectoryValid(partialRegion, 0.0, 0.0, 6),
        "trajectory should detect the first future overwrite"
    );
    context.expect(
        partial.isLinearTrajectoryValid(partialRegion, 2.0, -1.0, 3),
        "reverse playback should work with partial history"
    );
    context.expect(
        !partial.isLinearTrajectoryValid(partialRegion, 2.0, -1.0, 4),
        "reverse playback before accumulated history should fail"
    );
}

void testResetAndReprepare(TestContext& context)
{
    CircularAudioBuffer buffer;
    context.expect(
        buffer.prepare(makeSpec(), durationForFrames(4), durationForFrames(4)),
        "reset buffer should prepare"
    );
    context.expect(writeMono(buffer, 1.0f), "reset history write 1");
    context.expect(writeMono(buffer, 2.0f), "reset history write 2");
    const auto oldRegion = buffer.captureSourceRegion();
    const auto oldGeneration = buffer.getGeneration();

    buffer.reset();
    context.expect(
        buffer.getGeneration() != oldGeneration,
        "reset should advance generation"
    );
    context.expect(
        !buffer.isSourceRegionCurrent(oldRegion),
        "reset should invalidate existing SourceRegion"
    );
    context.expect(
        buffer.getPhysicalFrameRange().isEmpty(),
        "reset should discard history without requiring a memory clear"
    );
    context.expect(writeMono(buffer, 9.0f), "write after reset should work");
    context.expect(writeMono(buffer, 11.0f), "second write after reset should work");

    float result = 0.0f;
    const auto resetRegion = buffer.captureSourceRegion();
    context.expect(
        buffer.tryReadLinear(resetRegion, 0, 0.5, result),
        "new generation should be readable"
    );
    context.expectNear(result, 10.0f, 1.0e-6f, "post-reset data should replace stale data");

    const auto generationBeforeReprepare = buffer.getGeneration();
    context.expect(
        buffer.prepare(
            makeSpec(48000.0, 2, 64),
            durationForFrames(6, 48000.0),
            durationForFrames(3, 48000.0)
        ),
        "reprepare should succeed"
    );
    context.expect(
        buffer.getGeneration() != generationBeforeReprepare,
        "reprepare should advance generation"
    );
    context.expect(
        !buffer.isSourceRegionCurrent(resetRegion),
        "reprepare should invalidate the previous preparation"
    );
    context.expectEqual(buffer.getNumChannels(), 2u, "reprepare should update layout");

    const std::array wrongFrame { 1.0f };
    const auto writePosition = buffer.getAbsoluteWritePosition();
    context.expect(
        !buffer.writeFrame(wrongFrame),
        "wrong frame channel count should be rejected"
    );
    context.expectEqual(
        buffer.getAbsoluteWritePosition(),
        writePosition,
        "failed write must not mutate history"
    );
}
}

int main()
{
    TestContext context;

    testProductConfigurationAndPreparation(context);
    testStartupInterpolationAndChannels(context);
    testWraparoundAndPhysicalValidity(context);
    testLogicalDurationAndSourceLifetime(context);
    testTrajectoryValidity(context);
    testResetAndReprepare(context);

    if (context.getFailures() == 0)
    {
        std::cout << "All CircularAudioBuffer tests passed.\n";
        return 0;
    }

    std::cerr << context.getFailures() << " test(s) failed.\n";
    return 1;
}
