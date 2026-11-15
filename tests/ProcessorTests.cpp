#include "PluginProcessor.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>

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
constexpr double sampleRate = 48000.0;
constexpr double sampleRates[] = { 44100.0, 48000.0, 96000.0, 192000.0 };

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

    int getFailures() const noexcept
    {
        return failures;
    }

private:
    int failures = 0;
};

juce::RangedAudioParameter& getParameter(
    HypercolorAudioProcessor& processor,
    const char* parameterID
)
{
    auto* parameter = processor.getValueTreeState().getParameter(parameterID);
    jassert(parameter != nullptr);
    return *parameter;
}

void setParameter(
    HypercolorAudioProcessor& processor,
    const char* parameterID,
    float value
)
{
    auto& parameter = getParameter(processor, parameterID);
    parameter.setValueNotifyingHost(parameter.convertTo0to1(value));
}

float getParameterValue(
    HypercolorAudioProcessor& processor,
    const char* parameterID
)
{
    return getParameter(processor, parameterID).convertFrom0to1(
        getParameter(processor, parameterID).getValue()
    );
}

void fillBuffer(juce::AudioBuffer<float>& buffer, float value)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        buffer.clear(channel, 0, buffer.getNumSamples());

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            buffer.setSample(channel, sample, value);
    }
}

bool isFinite(const juce::AudioBuffer<float>& buffer)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            if (!std::isfinite(buffer.getSample(channel, sample)))
                return false;
        }
    }

    return true;
}

bool isSilent(const juce::AudioBuffer<float>& buffer)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            if (buffer.getSample(channel, sample) != 0.0f)
                return false;
        }
    }

    return true;
}

bool buffersDiffer(
    const juce::AudioBuffer<float>& first,
    const juce::AudioBuffer<float>& second,
    float tolerance = 1.0e-5f
)
{
    if (first.getNumChannels() != second.getNumChannels()
        || first.getNumSamples() != second.getNumSamples())
    {
        return true;
    }

    for (int channel = 0; channel < first.getNumChannels(); ++channel)
    {
        for (int sample = 0; sample < first.getNumSamples(); ++sample)
        {
            if (std::abs(
                    first.getSample(channel, sample)
                    - second.getSample(channel, sample)
                ) > tolerance)
            {
                return true;
            }
        }
    }

    return false;
}

void expectParameterContract(
    TestContext& context,
    HypercolorAudioProcessor& processor,
    const char* parameterID,
    float expectedMinimum,
    float expectedMaximum,
    float expectedDefault
)
{
    auto* parameter = processor.getValueTreeState().getParameter(parameterID);
    context.expect(parameter != nullptr, "expected APVTS parameter should exist");

    if (parameter == nullptr)
        return;

    const auto actualMinimum = parameter->convertFrom0to1(0.0f);
    const auto actualMaximum = parameter->convertFrom0to1(1.0f);
    const auto actualDefault = parameter->convertFrom0to1(
        parameter->getDefaultValue()
    );

    context.expect(
        std::abs(actualMinimum - expectedMinimum) < 1.0e-4f,
        "parameter minimum should match its public contract"
    );
    context.expect(
        std::abs(actualMaximum - expectedMaximum) < 1.0e-4f,
        "parameter maximum should match its public contract"
    );
    context.expect(
        std::abs(actualDefault - expectedDefault) < 1.0e-4f,
        "parameter default should match its public contract"
    );
}

void testGranularParameterContracts(TestContext& context)
{
    HypercolorAudioProcessor processor;
    expectParameterContract(
        context,
        processor,
        hypercolor::ParameterIDs::grainDensity,
        0.0f,
        120.0f,
        20.0f
    );
    expectParameterContract(
        context,
        processor,
        hypercolor::ParameterIDs::grainSize,
        10.0f,
        500.0f,
        100.0f
    );
    expectParameterContract(
        context,
        processor,
        hypercolor::ParameterIDs::grainPosition,
        0.0f,
        1.0f,
        0.0f
    );
    expectParameterContract(
        context,
        processor,
        hypercolor::ParameterIDs::grainPitch,
        -24.0f,
        24.0f,
        0.0f
    );
    expectParameterContract(
        context,
        processor,
        hypercolor::ParameterIDs::grainReverse,
        0.0f,
        1.0f,
        0.0f
    );
}

