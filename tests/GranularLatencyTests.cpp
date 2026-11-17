#include "core/ProcessSpec.h"
#include "dsp/granular/CircularAudioBuffer.h"
#include "dsp/granular/GranularEngine.h"
#include "dsp/granular/GranularTelemetry.h"
#include "dsp/granular/GrainRenderer.h"
#include "dsp/granular/GrainScheduler.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <new>
#include <numbers>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// This executable is deliberately independent of the plugin target.  It is a
// diagnostic mirror around the public granular classes and does not alter the
// production engine or expose test-only counters from the audio path.
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

void* operator new[](std::size_t size)
{
    return ::operator new(size);
}

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
using hypercolor::dsp::granular::GranularAudioBlockView;
using hypercolor::dsp::granular::GranularEngine;
using hypercolor::dsp::granular::GranularEngineParameters;
using hypercolor::dsp::granular::GranularEngineProcessResult;
using hypercolor::dsp::granular::GranularTelemetrySnapshot;
using hypercolor::dsp::granular::GrainRenderer;
using hypercolor::dsp::granular::GrainScheduler;
using hypercolor::dsp::granular::GrainSchedulerParameters;
using hypercolor::dsp::granular::GrainSchedulerStepResult;
using hypercolor::dsp::granular::GrainPool;

constexpr double kEpsilon = 1.0e-12;
constexpr double kMeaningfulFraction = 1.0e-3;

enum class SignalKind
{
    impulse,
    drumTransient,
    ramp,
    periodic,
    silenceThenSignal
};

struct MirrorCounts
{
    std::size_t eventAttempts = 0;
    std::size_t started = 0;
    std::size_t insufficientHistory = 0;
    std::size_t rejected = 0;
    std::size_t firstEvent = std::numeric_limits<std::size_t>::max();
    std::size_t firstStarted = std::numeric_limits<std::size_t>::max();
    std::size_t firstInsufficient = std::numeric_limits<std::size_t>::max();
    std::size_t lastEvent = std::numeric_limits<std::size_t>::max();
    std::size_t firstEventToStart = std::numeric_limits<std::size_t>::max();
    std::size_t maximumEventToStart = 0;
};

struct Measurement
{
    std::string signal;
    double sampleRate = 0.0;
    std::size_t blockSize = 0;
    std::size_t channels = 0;
    double grainSizeMs = 0.0;
    double density = 0.0;
    double position = 0.0;
    double pitch = 0.0;
    bool reverse = false;
    std::size_t samples = 0;
    std::size_t firstNonzero = std::numeric_limits<std::size_t>::max();
    std::size_t firstMeaningful = std::numeric_limits<std::size_t>::max();
    std::size_t correlationLag = std::numeric_limits<std::size_t>::max();
    double correlation = 0.0;
    double sourceAgeSeconds = std::numeric_limits<double>::quiet_NaN();
    std::size_t eventAttempts = 0;
    std::size_t startedGrains = 0;
    std::size_t insufficientHistory = 0;
    std::size_t rejectedEvents = 0;
    std::size_t firstEvent = std::numeric_limits<std::size_t>::max();
    std::size_t firstStarted = std::numeric_limits<std::size_t>::max();
    std::size_t firstInsufficient = std::numeric_limits<std::size_t>::max();
    std::size_t firstEventToStart = std::numeric_limits<std::size_t>::max();
    std::size_t maximumEventToStart = 0;
    std::uint64_t pendingOverflow = 0;
    std::uint64_t pendingExpired = 0;
    std::uint64_t pendingStale = 0;
    std::size_t allocationsAfterPrepare = 0;
    bool finite = true;
    bool processSucceeded = true;
};

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

    [[nodiscard]] int failuresCount() const noexcept { return failures; }

private:
    int failures = 0;
};

std::string_view signalName(SignalKind signal) noexcept
{
    switch (signal)
    {
        case SignalKind::impulse: return "impulse";
        case SignalKind::drumTransient: return "drum";
        case SignalKind::ramp: return "ramp";
        case SignalKind::periodic: return "periodic";
        case SignalKind::silenceThenSignal: return "silence_then_signal";
    }

    return "unknown";
}

