#include "PluginProcessor.h"

#include <cmath>
#include <iostream>

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

void testStateRoundTrip(TestContext& context)
{
    HypercolorAudioProcessor source;
    setParameter(source, hypercolor::ParameterIDs::dryWet, 37.5f);
    setParameter(source, hypercolor::ParameterIDs::outputGain, -7.2f);

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
    testOutputGain(context);
    testStateRoundTrip(context);

    if (context.getFailures() == 0)
    {
        std::cout << "All HYPERCOLOR processor tests passed.\n";
        return 0;
    }

    std::cerr << context.getFailures() << " test(s) failed.\n";
    return 1;
}