void testLayoutsAndBlockSizes(TestContext& context)
{
    for (const auto testSampleRate : sampleRates)
    {
        for (const auto channels : { 1, 2 })
        {
            for (const auto blockSize : { 1, 17, 64, 257, 1024 })
            {
                HypercolorAudioProcessor processor;
                const auto channelSet = channels == 1
                    ? juce::AudioChannelSet::mono()
                    : juce::AudioChannelSet::stereo();

                context.expect(
                    processor.setBusesLayout(
                        { channelSet, channelSet }
                    ),
                    "mono/stereo layout should be accepted"
                );
                setParameter(
                    processor,
                    hypercolor::ParameterIDs::dryWet,
                    0.0f
                );

                // Deliberately prepare for less than the delivered block in
                // some cases to exercise bounded chunk processing.
                processor.prepareToPlay(
                    testSampleRate,
                    juce::jmin(64, blockSize)
                );

                juce::AudioBuffer<float> buffer(channels, blockSize);
                juce::MidiBuffer midi;
                fillBuffer(buffer, 0.25f);
                processor.processBlock(buffer, midi);

                for (int channel = 0; channel < channels; ++channel)
                {
                    context.expect(
                        std::abs(
                            buffer.getSample(channel, blockSize - 1) - 0.25f
                        ) < 1.0e-6f,
                        "default processing should preserve the signal"
                    );
                }
            }
        }
    }
}

void testOutputGain(TestContext& context)
{
    HypercolorAudioProcessor processor;
    setParameter(processor, hypercolor::ParameterIDs::dryWet, 0.0f);
    processor.prepareToPlay(sampleRate, 64);
    setParameter(
        processor,
        hypercolor::ParameterIDs::outputGain,
        -6.0f
    );

    juce::AudioBuffer<float> buffer(2, 2048);
    juce::MidiBuffer midi;
    fillBuffer(buffer, 0.5f);
    processor.processBlock(buffer, midi);

    const auto expected = 0.5f
        * juce::Decibels::decibelsToGain(-6.0f);

    context.expect(
        std::abs(buffer.getSample(0, buffer.getNumSamples() - 1) - expected)
            < 1.0e-4f,
        "Output Gain should reach the automated target after smoothing"
    );
    context.expect(
        buffer.getSample(0, 0) > buffer.getSample(0, buffer.getNumSamples() - 1),
        "Output Gain automation should be smoothed"
    );
}

juce::AudioBuffer<float> renderMono(
    float dryWet,
    float outputGainDb = 0.0f,
    int samples = 12000
)
{
    HypercolorAudioProcessor processor;
    (void) processor.setBusesLayout(
        { juce::AudioChannelSet::mono(), juce::AudioChannelSet::mono() }
    );
    setParameter(processor, hypercolor::ParameterIDs::dryWet, dryWet);
    setParameter(
        processor,
        hypercolor::ParameterIDs::outputGain,
        outputGainDb
    );
    processor.prepareToPlay(sampleRate, 64);

    juce::AudioBuffer<float> buffer(1, samples);

    for (int sample = 0; sample < samples; ++sample)
    {
        buffer.setSample(
            0,
            sample,
            0.2f + 0.1f * static_cast<float>(sample % 101) / 100.0f
        );
    }

    juce::MidiBuffer midi;
    processor.processBlock(buffer, midi);
    return buffer;
}

juce::AudioBuffer<float> renderGranularParameters(
    float density,
    float grainSize,
    float position,
    float pitch,
    bool reverse,
    int samples = 36000
)
{
    HypercolorAudioProcessor processor;
    (void) processor.setBusesLayout(
        { juce::AudioChannelSet::mono(), juce::AudioChannelSet::mono() }
    );
    setParameter(processor, hypercolor::ParameterIDs::dryWet, 100.0f);
    setParameter(processor, hypercolor::ParameterIDs::grainDensity, density);
    setParameter(processor, hypercolor::ParameterIDs::grainSize, grainSize);
    setParameter(processor, hypercolor::ParameterIDs::grainPosition, position);
    setParameter(processor, hypercolor::ParameterIDs::grainPitch, pitch);
    setParameter(
        processor,
        hypercolor::ParameterIDs::grainReverse,
        reverse ? 1.0f : 0.0f
    );
    processor.prepareToPlay(sampleRate, 64);

    juce::AudioBuffer<float> buffer(1, samples);

    for (int sample = 0; sample < samples; ++sample)
    {
        const auto phase = static_cast<float>(sample % 997) / 997.0f;
        buffer.setSample(
            0,
            sample,
            0.35f * std::sin(phase * juce::MathConstants<float>::twoPi)
                + 0.15f * phase
        );
    }

    juce::MidiBuffer midi;
    processor.processBlock(buffer, midi);
    return buffer;
}