float makeSignalSample(
    SignalKind signal,
    std::size_t sample,
    double sampleRate
) noexcept
{
    const auto time = static_cast<double>(sample) / sampleRate;

    switch (signal)
    {
        case SignalKind::impulse:
            return sample == 0 ? 1.0f : 0.0f;

        case SignalKind::drumTransient:
        {
            if (time > 0.12)
                return 0.0f;

            const auto envelope = std::exp(-34.0 * time);
            const auto body = std::sin(2.0 * std::numbers::pi * 173.0 * time);
            const auto click = sample < 5 ? 0.8 : 0.0;
            return static_cast<float>(envelope * (0.8 * body + click));
        }

        case SignalKind::ramp:
        {
            constexpr std::size_t period = 2048;
            const auto phase = static_cast<double>(sample % period)
                / static_cast<double>(period - 1);
            return static_cast<float>(phase * 2.0 - 1.0);
        }

        case SignalKind::periodic:
            return static_cast<float>(
                0.7 * std::sin(2.0 * std::numbers::pi * 271.0 * time)
                + 0.25 * std::sin(2.0 * std::numbers::pi * 997.0 * time)
            );

        case SignalKind::silenceThenSignal:
            if (time < 0.1)
                return 0.0f;
            return static_cast<float>(
                0.7 * std::sin(2.0 * std::numbers::pi * 271.0 * (time - 0.1))
                * std::exp(-2.0 * (time - 0.1))
            );
    }

    return 0.0f;
}

GranularEngineParameters makeParameters(
    double grainSizeMs,
    double density,
    double position,
    double pitch,
    bool reverse
)
{
    GranularEngineParameters parameters;
    parameters.grainSizeMilliseconds = grainSizeMs;
    parameters.densityGrainsPerSecond = density;
    parameters.positionNormalized = position;
    parameters.pitchSemitones = pitch;
    parameters.reverse = reverse;
    return parameters;
}

GranularAudioBlockView makeBlock(
    std::span<const float> leftInput,
    std::span<const float> rightInput,
    std::span<float> leftOutput,
    std::span<float> rightOutput,
    std::size_t channels
)
{
    GranularAudioBlockView block;
    block.inputChannels[0] = leftInput;
    block.outputChannels[0] = leftOutput;
    block.numChannels = static_cast<std::uint32_t>(channels);

    if (channels == 2)
    {
        block.inputChannels[1] = rightInput;
        block.outputChannels[1] = rightOutput;
    }

    return block;
}

bool finiteVector(std::span<const float> values) noexcept
{
    return std::all_of(values.begin(), values.end(),
        [](float value) { return std::isfinite(value); });
}

double absSample(
    const std::vector<std::vector<float>>& values,
    std::size_t index
) noexcept
{
    auto peak = 0.0;
    for (const auto& channel : values)
        peak = std::max(peak, std::abs(static_cast<double>(channel[index])));
    return peak;
}

// A bounded, deliberately coarse normalized cross-correlation.  Granular
// output is a resynthesized cloud rather than a delayed copy, so this metric
// is an orientation aid, not an assertion of a single fixed latency.
std::pair<std::size_t, double> estimateCorrelation(
    const std::vector<float>& input,
    const std::vector<float>& output,
    std::size_t maximumLag
)
{
    const auto length = std::min(input.size(), output.size());
    if (length < 8)
        return { std::numeric_limits<std::size_t>::max(), 0.0 };

    const auto stride = std::max<std::size_t>(1, length / 512);
    const auto lagStride = stride;
    const auto mean = [](const std::vector<float>& values,
                         std::size_t start,
                         std::size_t count,
                         std::size_t step)
    {
        auto sum = 0.0;
        auto used = std::size_t { 0 };
        for (std::size_t index = start; index < values.size()
             && used < count; index += step, ++used)
            sum += values[index];
        return used == 0 ? 0.0 : sum / static_cast<double>(used);
    };

    auto bestLag = std::numeric_limits<std::size_t>::max();
    auto best = 0.0;

    for (std::size_t lag = 0; lag <= maximumLag && lag < length;
         lag += lagStride)
    {
        const auto count = (length - lag + stride - 1) / stride;
        const auto inputMean = mean(input, lag, count, stride);
        const auto outputMean = mean(output, 0, count, stride);
        auto numerator = 0.0;
        auto inputEnergy = 0.0;
        auto outputEnergy = 0.0;
        auto used = std::size_t { 0 };

        for (std::size_t outputIndex = 0;
             outputIndex < length - lag && used < count;
             outputIndex += stride, ++used)
        {
            const auto inputIndex = outputIndex + lag;
            const auto inputValue = static_cast<double>(input[inputIndex])
                - inputMean;
            const auto outputValue = static_cast<double>(output[outputIndex])
                - outputMean;
            numerator += inputValue * outputValue;
            inputEnergy += inputValue * inputValue;
            outputEnergy += outputValue * outputValue;
        }

        if (inputEnergy <= 0.0 || outputEnergy <= 0.0)
            continue;

        const auto correlation = std::abs(
            numerator / std::sqrt(inputEnergy * outputEnergy)
        );
        if (correlation > best)
        {
            best = correlation;
            bestLag = lag;
        }
    }

    return { bestLag, best };
}

