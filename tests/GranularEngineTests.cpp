#include "core/ProcessSpec.h"
#include "dsp/granular/GranularEngine.h"

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
#include <utility>
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

void* operator new[](std::size_t size)
{
    return ::operator new(size);
}

void operator delete(void* memory) noexcept
{
    std::free(memory);
}

void operator delete[](void* memory) noexcept
{
    ::operator delete(memory);
}

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
using hypercolor::dsp::granular::GranularAudioBlockView;
using hypercolor::dsp::granular::GranularEngine;
using hypercolor::dsp::granular::GranularEngineParameters;
using hypercolor::dsp::granular::GranularEngineProcessResult;

constexpr double defaultSampleRate = 44100.0;

static_assert(noexcept(std::declval<GranularEngine&>().reset()));
static_assert(noexcept(std::declval<const GranularEngine&>().isPrepared()));
static_assert(noexcept(
    std::declval<GranularEngine&>().processBlock(
        std::declval<const GranularAudioBlockView&>(),
        std::declval<const GranularEngineParameters&>()
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
    std::uint32_t maximumBlockSize = 64,
    std::uint32_t numChannels = 1
)
{
    return { sampleRate, maximumBlockSize, numChannels };
}

GranularEngineParameters makeParameters(
    double density = 0.0,
    double grainSizeMilliseconds = 10.0,
    double position = 0.0,
    double pitch = 0.0,
    bool reverse = false
)
{
    GranularEngineParameters parameters;
    parameters.densityGrainsPerSecond = density;
    parameters.grainSizeMilliseconds = grainSizeMilliseconds;
    parameters.positionNormalized = position;
    parameters.pitchSemitones = pitch;
    parameters.reverse = reverse;
    return parameters;
}

GranularAudioBlockView makeMonoBlock(
    std::span<const float> input,
    std::span<float> output
)
{
    GranularAudioBlockView block;
    block.inputChannels[0] = input;
    block.outputChannels[0] = output;
    block.numChannels = 1;
    return block;
}

GranularAudioBlockView makeStereoBlock(
    std::span<const float> leftInput,
    std::span<const float> rightInput,
    std::span<float> leftOutput,
    std::span<float> rightOutput
)
{
    GranularAudioBlockView block;
    block.inputChannels[0] = leftInput;
    block.inputChannels[1] = rightInput;
    block.outputChannels[0] = leftOutput;
    block.outputChannels[1] = rightOutput;
    block.numChannels = 2;
    return block;
}

bool allFinite(std::span<const float> values)
{
    return std::all_of(
        values.begin(),
        values.end(),
        [](float value) { return std::isfinite(value); }
    );
}

bool allZero(std::span<const float> values)
{
    return std::all_of(
        values.begin(),
        values.end(),
        [](float value) { return value == 0.0f; }
    );
}

GranularEngineProcessResult processMono(
    GranularEngine& engine,
    std::span<const float> input,
    std::span<float> output,
    const GranularEngineParameters& parameters
)
{
    return engine.processBlock(
        makeMonoBlock(input, output),
        parameters
    );
}

void testPreparationLifecycle(TestContext& context)
{
    for (const auto sampleRate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        for (const auto channels : { 1u, 2u })
        {
            GranularEngine engine;
            context.expect(
                engine.prepare(makeSpec(sampleRate, 64, channels)),
                "supported ProcessSpec should prepare"
            );
            context.expect(engine.isPrepared(), "successful prepare should set state");
            engine.reset();
            context.expect(engine.isPrepared(), "reset should preserve prepared state");
        }
    }

    for (const auto invalidSpec : {
             makeSpec(44099.0),
             makeSpec(192001.0),
             makeSpec(defaultSampleRate, 0),
             makeSpec(defaultSampleRate, 64, 0),
             makeSpec(defaultSampleRate, 64, 3),
             makeSpec(std::numeric_limits<double>::quiet_NaN()) })
    {
        GranularEngine engine;
        context.expect(!engine.prepare(invalidSpec), "invalid ProcessSpec should fail");
        context.expect(!engine.isPrepared(), "failed prepare should leave engine unprepared");
    }

    GranularEngine engine;
    context.expect(engine.prepare(makeSpec()), "initial lifecycle prepare should succeed");
    context.expect(
        !engine.prepare(makeSpec(defaultSampleRate, 64, 3)),
        "invalid reprepare should fail"
    );
    context.expect(!engine.isPrepared(), "invalid reprepare should discard old prepared state");

    std::array input { 1.0f };
    std::array output { 9.0f };
    context.expect(
        processMono(engine, input, output, makeParameters())
            == GranularEngineProcessResult::engineNotPrepared,
        "processing after failed reprepare should be rejected"
    );
    context.expect(output[0] == 9.0f, "unprepared processing should leave output unchanged");

    context.expect(
        engine.prepare(makeSpec(48000.0, 1024, 2)),
        "valid prepare after failure should recover"
    );
    context.expect(engine.isPrepared(), "recovered engine should be prepared");
}

void testEmptyAndInvalidBlocks(TestContext& context)
{
    GranularEngine engine;
    context.expect(engine.prepare(makeSpec()), "block-validation engine should prepare");

    GranularAudioBlockView emptyBlock;
    emptyBlock.numChannels = 1;
    context.expect(
        engine.processBlock(emptyBlock, makeParameters())
            == GranularEngineProcessResult::success,
        "empty valid block should be a successful no-op"
    );

    std::array input { 1.0f, 2.0f };
    std::array output { 7.0f, 8.0f };
    auto wrongChannels = makeMonoBlock(input, output);
    wrongChannels.numChannels = 2;
    context.expect(
        engine.processBlock(wrongChannels, makeParameters())
            == GranularEngineProcessResult::invalidConfiguration,
        "prepared channel mismatch should be rejected"
    );
    context.expect(
        output[0] == 7.0f && output[1] == 8.0f,
        "invalid channel configuration should leave output unchanged"
    );

    std::array shortOutput { 3.0f };
    context.expect(
        engine.processBlock(
            makeMonoBlock(input, shortOutput),
            makeParameters()
        ) == GranularEngineProcessResult::invalidConfiguration,
        "different input/output lengths should be rejected"
    );
    context.expect(shortOutput[0] == 3.0f, "length mismatch should not write output");

    GranularAudioBlockView zeroChannels;
    context.expect(
        engine.processBlock(zeroChannels, makeParameters())
            == GranularEngineProcessResult::invalidConfiguration,
        "zero channels should be rejected"
    );

    GranularEngine unprepared;
    output = { 5.0f, 6.0f };
    context.expect(
        processMono(unprepared, input, output, makeParameters())
            == GranularEngineProcessResult::engineNotPrepared,
        "unprepared engine should reject block before validation"
    );
    context.expect(
        output[0] == 5.0f && output[1] == 6.0f,
        "unprepared engine should leave output unchanged"
    );

    // A non-empty std::span with a null pointer violates std::span's own
    // constructor precondition, so that case cannot be formed by a valid C++
    // caller. GranularEngine still checks data() defensively.
}

struct StereoRender
{
    std::vector<float> left;
    std::vector<float> right;
    GranularEngineProcessResult result = GranularEngineProcessResult::success;
};

StereoRender renderStereo(bool inPlace)
{
    constexpr std::size_t frames = 5000;
    std::vector<float> left(frames);
    std::vector<float> right(frames);

    for (std::size_t frame = 0; frame < frames; ++frame)
    {
        left[frame] = static_cast<float>(frame % 997) / 997.0f;
        right[frame] = -0.5f * left[frame];
    }

    std::vector<float> leftOutput(frames, 0.0f);
    std::vector<float> rightOutput(frames, 0.0f);
    GranularEngine engine;

    if (!engine.prepare(makeSpec(defaultSampleRate, 17, 2)))
        return {};

    const auto parameters = makeParameters(120.0, 10.0, 0.5, 7.0, false);
    StereoRender rendered;

    if (inPlace)
    {
        rendered.result = engine.processBlock(
            makeStereoBlock(left, right, left, right),
            parameters
        );
        rendered.left = std::move(left);
        rendered.right = std::move(right);
    }
    else
    {
        rendered.result = engine.processBlock(
            makeStereoBlock(left, right, leftOutput, rightOutput),
            parameters
        );
        rendered.left = std::move(leftOutput);
        rendered.right = std::move(rightOutput);
    }

    return rendered;
}

void testInPlaceOutOfPlaceAndOversized(TestContext& context)
{
    const auto outOfPlace = renderStereo(false);
    const auto inPlace = renderStereo(true);
    context.expect(
        outOfPlace.result == GranularEngineProcessResult::success
            && inPlace.result == GranularEngineProcessResult::success,
        "in-place and out-of-place stereo renders should succeed"
    );
    context.expect(
        outOfPlace.left == inPlace.left && outOfPlace.right == inPlace.right,
        "in-place and out-of-place results should be identical"
    );
    context.expect(allFinite(inPlace.left), "in-place left output should stay finite");
    context.expect(allFinite(inPlace.right), "in-place right output should stay finite");

    GranularEngine oversizedEngine;
    context.expect(
        oversizedEngine.prepare(makeSpec(defaultSampleRate, 64, 1)),
        "oversized engine should prepare"
    );
    std::vector<float> input(4097, 1.0f);
    std::vector<float> output(4097, -1.0f);
    context.expect(
        processMono(oversizedEngine, input, output, makeParameters())
            == GranularEngineProcessResult::success,
        "4097-sample block should process despite a 64-sample ProcessSpec"
    );
    context.expect(allZero(output), "Density zero oversized block should be wet silence");
}

void testHistoryLaunchAndSingleGrain(TestContext& context)
{
    GranularEngine engine;
    context.expect(engine.prepare(makeSpec()), "history engine should prepare");

    std::vector<float> history(1000, 1.0f);
    std::vector<float> historyOutput(history.size(), -1.0f);
    context.expect(
        processMono(engine, history, historyOutput, makeParameters())
            == GranularEngineProcessResult::success,
        "Density zero history block should process"
    );
    context.expect(allZero(historyOutput), "history recording alone should produce no wet signal");

    std::vector<float> preEventInput(367, 0.0f);
    std::vector<float> preEventOutput(367, -1.0f);
    const auto eventParameters = makeParameters(120.0, 10.0, 1.0, 0.0, false);
    context.expect(
        processMono(engine, preEventInput, preEventOutput, eventParameters)
            == GranularEngineProcessResult::success,
        "pre-event block should process"
    );
    context.expect(allZero(preEventOutput), "first event should not occur before full phase");

    std::array eventInput { 0.0f };
    std::array eventOutput { -1.0f };
    context.expect(
        processMono(engine, eventInput, eventOutput, eventParameters)
            == GranularEngineProcessResult::success,
        "event sample should process"
    );
    context.expect(eventOutput[0] == 0.0f, "new grain should render zero Hann first frame");

    std::array nextInput { 0.0f };
    std::array nextOutput { -1.0f };
    context.expect(
        processMono(engine, nextInput, nextOutput, makeParameters())
            == GranularEngineProcessResult::success,
        "post-launch sample should process"
    );
    context.expect(
        nextOutput[0] > 0.0f,
        "grain should advance in launch sample and read recorded history next"
    );

    std::vector<float> remainderInput(439, 0.0f);
    std::vector<float> remainderOutput(439, -1.0f);
    context.expect(
        processMono(engine, remainderInput, remainderOutput, makeParameters())
            == GranularEngineProcessResult::success,
        "single grain remainder should process"
    );

    std::array silentInput { 0.0f };
    std::array silentOutput { -1.0f };
    context.expect(
        processMono(engine, silentInput, silentOutput, makeParameters())
            == GranularEngineProcessResult::success,
        "post-grain sample should process"
    );
    context.expect(silentOutput[0] == 0.0f, "finished grain should be reclaimed");
}

void testResetClearsRuntimeState(TestContext& context)
{
    GranularEngine engine;
    context.expect(engine.prepare(makeSpec()), "reset-state engine should prepare");

    std::vector<float> history(1000, 1.0f);
    std::vector<float> scratch(history.size());
    (void) processMono(engine, history, scratch, makeParameters());

    std::vector<float> launchInput(368, 1.0f);
    std::vector<float> launchOutput(368);
    (void) processMono(
        engine,
        launchInput,
        launchOutput,
        makeParameters(120.0, 10.0, 1.0, 0.0, false)
    );

    engine.reset();
    context.expect(engine.isPrepared(), "reset should retain prepared state");

    std::array input { 0.0f };
    std::array output { -1.0f };
    context.expect(
        processMono(engine, input, output, makeParameters())
            == GranularEngineProcessResult::success,
        "processing after reset should remain available"
    );
    context.expect(output[0] == 0.0f, "reset should remove active grains");

    std::vector<float> earlyInput(368, 1.0f);
    std::vector<float> earlyOutput(368, -1.0f);
    context.expect(
        processMono(
            engine,
            earlyInput,
            earlyOutput,
            makeParameters(120.0, 10.0, 1.0, 0.0, false)
        ) == GranularEngineProcessResult::success,
        "insufficient history after reset should be a normal startup state"
    );
    context.expect(allZero(earlyOutput), "reset should invalidate previous history");
}

void testOverlapAddAndRepeatedReclaim(TestContext& context)
{
    GranularEngine continuous;
    GranularEngine isolated;
    context.expect(continuous.prepare(makeSpec()), "continuous overlap engine prepare");
    context.expect(isolated.prepare(makeSpec()), "isolated overlap engine prepare");

    std::vector<float> history(1000, 1.0f);
    std::vector<float> scratch(1000);
    (void) processMono(continuous, history, scratch, makeParameters());
    (void) processMono(isolated, history, scratch, makeParameters());

    std::vector<float> launchInput(368, 1.0f);
    std::vector<float> launchContinuous(368);
    std::vector<float> launchIsolated(368);
    const auto active = makeParameters(120.0, 10.0, 1.0, 0.0, false);
    (void) processMono(continuous, launchInput, launchContinuous, active);
    (void) processMono(isolated, launchInput, launchIsolated, active);

    std::vector<float> overlapInput(368, 1.0f);
    std::vector<float> overlapContinuous(368);
    std::vector<float> overlapIsolated(368);
    (void) processMono(continuous, overlapInput, overlapContinuous, active);
    (void) processMono(isolated, overlapInput, overlapIsolated, makeParameters());

    std::array followUpInput { 1.0f };
    std::array continuousOutput { 0.0f };
    std::array isolatedOutput { 0.0f };
    (void) processMono(continuous, followUpInput, continuousOutput, makeParameters());
    (void) processMono(isolated, followUpInput, isolatedOutput, makeParameters());
    context.expect(
        continuousOutput[0] > isolatedOutput[0],
        "overlapping grain contribution should be raw-added without normalisation"
    );

    GranularEngine reclaimEngine;
    context.expect(reclaimEngine.prepare(makeSpec()), "reclaim stress engine prepare");
    (void) processMono(reclaimEngine, history, scratch, makeParameters());

    std::vector<float> eventInput(368, 1.0f);
    std::vector<float> eventOutput(368);
    std::vector<float> finishInput(441, 1.0f);
    std::vector<float> finishOutput(441);
    auto sawWetSignal = true;

    for (std::size_t cycle = 0; cycle < 65; ++cycle)
    {
        const auto eventResult = processMono(
            reclaimEngine,
            eventInput,
            eventOutput,
            active
        );
        const auto finishResult = processMono(
            reclaimEngine,
            finishInput,
            finishOutput,
            makeParameters()
        );
        sawWetSignal = sawWetSignal
            && eventResult == GranularEngineProcessResult::success
            && finishResult == GranularEngineProcessResult::success
            && std::any_of(
                finishOutput.begin(),
                finishOutput.end(),
                [](float value) { return value > 0.0f; }
            );
    }

    context.expect(
        sawWetSignal,
        "more than 64 sequential grains should keep starting after reclamation"
    );
}

std::vector<float> renderPartitioned(std::size_t blockSize)
{
    constexpr std::size_t totalSamples = 6000;
    std::vector<float> input(totalSamples);
    std::vector<float> output(totalSamples, 0.0f);

    for (std::size_t sample = 0; sample < totalSamples; ++sample)
        input[sample] = static_cast<float>(sample % 1024) / 1024.0f;

    GranularEngine engine;

    if (!engine.prepare(makeSpec(48000.0, 64, 1)))
        return {};

    const auto parameters = makeParameters(120.0, 10.0, 0.35, -7.0, false);

    for (std::size_t start = 0; start < totalSamples; start += blockSize)
    {
        const auto length = std::min(blockSize, totalSamples - start);
        const auto result = processMono(
            engine,
            std::span<const float> { input }.subspan(start, length),
            std::span<float> { output }.subspan(start, length),
            parameters
        );

        if (result != GranularEngineProcessResult::success)
            return {};
    }

    return output;
}

void testBlockIndependence(TestContext& context)
{
    const auto reference = renderPartitioned(1);
    context.expect(!reference.empty(), "block reference should render");

    for (const auto blockSize : { 17u, 64u, 257u, 1024u, 2048u, 4097u })
    {
        context.expect(
            renderPartitioned(blockSize) == reference,
            "wet output should be independent of block partition"
        );
    }
}

struct DirectionRender
{
    std::vector<float> grain;
    bool valid = false;
};

DirectionRender renderDirection(bool reverse)
{
    GranularEngine engine;

    if (!engine.prepare(makeSpec()))
        return {};

    std::vector<float> history(2000);

    for (std::size_t sample = 0; sample < history.size(); ++sample)
        history[sample] = static_cast<float>(sample);

    std::vector<float> scratch(history.size());

    if (processMono(engine, history, scratch, makeParameters())
        != GranularEngineProcessResult::success)
    {
        return {};
    }

    std::vector<float> eventInput(368);

    for (std::size_t sample = 0; sample < eventInput.size(); ++sample)
        eventInput[sample] = static_cast<float>(history.size() + sample);

    std::vector<float> eventOutput(eventInput.size());
    const auto parameters = makeParameters(120.0, 10.0, 0.5, 7.0, reverse);

    if (processMono(engine, eventInput, eventOutput, parameters)
        != GranularEngineProcessResult::success)
    {
        return {};
    }

    DirectionRender result;
    result.grain.reserve(441);
    result.grain.push_back(eventOutput.back());
    std::vector<float> tailInput(440);

    for (std::size_t sample = 0; sample < tailInput.size(); ++sample)
        tailInput[sample] = static_cast<float>(2368 + sample);

    std::vector<float> tailOutput(tailInput.size());

    if (processMono(engine, tailInput, tailOutput, makeParameters())
        != GranularEngineProcessResult::success)
    {
        return {};
    }

    result.grain.insert(
        result.grain.end(),
        tailOutput.begin(),
        tailOutput.end()
    );
    result.valid = allFinite(result.grain);
    return result;
}

void testForwardReverseFractional(TestContext& context)
{
    const auto forward = renderDirection(false);
    const auto reverse = renderDirection(true);
    context.expect(forward.valid && reverse.valid, "fractional direction renders should succeed");
    context.expect(
        forward.grain.size() == 441 && reverse.grain.size() == 441,
        "direction renders should contain exactly one grain duration"
    );

    if (forward.grain.size() != reverse.grain.size())
        return;

    for (std::size_t frame = 0; frame < forward.grain.size(); ++frame)
    {
        context.expectNear(
            forward.grain[frame],
            reverse.grain[reverse.grain.size() - 1 - frame],
            2.0e-3,
            "forward and reverse should render one fractional segment oppositely"
        );
    }
}

void testParameterSnapshot(TestContext& context)
{
    GranularEngine changedEngine;
    GranularEngine referenceEngine;
    context.expect(changedEngine.prepare(makeSpec()), "changed snapshot engine prepare");
    context.expect(referenceEngine.prepare(makeSpec()), "reference snapshot engine prepare");

    std::vector<float> history(2000);

    for (std::size_t sample = 0; sample < history.size(); ++sample)
        history[sample] = static_cast<float>(sample % 251);

    std::vector<float> scratchA(history.size());
    std::vector<float> scratchB(history.size());
    (void) processMono(changedEngine, history, scratchA, makeParameters());
    (void) processMono(referenceEngine, history, scratchB, makeParameters());

    const auto original = makeParameters(120.0, 10.0, 0.25, 0.0, false);
    std::vector<float> launchInput(368, 1.0f);
    std::vector<float> launchA(368);
    std::vector<float> launchB(368);
    (void) processMono(changedEngine, launchInput, launchA, original);
    (void) processMono(referenceEngine, launchInput, launchB, original);

    auto changed = makeParameters(0.0, 20.0, 0.9, 12.0, true);
    auto stoppedOriginal = original;
    stoppedOriginal.densityGrainsPerSecond = 0.0;
    std::vector<float> activeInput(100, 1.0f);
    std::vector<float> activeA(100);
    std::vector<float> activeB(100);
    (void) processMono(changedEngine, activeInput, activeA, changed);
    (void) processMono(referenceEngine, activeInput, activeB, stoppedOriginal);
    context.expect(
        activeA == activeB,
        "changed launch parameters must not alter an already active grain"
    );

    std::vector<float> finishInput(340, 1.0f);
    std::vector<float> finishA(340);
    std::vector<float> finishB(340);
    (void) processMono(changedEngine, finishInput, finishA, changed);
    (void) processMono(referenceEngine, finishInput, finishB, stoppedOriginal);

    changed.densityGrainsPerSecond = 120.0;
    std::vector<float> secondEventInput(368, 1.0f);
    std::vector<float> secondEventA(368);
    std::vector<float> secondEventB(368);
    (void) processMono(changedEngine, secondEventInput, secondEventA, changed);
    (void) processMono(referenceEngine, secondEventInput, secondEventB, original);

    std::vector<float> newVoiceInput(20, 1.0f);
    std::vector<float> newVoiceA(20);
    std::vector<float> newVoiceB(20);
    (void) processMono(changedEngine, newVoiceInput, newVoiceA, makeParameters());
    (void) processMono(referenceEngine, newVoiceInput, newVoiceB, makeParameters());
    context.expect(
        newVoiceA != newVoiceB,
        "changed parameters should affect subsequently launched grains"
    );
}

void testNonFiniteInputsAndParameters(TestContext& context)
{
    GranularEngine engine;
    context.expect(engine.prepare(makeSpec()), "non-finite engine should prepare");

    std::array input {
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
        1.0f
    };
    std::array output { 4.0f, 4.0f, 4.0f, 4.0f };
    context.expect(
        processMono(engine, input, output, makeParameters())
            == GranularEngineProcessResult::degraded,
        "non-finite input should degrade but process the block"
    );
    context.expect(allFinite(output), "non-finite input must not reach wet output");

    std::array finiteInput { 1.0f, 1.0f };
    std::array finiteOutput { -1.0f, -1.0f };
    auto invalidDensity = makeParameters();
    invalidDensity.densityGrainsPerSecond
        = std::numeric_limits<double>::quiet_NaN();
    context.expect(
        processMono(engine, finiteInput, finiteOutput, invalidDensity)
            == GranularEngineProcessResult::degraded,
        "NaN Density should degrade scheduling without corrupting output"
    );
    context.expect(allFinite(finiteOutput), "NaN parameter output should stay finite");

    GranularEngine invalidEventEngine;
    context.expect(invalidEventEngine.prepare(makeSpec()), "invalid-event engine prepare");
    std::vector<float> history(1000, 1.0f);
    std::vector<float> historyOutput(1000);
    (void) processMono(invalidEventEngine, history, historyOutput, makeParameters());
    std::vector<float> eventInput(368, 1.0f);
    std::vector<float> eventOutput(368);
    auto invalidSize = makeParameters(120.0);
    invalidSize.grainSizeMilliseconds = std::numeric_limits<double>::infinity();
    context.expect(
        processMono(invalidEventEngine, eventInput, eventOutput, invalidSize)
            == GranularEngineProcessResult::degraded,
        "non-finite event parameter should safely drop the event"
    );
    context.expect(allFinite(eventOutput), "invalid event output should stay finite");
}

void testRendererDegradedAndFiniteOutput(TestContext& context)
{
    GranularEngine engine;
    context.expect(engine.prepare(makeSpec()), "renderer-degraded engine prepare");

    std::vector<float> history(23000, std::numeric_limits<float>::max());
    std::vector<float> historyOutput(history.size());
    context.expect(
        processMono(engine, history, historyOutput, makeParameters())
            == GranularEngineProcessResult::success,
        "huge finite history should record"
    );

    std::vector<float> input(10000, std::numeric_limits<float>::max());
    std::vector<float> output(input.size(), 0.0f);
    const auto result = processMono(
        engine,
        input,
        output,
        makeParameters(120.0, 500.0, 1.0, 0.0, false)
    );
    context.expect(
        result == GranularEngineProcessResult::degraded,
        "unrepresentable raw overlap-add should report degraded"
    );
    context.expect(allFinite(output), "Renderer degradation must never emit NaN or Inf");
}

void testNoAllocationsAfterPrepare(TestContext& context)
{
    GranularEngine engine;
    context.expect(engine.prepare(makeSpec()), "allocation engine should prepare");

    std::array<float, 1024> input {};
    std::array<float, 1024> output {};
    const auto block = makeMonoBlock(input, output);
    const auto parameters = makeParameters(120.0, 10.0, 0.5, 7.0, false);

    allocationProbe::count.store(0, std::memory_order_relaxed);
    allocationProbe::enabled.store(true, std::memory_order_relaxed);
    const auto result = engine.processBlock(block, parameters);
    allocationProbe::enabled.store(false, std::memory_order_relaxed);
    const auto allocations = allocationProbe::count.load(std::memory_order_relaxed);

    context.expect(
        result == GranularEngineProcessResult::success,
        "allocation-instrumented block should process"
    );
    context.expect(allocations == 0, "processBlock should perform no heap allocations");
}
}

int main()
{
    TestContext context;

    testPreparationLifecycle(context);
    testEmptyAndInvalidBlocks(context);
    testInPlaceOutOfPlaceAndOversized(context);
    testHistoryLaunchAndSingleGrain(context);
    testResetClearsRuntimeState(context);
    testOverlapAddAndRepeatedReclaim(context);
    testBlockIndependence(context);
    testForwardReverseFractional(context);
    testParameterSnapshot(context);
    testNonFiniteInputsAndParameters(context);
    testRendererDegradedAndFiniteOutput(context);
    testNoAllocationsAfterPrepare(context);

    if (context.getFailures() == 0)
    {
        std::cout << "All GranularEngine tests passed.\n";
        return 0;
    }

    std::cerr << context.getFailures() << " test(s) failed.\n";
    return 1;
}