void testGranularParametersReachEngine(TestContext& context)
{
    const auto noDensity = renderGranularParameters(
        0.0f,
        100.0f,
        0.0f,
        0.0f,
        false
    );
    const auto defaultRender = renderGranularParameters(
        20.0f,
        100.0f,
        0.0f,
        0.0f,
        false
    );
    const auto highDensity = renderGranularParameters(
        120.0f,
        100.0f,
        0.0f,
        0.0f,
        false
    );
    const auto shortGrains = renderGranularParameters(
        20.0f,
        10.0f,
        0.0f,
        0.0f,
        false
    );
    const auto longGrains = renderGranularParameters(
        20.0f,
        500.0f,
        0.0f,
        0.0f,
        false
    );
    const auto oldPosition = renderGranularParameters(
        20.0f,
        100.0f,
        1.0f,
        0.0f,
        false
    );
    const auto pitched = renderGranularParameters(
        20.0f,
        100.0f,
        0.0f,
        12.0f,
        false
    );
    const auto reversed = renderGranularParameters(
        20.0f,
        100.0f,
        0.0f,
        0.0f,
        true
    );

    context.expect(
        isSilent(noDensity),
        "Density 0 should suppress all grain starts"
    );
    context.expect(
        buffersDiffer(defaultRender, highDensity),
        "Density changes should alter the real granular output"
    );
    context.expect(
        buffersDiffer(shortGrains, longGrains),
        "Grain Size changes should alter the real granular output"
    );
    context.expect(
        buffersDiffer(defaultRender, oldPosition),
        "Position changes should alter the real granular output"
    );
    context.expect(
        buffersDiffer(defaultRender, pitched),
        "Pitch changes should alter the real granular output"
    );
    context.expect(
        buffersDiffer(defaultRender, reversed),
        "Reverse changes should alter the real granular output"
    );

    for (const auto* rendered : {
             &noDensity,
             &defaultRender,
             &highDensity,
             &shortGrains,
             &longGrains,
             &oldPosition,
             &pitched,
             &reversed })
    {
        context.expect(isFinite(*rendered), "granular parameter output should stay finite");
    }
}

void testGranularDryWetMix(TestContext& context)
{
    const auto dry = renderMono(0.0f);
    const auto half = renderMono(50.0f);
    const auto wet = renderMono(100.0f);
    const auto attenuatedWet = renderMono(100.0f, -6.0f);
    const auto wetGain = juce::Decibels::decibelsToGain(-6.0f);
    auto wetDiffersFromDry = false;
    auto wetContainsSignal = false;

    for (int sample = 0; sample < dry.getNumSamples(); ++sample)
    {
        const auto expectedInput = 0.2f
            + 0.1f * static_cast<float>(sample % 101) / 100.0f;
        context.expect(
            std::abs(dry.getSample(0, sample) - expectedInput) < 1.0e-6f,
            "Dry/Wet 0% should preserve dry input"
        );

        const auto drySample = dry.getSample(0, sample);
        const auto wetSample = wet.getSample(0, sample);
        wetDiffersFromDry = wetDiffersFromDry
            || std::abs(wetSample - drySample) > 1.0e-4f;
        wetContainsSignal = wetContainsSignal
            || std::abs(wetSample) > 1.0e-5f;
        context.expect(
            std::abs(
                half.getSample(0, sample)
                - 0.5f * (drySample + wetSample)
            ) < 2.0e-5f,
            "Dry/Wet 50% should linearly mix dry and granular wet"
        );
        context.expect(
            std::abs(
                attenuatedWet.getSample(0, sample)
                - wetSample * wetGain
            ) < 2.0e-5f,
            "Output Gain should apply to the granular wet signal"
        );
    }

    context.expect(wetDiffersFromDry, "granular wet should differ from dry input");
    context.expect(wetContainsSignal, "granular wet should become audible after history accumulates");
    context.expect(isFinite(wet), "granular wet output should remain finite");
}