void updateTelemetryAge(
    const GranularTelemetrySnapshot& snapshot,
    double& firstAgeSeconds,
    double sampleRate
)
{
    if (snapshot.activeGrainCount == 0)
        return;

    for (std::size_t index = 0;
         index < std::min<std::size_t>(
             snapshot.activeGrainCount,
             snapshot.grains.size()
         );
         ++index)
    {
        const auto& grain = snapshot.grains[index];
        if (grain.sourceNewestExclusive == 0
            || !std::isfinite(grain.readPosition))
            continue;

        const auto age = static_cast<double>(grain.sourceNewestExclusive)
            - grain.readPosition;
        if (age >= 0.0 && std::isfinite(age))
        {
            if (!std::isfinite(firstAgeSeconds))
                firstAgeSeconds = age / sampleRate;
        }
    }
}

void mirrorOneSample(
    double sampleRate,
    std::size_t sample,
    std::size_t channels,
    const std::array<float, 2>& input,
    const GrainSchedulerParameters& parameters,
    GrainScheduler& scheduler,
    CircularAudioBuffer& source,
    GrainPool& pool,
    GrainRenderer& renderer,
    MirrorCounts& counts
)
{
    static_cast<void>(source.writeFrame(
        std::span<const float> { input.data(), channels }
    ));
    const auto result = scheduler.processSample(parameters, source, pool);
    if (result != GrainSchedulerStepResult::noEvent)
    {
        ++counts.eventAttempts;
        counts.lastEvent = sample;
        if (counts.firstEvent == std::numeric_limits<std::size_t>::max())
            counts.firstEvent = sample;
    }

    if (result == GrainSchedulerStepResult::started)
    {
        ++counts.started;
        if (counts.lastEvent != std::numeric_limits<std::size_t>::max())
        {
            const auto delay = sample - counts.lastEvent;
            counts.firstEventToStart = counts.firstEventToStart
                == std::numeric_limits<std::size_t>::max()
                ? delay
                : counts.firstEventToStart;
            counts.maximumEventToStart
                = std::max(counts.maximumEventToStart, delay);
        }
        if (counts.firstStarted == std::numeric_limits<std::size_t>::max())
            counts.firstStarted = sample;
    }
    else if (result == GrainSchedulerStepResult::insufficientHistory)
    {
        ++counts.insufficientHistory;
        if (counts.firstInsufficient
            == std::numeric_limits<std::size_t>::max())
            counts.firstInsufficient = sample;
    }
    else if (result != GrainSchedulerStepResult::noEvent)
    {
        ++counts.rejected;
    }

    std::array<float, 2> mirrorOutput {};
    static_cast<void>(renderer.renderSample(
        source,
        pool,
        std::span<float> { mirrorOutput.data(), channels }
    ));
    pool.reclaimFinished();
    static_cast<void>(sampleRate);
}

