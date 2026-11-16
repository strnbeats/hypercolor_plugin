#include "dsp/granular/GranularTelemetry.h"

#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <thread>
#include <type_traits>
#include <utility>

namespace
{
using hypercolor::dsp::granular::GranularTelemetryQueue;
using hypercolor::dsp::granular::GranularTelemetrySnapshot;
using hypercolor::dsp::granular::granularTelemetryGrainCapacity;
using hypercolor::dsp::granular::granularTelemetryQueueCapacity;
using hypercolor::dsp::granular::granularTelemetryWaveformCapacity;

static_assert(noexcept(
    std::declval<GranularTelemetryQueue&>().tryPush(
        std::declval<const GranularTelemetrySnapshot&>()
    )
));
static_assert(noexcept(
    std::declval<GranularTelemetryQueue&>().tryPop(
        std::declval<GranularTelemetrySnapshot&>()
    )
));
static_assert(std::is_trivially_copyable_v<GranularTelemetrySnapshot>);
static_assert(std::is_standard_layout_v<GranularTelemetrySnapshot>);

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

GranularTelemetrySnapshot makeSnapshot(std::uint64_t sequence)
{
    GranularTelemetrySnapshot snapshot;
    snapshot.sequence = sequence;
    snapshot.generation = sequence + 10;
    snapshot.logicalOldest = sequence + 20;
    snapshot.logicalNewestExclusive = sequence + 30;
    snapshot.waveformOldest = sequence + 40;
    snapshot.waveformNewestExclusive = sequence + 50;
    snapshot.sampleRate = 44100.0 + static_cast<double>(sequence);
    snapshot.logicalDurationSeconds = 4.0;
    snapshot.validDurationSeconds = 3.0;
    snapshot.numChannels = static_cast<std::uint32_t>((sequence % 2) + 1);
    snapshot.waveformPointCount
        = static_cast<std::uint32_t>(granularTelemetryWaveformCapacity);
    snapshot.activeGrainCount
        = static_cast<std::uint32_t>(granularTelemetryGrainCapacity);
    snapshot.appliedParameters.densityGrainsPerSecond
        = 20.0 + static_cast<double>(sequence);
    snapshot.appliedParameters.grainSizeMilliseconds = 100.0;
    snapshot.appliedParameters.positionNormalized = 0.25;
    snapshot.appliedParameters.pitchSemitones = -7.0;
    snapshot.appliedParameters.reverse = (sequence % 2) != 0;
    snapshot.appliedParameters.character
        = hypercolor::dsp::granular::GranularCharacter::Atmospheric;
    snapshot.appliedParameters.pitchRandomAmount = 0.75;
    snapshot.appliedParameters.positionRandomAmount = 0.5;
    snapshot.appliedParameters.timingJitterAmount = 0.625;

    for (std::size_t index = 0; index < snapshot.waveformPoints.size(); ++index)
    {
        const auto value = static_cast<float>(sequence + index);
        snapshot.waveformPoints[index].minimum = { -value, -(value + 0.5f) };
        snapshot.waveformPoints[index].maximum = { value, value + 0.5f };
    }

    for (std::size_t index = 0; index < snapshot.grains.size(); ++index)
    {
        auto& grain = snapshot.grains[index];
        grain.readPosition = static_cast<double>(sequence * 1000 + index);
        grain.playbackRate = 0.5 + static_cast<double>(index);
        grain.sourceOldest = sequence * 100 + index;
        grain.sourceNewestExclusive = sequence * 100 + index + 64;
        grain.durationFrames = static_cast<std::uint32_t>(100 + index);
        grain.renderedFrames = static_cast<std::uint32_t>(index);
    }

    return snapshot;
}

bool exactlyEqual(float left, float right) noexcept
{
    return std::bit_cast<std::uint32_t>(left)
        == std::bit_cast<std::uint32_t>(right);
}

bool exactlyEqual(double left, double right) noexcept
{
    return std::bit_cast<std::uint64_t>(left)
        == std::bit_cast<std::uint64_t>(right);
}

bool payloadMatches(
    const GranularTelemetrySnapshot& actual,
    const GranularTelemetrySnapshot& expected
)
{
    if (actual.sequence != expected.sequence
        || actual.generation != expected.generation
        || actual.logicalOldest != expected.logicalOldest
        || actual.logicalNewestExclusive != expected.logicalNewestExclusive
        || actual.waveformOldest != expected.waveformOldest
        || actual.waveformNewestExclusive != expected.waveformNewestExclusive
        || !exactlyEqual(actual.sampleRate, expected.sampleRate)
        || !exactlyEqual(
            actual.logicalDurationSeconds,
            expected.logicalDurationSeconds
        )
        || !exactlyEqual(actual.validDurationSeconds, expected.validDurationSeconds)
        || actual.numChannels != expected.numChannels
        || actual.waveformPointCount != expected.waveformPointCount
        || actual.activeGrainCount != expected.activeGrainCount
        || !exactlyEqual(
            actual.appliedParameters.densityGrainsPerSecond,
            expected.appliedParameters.densityGrainsPerSecond
        )
        || !exactlyEqual(
            actual.appliedParameters.grainSizeMilliseconds,
            expected.appliedParameters.grainSizeMilliseconds
        )
        || !exactlyEqual(
            actual.appliedParameters.positionNormalized,
            expected.appliedParameters.positionNormalized
        )
        || !exactlyEqual(
            actual.appliedParameters.pitchSemitones,
            expected.appliedParameters.pitchSemitones
        )
        || actual.appliedParameters.reverse
            != expected.appliedParameters.reverse
        || actual.appliedParameters.character
            != expected.appliedParameters.character
        || !exactlyEqual(
            actual.appliedParameters.pitchRandomAmount,
            expected.appliedParameters.pitchRandomAmount
        )
        || !exactlyEqual(
            actual.appliedParameters.positionRandomAmount,
            expected.appliedParameters.positionRandomAmount
        )
        || !exactlyEqual(
            actual.appliedParameters.timingJitterAmount,
            expected.appliedParameters.timingJitterAmount
        ))
    {
        return false;
    }

    for (std::size_t index = 0; index < actual.waveformPoints.size(); ++index)
    {
        for (std::size_t channel = 0; channel < 2; ++channel)
        {
            if (!exactlyEqual(
                    actual.waveformPoints[index].minimum[channel],
                    expected.waveformPoints[index].minimum[channel]
                )
                || !exactlyEqual(
                    actual.waveformPoints[index].maximum[channel],
                    expected.waveformPoints[index].maximum[channel]
                ))
            {
                return false;
            }
        }
    }

    for (std::size_t index = 0; index < actual.grains.size(); ++index)
    {
        const auto& left = actual.grains[index];
        const auto& right = expected.grains[index];

        if (!exactlyEqual(left.readPosition, right.readPosition)
            || !exactlyEqual(left.playbackRate, right.playbackRate)
            || left.sourceOldest != right.sourceOldest
            || left.sourceNewestExclusive != right.sourceNewestExclusive
            || left.durationFrames != right.durationFrames
            || left.renderedFrames != right.renderedFrames)
        {
            return false;
        }
    }

    return true;
}

void testEmptyAndSingleSnapshot(TestContext& context)
{
    GranularTelemetryQueue queue;
    GranularTelemetrySnapshot output;
    context.expect(!queue.tryPop(output), "new queue should be empty");

    const auto input = makeSnapshot(1);
    context.expect(queue.tryPush(input), "single snapshot should be accepted");
    context.expect(queue.tryPop(output), "single snapshot should be readable");
    context.expect(payloadMatches(output, input), "single payload should remain intact");
    context.expect(!queue.tryPop(output), "queue should be empty after pop");
}

void testCapacityFifoAndReuse(TestContext& context)
{
    GranularTelemetryQueue queue;

    for (std::uint64_t sequence = 0;
         sequence < granularTelemetryQueueCapacity;
         ++sequence)
    {
        context.expect(queue.tryPush(makeSnapshot(sequence)), "all eight slots should fill");
    }

    context.expect(
        !queue.tryPush(makeSnapshot(8)),
        "ninth snapshot should be dropped while full"
    );

    GranularTelemetrySnapshot output;
    for (std::uint64_t sequence = 0;
         sequence < granularTelemetryQueueCapacity;
         ++sequence)
    {
        context.expect(queue.tryPop(output), "filled snapshot should pop");
        context.expect(output.sequence == sequence, "snapshots should remain FIFO");
    }

    context.expect(queue.tryPush(makeSnapshot(9)), "freed slot should be reusable");
    context.expect(queue.tryPop(output), "reused slot should be readable");
    context.expect(output.sequence == 9, "reused slot should contain new payload");
}

void testRepeatedWraparound(TestContext& context)
{
    GranularTelemetryQueue queue;
    GranularTelemetrySnapshot output;
    constexpr std::uint64_t cycleCount = 1000;

    for (std::uint64_t cycle = 0; cycle < cycleCount; ++cycle)
    {
        for (std::uint64_t offset = 0;
             offset < granularTelemetryQueueCapacity;
             ++offset)
        {
            const auto sequence = cycle * granularTelemetryQueueCapacity + offset;
            context.expect(queue.tryPush(makeSnapshot(sequence)), "cycle push should succeed");
        }

        for (std::uint64_t offset = 0;
             offset < granularTelemetryQueueCapacity;
             ++offset)
        {
            const auto sequence = cycle * granularTelemetryQueueCapacity + offset;
            context.expect(queue.tryPop(output), "cycle pop should succeed");
            context.expect(output.sequence == sequence, "cycle order should remain FIFO");
        }
    }
}

void testFullPayloadPreservation(TestContext& context)
{
    GranularTelemetryQueue queue;
    const auto expected = makeSnapshot(123);
    GranularTelemetrySnapshot actual;

    context.expect(queue.tryPush(expected), "payload snapshot should push");
    context.expect(queue.tryPop(actual), "payload snapshot should pop");
    context.expect(
        payloadMatches(actual, expected),
        "all scalar, waveform and grain fields should be preserved"
    );
}

void testLongSequentialOperation(TestContext& context)
{
    GranularTelemetryQueue queue;
    GranularTelemetrySnapshot output;
    constexpr std::uint64_t operationCount = 100000;

    for (std::uint64_t sequence = 0; sequence < operationCount; ++sequence)
    {
        context.expect(queue.tryPush(makeSnapshot(sequence)), "sequential push should succeed");
        context.expect(queue.tryPop(output), "sequential pop should succeed");
        context.expect(output.sequence == sequence, "long sequence should preserve order");
    }
}

void testConcurrentProducerConsumer(TestContext& context)
{
    GranularTelemetryQueue queue;
    constexpr std::uint64_t publicationCount = 100000;
    std::atomic<bool> payloadValid { true };
    std::atomic<std::uint64_t> consumedCount { 0 };

    std::thread producer(
        [&]()
        {
            for (std::uint64_t sequence = 0; sequence < publicationCount; ++sequence)
            {
                const auto snapshot = makeSnapshot(sequence);
                while (!queue.tryPush(snapshot))
                    std::this_thread::yield();
            }
        }
    );

    std::thread consumer(
        [&]()
        {
            GranularTelemetrySnapshot output;
            std::uint64_t expectedSequence = 0;

            while (expectedSequence < publicationCount)
            {
                if (!queue.tryPop(output))
                {
                    std::this_thread::yield();
                    continue;
                }

                if (output.sequence != expectedSequence
                    || !payloadMatches(output, makeSnapshot(expectedSequence)))
                {
                    payloadValid.store(false, std::memory_order_relaxed);
                }

                ++expectedSequence;
            }

            consumedCount.store(expectedSequence, std::memory_order_relaxed);
        }
    );

    producer.join();
    consumer.join();

    context.expect(
        consumedCount.load(std::memory_order_relaxed) == publicationCount,
        "concurrent consumer should receive every accepted publication"
    );
    context.expect(
        payloadValid.load(std::memory_order_relaxed),
        "concurrent payload should remain ordered and uncorrupted"
    );
}
}

int main()
{
    TestContext context;

    testEmptyAndSingleSnapshot(context);
    testCapacityFifoAndReuse(context);
    testRepeatedWraparound(context);
    testFullPayloadPreservation(context);
    testLongSequentialOperation(context);
    testConcurrentProducerConsumer(context);

    if (context.getFailures() == 0)
    {
        std::cout << "All GranularTelemetry tests passed.\n";
        return 0;
    }

    std::cerr << context.getFailures() << " test(s) failed.\n";
    return 1;
}