void testStereoGranularProcessing(TestContext& context)
{
    HypercolorAudioProcessor processor;
    setParameter(processor, hypercolor::ParameterIDs::dryWet, 100.0f);
    processor.prepareToPlay(sampleRate, 17);

    juce::AudioBuffer<float> buffer(2, 12000);

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto left = 0.1f
            + 0.2f * static_cast<float>(sample % 97) / 96.0f;
        buffer.setSample(0, sample, left);
        buffer.setSample(1, sample, -0.5f * left);
    }

    juce::MidiBuffer midi;
    processor.processBlock(buffer, midi);
    auto foundWet = false;

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto left = buffer.getSample(0, sample);
        const auto right = buffer.getSample(1, sample);
        foundWet = foundWet || std::abs(left) > 1.0e-5f;

        if (std::abs(left) > 1.0e-5f)
        {
            context.expect(
                std::abs(right + 0.5f * left) < 2.0e-5f,
                "stereo granular processing should preserve channel relationship"
            );
        }
    }

    context.expect(foundWet, "stereo granular wet should become audible");
    context.expect(isFinite(buffer), "stereo granular output should remain finite");
}

void testFallbackResetAndReprepare(TestContext& context)
{
    HypercolorAudioProcessor processor;
    setParameter(processor, hypercolor::ParameterIDs::dryWet, 100.0f);
    setParameter(processor, hypercolor::ParameterIDs::outputGain, -6.0f);
    processor.prepareToPlay(1.0, 64);

    juce::AudioBuffer<float> fallbackBuffer(2, 64);
    fillBuffer(fallbackBuffer, 0.5f);
    juce::MidiBuffer midi;
    processor.processBlock(fallbackBuffer, midi);
    const auto expectedFallback = 0.5f
        * juce::Decibels::decibelsToGain(-6.0f);
    context.expect(
        std::abs(fallbackBuffer.getSample(0, 0) - expectedFallback) < 1.0e-5f,
        "failed Engine prepare should fall back to dry signal with Output Gain"
    );

    processor.releaseResources();
    processor.prepareToPlay(sampleRate, 64);
    juce::AudioBuffer<float> wetBuffer(2, 12000);
    fillBuffer(wetBuffer, 0.25f);
    processor.processBlock(wetBuffer, midi);
    context.expect(isFinite(wetBuffer), "reprepared granular output should stay finite");
    context.expect(
        std::any_of(
            wetBuffer.getReadPointer(0),
            wetBuffer.getReadPointer(0) + wetBuffer.getNumSamples(),
            [](float value) { return std::abs(value) > 1.0e-5f; }
        ),
        "Engine should process again after release and reprepare"
    );
}

void testNonFiniteInputAndRealtimeAllocations(TestContext& context)
{
    HypercolorAudioProcessor processor;
    setParameter(processor, hypercolor::ParameterIDs::dryWet, 100.0f);
    processor.prepareToPlay(sampleRate, 64);

    juce::AudioBuffer<float> history(2, 10000);
    fillBuffer(history, 0.25f);
    juce::MidiBuffer midi;
    processor.processBlock(history, midi);

    juce::AudioBuffer<float> buffer(2, 1024);
    fillBuffer(buffer, 0.25f);
    buffer.setSample(0, 0, std::numeric_limits<float>::quiet_NaN());
    buffer.setSample(1, 1, std::numeric_limits<float>::infinity());

    allocationProbe::count.store(0, std::memory_order_relaxed);
    allocationProbe::enabled.store(true, std::memory_order_relaxed);
    processor.processBlock(buffer, midi);
    allocationProbe::enabled.store(false, std::memory_order_relaxed);

    context.expect(isFinite(buffer), "non-finite input must not reach plugin output");
    context.expect(
        allocationProbe::count.load(std::memory_order_relaxed) == 0,
        "prepared processBlock should perform no heap allocations"
    );
}