Measurement runScenario(
    double sampleRate,
    std::size_t blockSize,
    std::size_t channels,
    const GranularEngineParameters& parameters,
    SignalKind signal,
    double observationSeconds,
    double prefillSeconds = 0.0,
    std::span<const std::pair<std::size_t, GranularEngineParameters>>
        parameterSchedule = {}
)
{
    const auto totalSamples = static_cast<std::size_t>(
        std::ceil((prefillSeconds + observationSeconds) * sampleRate)
    );
    const auto prefillSamples = static_cast<std::size_t>(
        std::ceil(prefillSeconds * sampleRate)
    );

    Measurement measurement;
    measurement.signal = std::string(signalName(signal));
    measurement.sampleRate = sampleRate;
    measurement.blockSize = blockSize;
    measurement.channels = channels;
    measurement.grainSizeMs = parameters.grainSizeMilliseconds;
    measurement.density = parameters.densityGrainsPerSecond;
    measurement.position = parameters.positionNormalized;
    measurement.pitch = parameters.pitchSemitones;
    measurement.reverse = parameters.reverse;
    measurement.samples = totalSamples - prefillSamples;

    const ProcessSpec spec {
        sampleRate,
        static_cast<std::uint32_t>(blockSize),
        static_cast<std::uint32_t>(channels)
    };
    GranularEngine engine;
    CircularAudioBuffer mirrorSource;
    GrainScheduler mirrorScheduler;
    GrainPool mirrorPool;
    GrainRenderer mirrorRenderer;
    const auto prepared = engine.prepare(spec)
        && mirrorSource.prepare(
            spec,
            CircularAudioBuffer::productMaximumDurationSeconds,
            CircularAudioBuffer::defaultLogicalDurationSeconds
        )
        && mirrorScheduler.prepare(spec);

    if (!prepared)
    {
        measurement.processSucceeded = false;
        return measurement;
    }

    std::vector<std::vector<float>> input(
        channels,
        std::vector<float>(totalSamples, 0.0f)
    );
    std::vector<std::vector<float>> output(
        channels,
        std::vector<float>(totalSamples, 0.0f)
    );

    for (std::size_t sample = 0; sample < totalSamples; ++sample)
    {
        const auto generated = makeSignalSample(signal, sample, sampleRate);
        input[0][sample] = generated;
        if (channels == 2)
            input[1][sample] = -generated * 0.73f;
    }

    GranularTelemetrySnapshot snapshot;
    auto sourceAge = std::numeric_limits<double>::quiet_NaN();
    MirrorCounts mirrorCounts;
    auto processResult = GranularEngineProcessResult::success;
    std::size_t scheduleIndex = 0;

    allocationProbe::count.store(0, std::memory_order_relaxed);
    allocationProbe::enabled.store(true, std::memory_order_release);

    for (std::size_t blockStart = 0;
         blockStart < totalSamples;
         blockStart += blockSize)
    {
        const auto numSamples = std::min(blockSize, totalSamples - blockStart);
        while (scheduleIndex + 1 < parameterSchedule.size()
               && parameterSchedule[scheduleIndex + 1].first <= blockStart)
            ++scheduleIndex;
        const auto& blockParameters = parameterSchedule.empty()
            ? parameters
            : parameterSchedule[scheduleIndex].second;
        std::array<std::span<const float>, 2> inputSpans;
        std::array<std::span<float>, 2> outputSpans;
        inputSpans[0] = std::span<const float> {
            input[0].data() + blockStart, numSamples
        };
        outputSpans[0] = std::span<float> {
            output[0].data() + blockStart, numSamples
        };
        if (channels == 2)
        {
            inputSpans[1] = std::span<const float> {
                input[1].data() + blockStart, numSamples
            };
            outputSpans[1] = std::span<float> {
                output[1].data() + blockStart, numSamples
            };
        }

        const auto block = makeBlock(
            inputSpans[0],
            inputSpans[1],
            outputSpans[0],
            outputSpans[1],
            channels
        );
        const auto result = engine.processBlock(block, blockParameters);
        if (result != GranularEngineProcessResult::success)
            processResult = result;

        for (std::size_t offset = 0; offset < numSamples; ++offset)
        {
            const auto absoluteSample = blockStart + offset;
            std::array<float, 2> inputFrame { input[0][absoluteSample], 0.0f };
            if (channels == 2)
                inputFrame[1] = input[1][absoluteSample];
            mirrorOneSample(
                sampleRate,
                absoluteSample,
                channels,
                inputFrame,
                blockParameters,
                mirrorScheduler,
                mirrorSource,
                mirrorPool,
                mirrorRenderer,
                mirrorCounts
            );
        }

        if (engine.copyTelemetrySnapshot(snapshot)
            && blockStart + numSamples >= prefillSamples)
            updateTelemetryAge(snapshot, sourceAge, sampleRate);
    }

    allocationProbe::enabled.store(false, std::memory_order_release);
    measurement.allocationsAfterPrepare = allocationProbe::count.load(
        std::memory_order_relaxed
    );
    measurement.processSucceeded
        = processResult != GranularEngineProcessResult::invalidConfiguration;
    measurement.finite = true;
    for (const auto& channel : output)
        measurement.finite = measurement.finite
            && finiteVector(std::span<const float> { channel });

    const auto observationStart = prefillSamples;
    const auto observationEnd = totalSamples;
    auto peak = 0.0;
    for (std::size_t sample = observationStart;
         sample < observationEnd;
         ++sample)
        peak = std::max(peak, absSample(output, sample));

    const auto meaningfulThreshold = peak * kMeaningfulFraction;
    for (std::size_t sample = observationStart;
         sample < observationEnd;
         ++sample)
    {
        const auto value = absSample(output, sample);
        if (measurement.firstNonzero == std::numeric_limits<std::size_t>::max()
            && value > kEpsilon)
            measurement.firstNonzero = sample - observationStart;
        if (measurement.firstMeaningful
                == std::numeric_limits<std::size_t>::max()
            && peak > kEpsilon
            && value >= meaningfulThreshold)
            measurement.firstMeaningful = sample - observationStart;
    }

    std::vector<float> inputObservation(
        input[0].begin() + static_cast<std::ptrdiff_t>(observationStart),
        input[0].begin() + static_cast<std::ptrdiff_t>(observationEnd)
    );
    std::vector<float> outputObservation(
        output[0].begin() + static_cast<std::ptrdiff_t>(observationStart),
        output[0].begin() + static_cast<std::ptrdiff_t>(observationEnd)
    );
    const auto correlation = estimateCorrelation(
        inputObservation,
        outputObservation,
        std::min<std::size_t>(
            inputObservation.size() > 0
                ? inputObservation.size() - 1
                : 0,
            static_cast<std::size_t>(2.0 * sampleRate)
        )
    );

    measurement.correlationLag = correlation.first;
    measurement.correlation = correlation.second;
    measurement.sourceAgeSeconds = sourceAge;
    measurement.eventAttempts = mirrorCounts.eventAttempts;
    measurement.startedGrains = mirrorCounts.started;
    measurement.insufficientHistory = mirrorCounts.insufficientHistory;
    measurement.rejectedEvents = mirrorCounts.rejected;
    measurement.firstEvent = mirrorCounts.firstEvent;
    measurement.firstStarted = mirrorCounts.firstStarted;
    measurement.firstInsufficient = mirrorCounts.firstInsufficient;
    measurement.firstEventToStart = mirrorCounts.firstEventToStart;
    measurement.maximumEventToStart = mirrorCounts.maximumEventToStart;
    measurement.pendingOverflow = mirrorScheduler.getPendingOverflowCount();
    measurement.pendingExpired = mirrorScheduler.getPendingExpiredCount();
    measurement.pendingStale = mirrorScheduler.getPendingStaleCount();
    return measurement;
}