void testBlockRateGranularAutomation(TestContext& context)
{
    HypercolorAudioProcessor processor;
    (void) processor.setBusesLayout(
        { juce::AudioChannelSet::mono(), juce::AudioChannelSet::mono() }
    );
    setParameter(processor, hypercolor::ParameterIDs::dryWet, 100.0f);
    setParameter(processor, hypercolor::ParameterIDs::grainDensity, 0.0f);
    processor.prepareToPlay(sampleRate, 64);

    juce::AudioBuffer<float> history(1, 12000);
    fillBuffer(history, 0.25f);
    juce::MidiBuffer midi;
    processor.processBlock(history, midi);
    context.expect(isSilent(history), "Density 0 should keep wet output silent");

    setParameter(processor, hypercolor::ParameterIDs::grainDensity, 120.0f);
    juce::AudioBuffer<float> automated(1, 1000);
    fillBuffer(automated, 0.25f);
    processor.processBlock(automated, midi);
    context.expect(
        !isSilent(automated),
        "Density automation should reach Engine on the next audio block"
    );
}

bool removeParameterFromXml(
    juce::XmlElement& xml,
    const char* parameterID
)
{
    for (auto* child = xml.getFirstChildElement(); child != nullptr;)
    {
        auto* next = child->getNextElement();

        if (child->getStringAttribute("id") == parameterID)
        {
            xml.removeChildElement(child, true);
            return true;
        }

        child = next;
    }

    return false;
}