std::string csvValue(std::size_t value)
{
    return value == std::numeric_limits<std::size_t>::max()
        ? ""
        : std::to_string(value);
}

void writeCsv(
    const std::string& path,
    const std::vector<Measurement>& measurements
)
{
    std::ofstream csv(path);
    csv << "signal,sample_rate,block_size,channels,grain_size_ms,density,"
           "position,pitch,reverse,samples,first_nonzero,first_meaningful,"
           "correlation_lag,correlation,source_age_seconds,event_attempts,"
           "started_grains,insufficient_history,rejected_events,first_event,"
           "first_started,first_insufficient,first_event_to_start,"
           "maximum_event_to_start,pending_overflow,pending_expired,pending_stale,"
           "allocations_after_prepare,"
           "finite,process_succeeded\n";
    csv << std::setprecision(12);
    for (const auto& measurement : measurements)
    {
        csv << measurement.signal << ','
            << measurement.sampleRate << ','
            << measurement.blockSize << ','
            << measurement.channels << ','
            << measurement.grainSizeMs << ','
            << measurement.density << ','
            << measurement.position << ','
            << measurement.pitch << ','
            << (measurement.reverse ? 1 : 0) << ','
            << measurement.samples << ','
            << csvValue(measurement.firstNonzero) << ','
            << csvValue(measurement.firstMeaningful) << ','
            << csvValue(measurement.correlationLag) << ','
            << measurement.correlation << ','
            << (std::isfinite(measurement.sourceAgeSeconds)
                    ? std::to_string(measurement.sourceAgeSeconds) : "") << ','
            << measurement.eventAttempts << ','
            << measurement.startedGrains << ','
            << measurement.insufficientHistory << ','
            << measurement.rejectedEvents << ','
            << csvValue(measurement.firstEvent) << ','
            << csvValue(measurement.firstStarted) << ','
            << csvValue(measurement.firstInsufficient) << ','
            << csvValue(measurement.firstEventToStart) << ','
            << measurement.maximumEventToStart << ','
            << measurement.pendingOverflow << ','
            << measurement.pendingExpired << ','
            << measurement.pendingStale << ','
            << measurement.allocationsAfterPrepare << ','
            << (measurement.finite ? 1 : 0) << ','
            << (measurement.processSucceeded ? 1 : 0) << '\n';
    }
}

void appendCoreCases(std::vector<Measurement>& measurements)
{
    const auto add = [&](double sampleRate,
                         std::size_t blockSize,
                         std::size_t channels,
                         double grainSize,
                         double density,
                         double position,
                         double pitch,
                         bool reverse,
                         SignalKind signal,
                         double seconds,
                         double prefill = 0.0)
    {
        measurements.push_back(runScenario(
            sampleRate,
            blockSize,
            channels,
            makeParameters(grainSize, density, position, pitch, reverse),
            signal,
            seconds,
            prefill
        ));
    };

    // Grain-size and density boundaries, with impulse and transient inputs.
    for (const auto grainSize : { 10.0, 50.0, 100.0, 250.0, 500.0 })
    {
        for (const auto density : { 1.0, 20.0, 120.0 })
        {
            const auto seconds = std::max(0.45, 1.25 / density + 0.25);
            add(44100.0, 64, 1, grainSize, density, 0.0, 0.0,
                false, SignalKind::impulse, seconds);
        }
    }

    // Non-standard blocks, stereo, and all supported sample-rate families.
    for (const auto sampleRate : { 44100.0, 48000.0, 88200.0, 96000.0 })
    {
        add(sampleRate, 1, 1, 100.0, 20.0, 0.0, 0.0,
            false, SignalKind::drumTransient, 0.55);
        add(sampleRate, 7, 2, 100.0, 20.0, 0.0, 0.0,
            false, SignalKind::periodic, 0.55);
        add(sampleRate, 257, 1, 100.0, 20.0, 0.0, 0.0,
            false, SignalKind::silenceThenSignal, 0.55);
        add(sampleRate, 512, 2, 500.0, 120.0, 0.0, 0.0,
            false, SignalKind::periodic, 0.55);
    }

    for (const auto blockSize : { std::size_t { 32 }, std::size_t { 64 },
                                  std::size_t { 257 }, std::size_t { 512 } })
    {
        add(48000.0, blockSize, 1, 100.0, 20.0, 0.0, 0.0,
            false, SignalKind::ramp, 0.7);
    }

    // Position, pitch, and reverse are sampled independently after the
    // logical history is full, where source-age telemetry is meaningful.
    for (const auto position : { 0.0, 0.25, 0.5, 0.75, 1.0 })
    {
        add(48000.0, 64, 1, 100.0, 20.0, position, 0.0,
            false, SignalKind::ramp, 0.55, 4.25);
    }
    for (const auto pitch : { -24.0, -12.0, 0.0, 12.0, 24.0 })
    {
        add(48000.0, 64, 1, 100.0, 20.0, 0.5, pitch,
            false, SignalKind::periodic, 0.55, 4.25);
    }
    for (const auto reverse : { false, true })
    {
        add(48000.0, 64, 2, 100.0, 20.0, 0.5, 0.0,
            reverse, SignalKind::periodic, 0.55, 4.25);
    }

    // Silence-to-signal and ramp cases make scheduler starvation visible
    // without changing any production telemetry or scheduler code.
    add(48000.0, 64, 1, 500.0, 1.0, 1.0, 0.0,
        false, SignalKind::silenceThenSignal, 1.5);

    // Parameter transitions are applied only at processBlock boundaries,
    // matching the real processor contract.  This covers density, grain
    // size, position, pitch, and reverse changes while the input is live.
    const auto dynamicStart = makeParameters(100.0, 20.0, 0.0, 0.0, false);
    const std::array<std::pair<std::size_t, GranularEngineParameters>, 4>
        dynamicSchedule {
            std::pair { std::size_t { 0 }, dynamicStart },
            std::pair { std::size_t { 8192 },
                        makeParameters(10.0, 120.0, 0.25, 12.0, false) },
            std::pair { std::size_t { 16384 },
                        makeParameters(500.0, 1.0, 0.75, -24.0, true) },
            std::pair { std::size_t { 24576 },
                        makeParameters(50.0, 20.0, 0.5, 24.0, false) }
        };
    auto dynamicPeriodic = runScenario(
        48000.0,
        257,
        1,
        dynamicStart,
        SignalKind::periodic,
        0.8,
        0.0,
        dynamicSchedule
    );
    dynamicPeriodic.signal = "dynamic_transition_periodic";
    measurements.push_back(std::move(dynamicPeriodic));

    auto dynamicRamp = runScenario(
        96000.0,
        7,
        2,
        dynamicStart,
        SignalKind::ramp,
        0.5,
        0.0,
        dynamicSchedule
    );
    dynamicRamp.signal = "dynamic_transition_ramp";
    measurements.push_back(std::move(dynamicRamp));
}