void testStateRoundTrip(TestContext& context)
{
    HypercolorAudioProcessor source;
    setParameter(source, hypercolor::ParameterIDs::dryWet, 37.5f);
    setParameter(source, hypercolor::ParameterIDs::outputGain, -7.2f);
    setParameter(source, hypercolor::ParameterIDs::grainDensity, 73.5f);
    setParameter(source, hypercolor::ParameterIDs::grainSize, 321.5f);
    setParameter(source, hypercolor::ParameterIDs::grainPosition, 0.731f);
    setParameter(source, hypercolor::ParameterIDs::grainPitch, -7.25f);
    setParameter(source, hypercolor::ParameterIDs::grainReverse, 1.0f);

    juce::MemoryBlock state;
    source.getStateInformation(state);
    context.expect(!state.isEmpty(), "serialized state should not be empty");

    HypercolorAudioProcessor restored;
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));

    context.expect(
        std::abs(getParameterValue(
            restored,
            hypercolor::ParameterIDs::dryWet
        ) - 37.5f) < 0.01f,
        "Dry/Wet should survive state round-trip"
    );
    context.expect(
        std::abs(getParameterValue(
            restored,
            hypercolor::ParameterIDs::outputGain
        ) - (-7.2f)) < 0.01f,
        "Output Gain should survive state round-trip"
    );
    context.expect(
        std::abs(getParameterValue(
            restored,
            hypercolor::ParameterIDs::grainDensity
        ) - 73.5f) < 0.01f,
        "Grain Density should survive state round-trip"
    );
    context.expect(
        std::abs(getParameterValue(
            restored,
            hypercolor::ParameterIDs::grainSize
        ) - 321.5f) < 0.01f,
        "Grain Size should survive state round-trip"
    );
    context.expect(
        std::abs(getParameterValue(
            restored,
            hypercolor::ParameterIDs::grainPosition
        ) - 0.731f) < 0.002f,
        "Grain Position should survive state round-trip"
    );
    context.expect(
        std::abs(getParameterValue(
            restored,
            hypercolor::ParameterIDs::grainPitch
        ) - (-7.25f)) < 0.01f,
        "Grain Pitch should survive state round-trip"
    );
    context.expect(
        getParameterValue(restored, hypercolor::ParameterIDs::grainReverse)
            == 1.0f,
        "Grain Reverse should survive state round-trip"
    );

    auto oldStateXml = juce::AudioProcessor::getXmlFromBinary(
        state.getData(),
        static_cast<int>(state.getSize())
    );
    context.expect(oldStateXml != nullptr, "saved state XML should support migration test");

    if (oldStateXml != nullptr)
    {
        for (const auto* parameterID : {
                 hypercolor::ParameterIDs::grainDensity,
                 hypercolor::ParameterIDs::grainSize,
                 hypercolor::ParameterIDs::grainPosition,
                 hypercolor::ParameterIDs::grainPitch,
                 hypercolor::ParameterIDs::grainReverse })
        {
            context.expect(
                removeParameterFromXml(*oldStateXml, parameterID),
                "legacy state fixture should remove each granular parameter"
            );
        }

        juce::MemoryBlock oldState;
        juce::AudioProcessor::copyXmlToBinary(*oldStateXml, oldState);
        setParameter(restored, hypercolor::ParameterIDs::grainDensity, 99.0f);
        setParameter(restored, hypercolor::ParameterIDs::grainSize, 400.0f);
        setParameter(restored, hypercolor::ParameterIDs::grainPosition, 0.9f);
        setParameter(restored, hypercolor::ParameterIDs::grainPitch, 17.0f);
        setParameter(restored, hypercolor::ParameterIDs::grainReverse, 1.0f);
        restored.setStateInformation(
            oldState.getData(),
            static_cast<int>(oldState.getSize())
        );

        context.expect(
            std::abs(getParameterValue(
                restored,
                hypercolor::ParameterIDs::grainDensity
            ) - 20.0f) < 0.01f,
            "legacy state should restore default Grain Density"
        );
        context.expect(
            std::abs(getParameterValue(
                restored,
                hypercolor::ParameterIDs::grainSize
            ) - 100.0f) < 0.01f,
            "legacy state should restore default Grain Size"
        );
        context.expect(
            getParameterValue(
                restored,
                hypercolor::ParameterIDs::grainPosition
            ) == 0.0f,
            "legacy state should restore default Grain Position"
        );
        context.expect(
            std::abs(getParameterValue(
                restored,
                hypercolor::ParameterIDs::grainPitch
            )) < 0.01f,
            "legacy state should restore default Grain Pitch"
        );
        context.expect(
            getParameterValue(restored, hypercolor::ParameterIDs::grainReverse)
                == 0.0f,
            "legacy state should restore default Grain Reverse"
        );
        context.expect(
            std::abs(getParameterValue(
                restored,
                hypercolor::ParameterIDs::dryWet
            ) - 37.5f) < 0.01f,
            "legacy state should still restore existing parameters"
        );
    }

    const auto dryWetBeforeInvalidState = getParameterValue(
        restored,
        hypercolor::ParameterIDs::dryWet
    );
    const char invalidState[] = "not a HYPERCOLOR state";
    restored.setStateInformation(
        invalidState,
        static_cast<int>(sizeof(invalidState))
    );
    restored.setStateInformation(nullptr, 0);

    context.expect(
        std::abs(getParameterValue(
            restored,
            hypercolor::ParameterIDs::dryWet
        ) - dryWetBeforeInvalidState) < 0.01f,
        "invalid state should leave current parameters unchanged"
    );

    auto futureXml = juce::AudioProcessor::getXmlFromBinary(
        state.getData(),
        static_cast<int>(state.getSize())
    );
    context.expect(futureXml != nullptr, "saved state XML should be readable");

    if (futureXml != nullptr)
    {
        futureXml->setAttribute("schemaVersion", 2);
        juce::MemoryBlock futureState;
        juce::AudioProcessor::copyXmlToBinary(*futureXml, futureState);

        setParameter(restored, hypercolor::ParameterIDs::dryWet, 81.0f);
        restored.setStateInformation(
            futureState.getData(),
            static_cast<int>(futureState.getSize())
        );

        context.expect(
            std::abs(getParameterValue(
                restored,
                hypercolor::ParameterIDs::dryWet
            ) - 81.0f) < 0.01f,
            "newer incompatible schema should be rejected without mutation"
        );
    }
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    TestContext context;

    testLayoutsAndBlockSizes(context);
    testGranularParameterContracts(context);
    testOutputGain(context);
    testGranularDryWetMix(context);
    testGranularParametersReachEngine(context);
    testStereoGranularProcessing(context);
    testFallbackResetAndReprepare(context);
    testNonFiniteInputAndRealtimeAllocations(context);
    testBlockRateGranularAutomation(context);
    testStateRoundTrip(context);

    if (context.getFailures() == 0)
    {
        std::cout << "All HYPERCOLOR processor tests passed.\n";
        return 0;
    }

    std::cerr << context.getFailures() << " test(s) failed.\n";
    return 1;
}