void printSummary(const std::vector<Measurement>& measurements)
{
    auto count = std::size_t { 0 };
    auto finite = true;
    auto successful = true;
    auto allocations = std::size_t { 0 };
    for (const auto& measurement : measurements)
    {
        count += measurement.startedGrains;
        finite = finite && measurement.finite;
        successful = successful && measurement.processSucceeded;
        allocations += measurement.allocationsAfterPrepare;
    }

    std::cout << "Granular latency harness: " << measurements.size()
              << " scenarios, " << count << " started grains\n";
    std::cout << "Finite output: " << (finite ? "yes" : "NO")
              << "; process configurations valid: "
              << (successful ? "yes" : "NO") << '\n';
    std::cout << "Allocations after prepare (all scenarios): "
              << allocations << '\n';
    std::cout << "Columns first_nonzero/first_meaningful are samples from the "
                 "observation window; source_age is telemetry-based.\n";
}
}

int main(int argc, char** argv)
{
    const std::string csvPath = argc > 1
        ? argv[1]
        : "granular_latency_measurements.csv";

    std::vector<Measurement> measurements;
    appendCoreCases(measurements);
    writeCsv(csvPath, measurements);
    printSummary(measurements);
    std::cout << "CSV: " << csvPath << '\n';

    TestContext context;
    context.expect(!measurements.empty(), "diagnostic matrix should not be empty");
    for (const auto& measurement : measurements)
    {
        context.expect(measurement.finite, "granular output must remain finite");
        context.expect(
            measurement.processSucceeded,
            "granular process must accept every diagnostic block"
        );
        context.expect(
            measurement.allocationsAfterPrepare == 0,
            "no allocations are allowed after prepare"
        );
    }

    if (context.failuresCount() != 0)
        return 1;

    std::cout << "Granular latency harness passed.\n";
    return 0;
}
