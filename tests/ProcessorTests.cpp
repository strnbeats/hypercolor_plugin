#include "PluginProcessor.h"
#include "GranularVisualizer.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <string_view>
#include <utility>

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

static_assert(noexcept(
    std::declval<HypercolorAudioProcessor&>().tryPopGranularTelemetry(
        std::declval<
            hypercolor::dsp::granular::GranularTelemetrySnapshot&
        >()
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
    expectParameterContract(
        context,
        processor,
        hypercolor::ParameterIDs::grainCharacter,
        0.0f,
        2.0f,
        1.0f
    );
    auto* characterParameter = dynamic_cast<juce::AudioParameterChoice*>(
        processor.getValueTreeState().getParameter(
            hypercolor::ParameterIDs::grainCharacter
        )
    );
    context.expect(
        characterParameter != nullptr,
        "Grain Character should be an AudioParameterChoice"
    );

    if (characterParameter != nullptr)
    {
        context.expect(
            characterParameter->choices
                == juce::StringArray {
                    "Controlled Chaos",
                    "Musical",
                    "Atmospheric"
                },
            "Grain Character choice order should remain stable"
        );
    }
    expectParameterContract(
        context,
        processor,
        hypercolor::ParameterIDs::grainPitchRandom,
        0.0f,
        100.0f,
        0.0f
    );
    expectParameterContract(
        context,
        processor,
        hypercolor::ParameterIDs::grainPositionRandom,
        0.0f,
        100.0f,
        0.0f
    );
    expectParameterContract(
        context,
        processor,
        hypercolor::ParameterIDs::grainTimingJitter,
        0.0f,
        100.0f,
        0.0f
    );
    expectParameterContract(
        context,
        processor,
        hypercolor::ParameterIDs::grainStereoSpread,
        0.0f,
        100.0f,
        0.0f
    );
    expectParameterContract(
        context,
        processor,
        hypercolor::ParameterIDs::grainPanRandom,
        0.0f,
        100.0f,
        0.0f
    );
    context.expect(
        dynamic_cast<juce::AudioParameterChoice*>(
            processor.getValueTreeState().getParameter(
                hypercolor::ParameterIDs::grainEnvelope
            )
        ) != nullptr,
        "Grain Envelope should be an AudioParameterChoice"
    );
    expectParameterContract(context, processor, hypercolor::ParameterIDs::reverbMix, 0.0f, 100.0f, 30.0f);
    expectParameterContract(context, processor, hypercolor::ParameterIDs::reverbSize, 0.0f, 100.0f, 50.0f);
    expectParameterContract(context, processor, hypercolor::ParameterIDs::reverbDecay, 0.1f, 20.0f, 4.0f);
    expectParameterContract(context, processor, hypercolor::ParameterIDs::reverbDamping, 0.0f, 100.0f, 40.0f);
    expectParameterContract(context, processor, hypercolor::ParameterIDs::reverbPreDelay, 0.0f, 200.0f, 20.0f);
    expectParameterContract(context, processor, hypercolor::ParameterIDs::reverbWidth, 0.0f, 200.0f, 100.0f);
    context.expect(
        dynamic_cast<juce::AudioParameterChoice*>(
            processor.getValueTreeState().getParameter(
                hypercolor::ParameterIDs::modulationMode
            )
        ) != nullptr,
        "Modulation Mode should be an AudioParameterChoice"
    );
    expectParameterContract(context, processor, hypercolor::ParameterIDs::modulationRate, 0.01f, 10.0f, 0.3f);
    expectParameterContract(context, processor, hypercolor::ParameterIDs::modulationDepth, 0.0f, 100.0f, 50.0f);
    expectParameterContract(context, processor, hypercolor::ParameterIDs::modulationBaseDelay, 0.1f, 20.0f, 7.0f);
    expectParameterContract(context, processor, hypercolor::ParameterIDs::modulationFeedback, -95.0f, 95.0f, 0.0f);
    expectParameterContract(context, processor, hypercolor::ParameterIDs::modulationMix, 0.0f, 100.0f, 30.0f);
    expectParameterContract(context, processor, hypercolor::ParameterIDs::modulationWidth, 0.0f, 200.0f, 100.0f);
    context.expect(
        dynamic_cast<juce::AudioParameterChoice*>(
            processor.getValueTreeState().getParameter(hypercolor::ParameterIDs::driveMode)
        ) != nullptr,
        "Drive Mode should be an AudioParameterChoice"
    );
    expectParameterContract(context, processor, hypercolor::ParameterIDs::driveAmount, 0.0f, 36.0f, 0.0f);
    expectParameterContract(context, processor, hypercolor::ParameterIDs::driveTone, 0.0f, 100.0f, 50.0f);
    expectParameterContract(context, processor, hypercolor::ParameterIDs::driveMix, 0.0f, 100.0f, 100.0f);
    expectParameterContract(context, processor, hypercolor::ParameterIDs::driveOutput, -24.0f, 12.0f, 0.0f);
    context.expect(
        dynamic_cast<juce::AudioParameterChoice*>(
            processor.getValueTreeState().getParameter(hypercolor::ParameterIDs::delayMode)
        ) != nullptr,
        "Delay Mode should be an AudioParameterChoice"
    );
    context.expect(
        dynamic_cast<juce::AudioParameterBool*>(
            processor.getValueTreeState().getParameter(hypercolor::ParameterIDs::delaySync)
        ) != nullptr,
        "Delay Sync should be an AudioParameterBool"
    );
    expectParameterContract(context, processor, hypercolor::ParameterIDs::delayTime, 1.0f, 2000.0f, 350.0f);
    expectParameterContract(context, processor, hypercolor::ParameterIDs::delayFeedback, 0.0f, 95.0f, 35.0f);
    expectParameterContract(context, processor, hypercolor::ParameterIDs::delayLowCut, 20.0f, 2000.0f, 100.0f);
    expectParameterContract(context, processor, hypercolor::ParameterIDs::delayHighCut, 1000.0f, 20000.0f, 12000.0f);
    expectParameterContract(context, processor, hypercolor::ParameterIDs::delayMix, 0.0f, 100.0f, 30.0f);
    expectParameterContract(context, processor, hypercolor::ParameterIDs::delayWidth, 0.0f, 200.0f, 100.0f);
    expectParameterContract(context, processor, hypercolor::ParameterIDs::tapeDrive, 0.0f, 100.0f, 35.0f);
    expectParameterContract(context, processor, hypercolor::ParameterIDs::tapeWow, 0.0f, 100.0f, 15.0f);
    expectParameterContract(context, processor, hypercolor::ParameterIDs::tapeFlutter, 0.0f, 100.0f, 10.0f);
    expectParameterContract(context, processor, hypercolor::ParameterIDs::tapeWear, 0.0f, 100.0f, 15.0f);
    expectParameterContract(context, processor, hypercolor::ParameterIDs::tapeMix, 0.0f, 100.0f, 100.0f);
    expectParameterContract(context, processor, hypercolor::ParameterIDs::tapeOutput, -24.0f, 12.0f, 0.0f);
    expectParameterContract(context, processor, hypercolor::ParameterIDs::bitcrushBits, 4.0f, 24.0f, 16.0f);
    expectParameterContract(context, processor, hypercolor::ParameterIDs::bitcrushRate, 500.0f, 44100.0f, 22050.0f);
    context.expect(
        dynamic_cast<juce::AudioParameterChoice*>(
            processor.getValueTreeState().getParameter(hypercolor::ParameterIDs::bitcrushMode)
        ) != nullptr,
        "Bitcrush Mode should be an AudioParameterChoice"
    );
    expectParameterContract(context, processor, hypercolor::ParameterIDs::bitcrushMix, 0.0f, 100.0f, 100.0f);
    expectParameterContract(context, processor, hypercolor::ParameterIDs::bitcrushOutput, -24.0f, 12.0f, 0.0f);
    expectParameterContract(context, processor, hypercolor::ParameterIDs::filterCutoff, 20.0f, 20000.0f, 10000.0f);
    expectParameterContract(context, processor, hypercolor::ParameterIDs::filterResonance, 0.0f, 100.0f, 20.0f);
    expectParameterContract(context, processor, hypercolor::ParameterIDs::filterDrive, 0.0f, 100.0f, 0.0f);
    expectParameterContract(context, processor, hypercolor::ParameterIDs::filterMix, 0.0f, 100.0f, 100.0f);
    expectParameterContract(context, processor, hypercolor::ParameterIDs::filterOutput, -24.0f, 12.0f, 0.0f);
    context.expect(dynamic_cast<juce::AudioParameterChoice*>(processor.getValueTreeState().getParameter(hypercolor::ParameterIDs::filterMode)) != nullptr, "Filter Mode should be an AudioParameterChoice");
    expectParameterContract(context, processor, hypercolor::ParameterIDs::compressorThreshold, -60.0f, 0.0f, -18.0f);
    expectParameterContract(context, processor, hypercolor::ParameterIDs::compressorRatio, 1.0f, 20.0f, 4.0f);
    expectParameterContract(context, processor, hypercolor::ParameterIDs::compressorAttack, 0.1f, 100.0f, 10.0f);
    expectParameterContract(context, processor, hypercolor::ParameterIDs::compressorRelease, 10.0f, 1000.0f, 100.0f);
    expectParameterContract(context, processor, hypercolor::ParameterIDs::compressorKnee, 0.0f, 24.0f, 6.0f);
    expectParameterContract(context, processor, hypercolor::ParameterIDs::compressorMakeup, 0.0f, 24.0f, 0.0f);
    expectParameterContract(context, processor, hypercolor::ParameterIDs::compressorMix, 0.0f, 100.0f, 100.0f);
    expectParameterContract(context, processor, hypercolor::ParameterIDs::compressorStereoLink, 0.0f, 100.0f, 100.0f);

    constexpr std::array<std::string_view, 66> parameterIDs {
        hypercolor::ParameterIDs::dryWet,
        hypercolor::ParameterIDs::outputGain,
        hypercolor::ParameterIDs::grainDensity,
        hypercolor::ParameterIDs::grainSize,
        hypercolor::ParameterIDs::grainPosition,
        hypercolor::ParameterIDs::grainPitch,
        hypercolor::ParameterIDs::grainReverse,
        hypercolor::ParameterIDs::grainCharacter,
        hypercolor::ParameterIDs::grainPitchRandom,
        hypercolor::ParameterIDs::grainPositionRandom,
        hypercolor::ParameterIDs::grainTimingJitter,
        hypercolor::ParameterIDs::grainStereoSpread,
        hypercolor::ParameterIDs::grainPanRandom,
        hypercolor::ParameterIDs::grainEnvelope,
        hypercolor::ParameterIDs::reverbMix,
        hypercolor::ParameterIDs::reverbSize,
        hypercolor::ParameterIDs::reverbDecay,
        hypercolor::ParameterIDs::reverbDamping,
        hypercolor::ParameterIDs::reverbPreDelay,
        hypercolor::ParameterIDs::reverbWidth,
        hypercolor::ParameterIDs::modulationMode,
        hypercolor::ParameterIDs::modulationRate,
        hypercolor::ParameterIDs::modulationDepth,
        hypercolor::ParameterIDs::modulationBaseDelay,
        hypercolor::ParameterIDs::modulationFeedback,
        hypercolor::ParameterIDs::modulationMix,
        hypercolor::ParameterIDs::modulationWidth,
        hypercolor::ParameterIDs::driveMode,
        hypercolor::ParameterIDs::driveAmount,
        hypercolor::ParameterIDs::driveTone,
        hypercolor::ParameterIDs::driveMix,
        hypercolor::ParameterIDs::driveOutput,
        hypercolor::ParameterIDs::delayMode,
        hypercolor::ParameterIDs::delaySync,
        hypercolor::ParameterIDs::delayTime,
        hypercolor::ParameterIDs::delayDivision,
        hypercolor::ParameterIDs::delayFeedback,
        hypercolor::ParameterIDs::delayLowCut,
        hypercolor::ParameterIDs::delayHighCut,
        hypercolor::ParameterIDs::delayMix,
        hypercolor::ParameterIDs::delayWidth,
        hypercolor::ParameterIDs::tapeDrive,
        hypercolor::ParameterIDs::tapeWow,
        hypercolor::ParameterIDs::tapeFlutter,
        hypercolor::ParameterIDs::tapeWear,
        hypercolor::ParameterIDs::tapeMix,
        hypercolor::ParameterIDs::tapeOutput,
        hypercolor::ParameterIDs::bitcrushBits,
        hypercolor::ParameterIDs::bitcrushRate,
        hypercolor::ParameterIDs::bitcrushMode,
        hypercolor::ParameterIDs::bitcrushMix,
        hypercolor::ParameterIDs::bitcrushOutput,
        hypercolor::ParameterIDs::filterMode,
        hypercolor::ParameterIDs::filterCutoff,
        hypercolor::ParameterIDs::filterResonance,
        hypercolor::ParameterIDs::filterDrive,
        hypercolor::ParameterIDs::filterMix,
        hypercolor::ParameterIDs::filterOutput,
        hypercolor::ParameterIDs::compressorThreshold,
        hypercolor::ParameterIDs::compressorRatio,
        hypercolor::ParameterIDs::compressorAttack,
        hypercolor::ParameterIDs::compressorRelease,
        hypercolor::ParameterIDs::compressorKnee,
        hypercolor::ParameterIDs::compressorMakeup,
        hypercolor::ParameterIDs::compressorMix,
        hypercolor::ParameterIDs::compressorStereoLink
    };

    for (std::size_t first = 0; first < parameterIDs.size(); ++first)
    {
        for (std::size_t second = first + 1;
             second < parameterIDs.size();
             ++second)
        {
            context.expect(
                parameterIDs[first] != parameterIDs[second],
                "APVTS parameter IDs should remain unique"
            );
        }
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
    int samples = 36000,
    float character = 1.0f,
    float pitchRandom = 0.0f,
    float positionRandom = 0.0f,
    float timingJitter = 0.0f
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
    setParameter(
        processor,
        hypercolor::ParameterIDs::grainCharacter,
        character
    );
    setParameter(
        processor,
        hypercolor::ParameterIDs::grainPitchRandom,
        pitchRandom
    );
    setParameter(
        processor,
        hypercolor::ParameterIDs::grainPositionRandom,
        positionRandom
    );
    setParameter(
        processor,
        hypercolor::ParameterIDs::grainTimingJitter,
        timingJitter
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
    const auto randomizedMusical = renderGranularParameters(
        20.0f,
        100.0f,
        0.5f,
        0.0f,
        false,
        36000,
        1.0f,
        100.0f,
        100.0f
    );
    const auto randomizedChaos = renderGranularParameters(
        20.0f,
        100.0f,
        0.5f,
        0.0f,
        false,
        36000,
        0.0f,
        100.0f,
        100.0f
    );
    const auto timingJitteredChaos = renderGranularParameters(
        20.0f,
        100.0f,
        0.5f,
        0.0f,
        false,
        36000,
        0.0f,
        100.0f,
        100.0f,
        100.0f
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
    context.expect(
        buffersDiffer(randomizedMusical, randomizedChaos),
        "Character should scale random variation reaching GranularEngine"
    );
    context.expect(
        buffersDiffer(randomizedChaos, timingJitteredChaos),
        "Timing Jitter should alter event timing reaching GranularEngine"
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
    setParameter(processor, hypercolor::ParameterIDs::reverbMix, 0.0f);
    setParameter(processor, hypercolor::ParameterIDs::modulationMix, 0.0f);
    setParameter(processor, hypercolor::ParameterIDs::tapeMix, 0.0f);
    setParameter(processor, hypercolor::ParameterIDs::filterMix, 0.0f);
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
    setParameter(processor, hypercolor::ParameterIDs::grainTimingJitter, 100.0f);
    processor.prepareToPlay(sampleRate, 64);

    juce::AudioBuffer<float> history(2, 10000);
    fillBuffer(history, 0.25f);
    juce::MidiBuffer midi;
    processor.processBlock(history, midi);

    // 10000 samples leave 400 samples toward the 1600-sample telemetry
    // interval at 48 kHz. This block crosses the publication boundary while
    // allocation instrumentation is active.
    juce::AudioBuffer<float> buffer(2, 1200);
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

void processTelemetryBlock(
    HypercolorAudioProcessor& processor,
    int numSamples,
    float value = 0.25f
)
{
    juce::AudioBuffer<float> buffer(2, numSamples);
    fillBuffer(buffer, value);
    juce::MidiBuffer midi;
    processor.processBlock(buffer, midi);
}

void testTelemetryPublicationAndSequence(TestContext& context)
{
    HypercolorAudioProcessor processor;
    setParameter(processor, hypercolor::ParameterIDs::dryWet, 0.0f);
    setParameter(processor, hypercolor::ParameterIDs::grainDensity, 0.0f);
    processor.prepareToPlay(sampleRate, 64);

    hypercolor::dsp::granular::GranularTelemetrySnapshot snapshot;
    context.expect(
        !processor.tryPopGranularTelemetry(snapshot),
        "telemetry queue should initially be empty"
    );

    constexpr int interval = 1600;
    processTelemetryBlock(processor, interval - 1);
    context.expect(
        !processor.tryPopGranularTelemetry(snapshot),
        "telemetry should not publish before its sample interval"
    );

    processTelemetryBlock(processor, 1);
    context.expect(
        processor.tryPopGranularTelemetry(snapshot),
        "telemetry should publish when the interval is reached"
    );
    context.expect(snapshot.sequence == 0, "first publication sequence should be zero");
    context.expectNear(
        snapshot.sampleRate,
        sampleRate,
        0.0,
        "published snapshot should contain prepared sample rate"
    );
    context.expect(snapshot.waveformPointCount > 0, "published waveform should be present");
    context.expect(
        !processor.tryPopGranularTelemetry(snapshot),
        "one published state must not be readable twice"
    );

    processTelemetryBlock(processor, interval);
    context.expect(processor.tryPopGranularTelemetry(snapshot), "second publication");
    context.expect(snapshot.sequence == 1, "second publication sequence");
    processTelemetryBlock(processor, interval);
    context.expect(processor.tryPopGranularTelemetry(snapshot), "third publication");
    context.expect(snapshot.sequence == 2, "third publication sequence");
}

void testTelemetryPeriodicityAcrossRatesAndBlocks(TestContext& context)
{
    for (const auto testSampleRate : sampleRates)
    {
        for (const auto blockSize : { 1, 7, 64, 511, 1024, 4097 })
        {
            HypercolorAudioProcessor processor;
            setParameter(processor, hypercolor::ParameterIDs::dryWet, 0.0f);
            setParameter(processor, hypercolor::ParameterIDs::grainDensity, 0.0f);
            processor.prepareToPlay(testSampleRate, juce::jmax(1, blockSize));

            const auto interval = static_cast<std::uint64_t>(std::llround(
                testSampleRate / 30.0
            ));
            std::uint64_t processed = 0;
            hypercolor::dsp::granular::GranularTelemetrySnapshot snapshot;
            auto published = false;
            std::uint64_t publicationCount = 0;

            while (!published)
            {
                processTelemetryBlock(processor, blockSize);
                processed += static_cast<std::uint64_t>(blockSize);
                while (processor.tryPopGranularTelemetry(snapshot))
                {
                    published = true;
                    ++publicationCount;
                }

                if (!published)
                {
                    context.expect(
                        processed < interval,
                        "publication should not be late after crossing interval"
                    );
                }
            }

            context.expect(processed >= interval, "publication must not be early");
            context.expect(
                processed < interval + static_cast<std::uint64_t>(blockSize),
                "publication should occur in the first crossing block"
            );
            context.expect(
                publicationCount == processed / interval,
                "oversized blocks should publish distinct interval snapshots"
            );
        }
    }
}

void testTelemetryQueueOverflowAndLifecycle(TestContext& context)
{
    HypercolorAudioProcessor processor;
    setParameter(processor, hypercolor::ParameterIDs::dryWet, 0.0f);
    setParameter(processor, hypercolor::ParameterIDs::grainDensity, 0.0f);
    processor.prepareToPlay(sampleRate, 1600);

    for (int publication = 0; publication < 9; ++publication)
        processTelemetryBlock(processor, 1600);

    hypercolor::dsp::granular::GranularTelemetrySnapshot snapshot;

    for (std::uint64_t sequence = 0; sequence < 8; ++sequence)
    {
        context.expect(
            processor.tryPopGranularTelemetry(snapshot),
            "all eight queue slots should be readable"
        );
        context.expect(snapshot.sequence == sequence, "full queue should remain FIFO");
    }

    context.expect(
        !processor.tryPopGranularTelemetry(snapshot),
        "ninth publication should have been dropped"
    );
    processTelemetryBlock(processor, 1600);
    context.expect(
        processor.tryPopGranularTelemetry(snapshot),
        "publication should resume after consumer frees a slot"
    );
    context.expect(
        snapshot.sequence == 9,
        "dropped full-queue publication should create a sequence gap"
    );

    processor.releaseResources();
    processTelemetryBlock(processor, 3200);
    context.expect(
        !processor.tryPopGranularTelemetry(snapshot),
        "released processor should not publish telemetry"
    );

    processor.prepareToPlay(96000.0, 64);
    processTelemetryBlock(processor, 3199);
    context.expect(
        !processor.tryPopGranularTelemetry(snapshot),
        "reprepare should reset the periodic sample counter"
    );
    processTelemetryBlock(processor, 1);
    context.expect(
        processor.tryPopGranularTelemetry(snapshot),
        "reprepared processor should publish at its new interval"
    );
    context.expect(snapshot.sequence == 10, "sequence should remain monotonic across reprepare");
    context.expectNear(snapshot.sampleRate, 96000.0, 0.0, "reprepared snapshot sample rate");

    HypercolorAudioProcessor queuedAcrossPrepare;
    setParameter(
        queuedAcrossPrepare,
        hypercolor::ParameterIDs::grainDensity,
        0.0f
    );
    queuedAcrossPrepare.prepareToPlay(48000.0, 64);
    processTelemetryBlock(queuedAcrossPrepare, 1600);
    queuedAcrossPrepare.prepareToPlay(96000.0, 64);
    processTelemetryBlock(queuedAcrossPrepare, 3200);

    context.expect(
        queuedAcrossPrepare.tryPopGranularTelemetry(snapshot),
        "unread pre-reprepare snapshot should remain queued"
    );
    context.expectNear(snapshot.sampleRate, 48000.0, 0.0, "old queued sample rate");
    context.expect(snapshot.sequence == 0, "old queued sequence");
    context.expect(
        queuedAcrossPrepare.tryPopGranularTelemetry(snapshot),
        "new post-reprepare snapshot should follow in FIFO order"
    );
    context.expectNear(snapshot.sampleRate, 96000.0, 0.0, "new queued sample rate");
    context.expect(snapshot.sequence == 1, "new queued sequence");
}

void testTelemetryConsumerDoesNotChangeAudio(TestContext& context)
{
    HypercolorAudioProcessor reference;
    HypercolorAudioProcessor observed;
    setParameter(reference, hypercolor::ParameterIDs::dryWet, 0.0f);
    setParameter(observed, hypercolor::ParameterIDs::dryWet, 0.0f);
    reference.prepareToPlay(sampleRate, 64);
    observed.prepareToPlay(sampleRate, 64);

    juce::AudioBuffer<float> referenceBuffer(2, 6000);
    juce::AudioBuffer<float> observedBuffer(2, 6000);
    fillBuffer(referenceBuffer, 0.25f);
    fillBuffer(observedBuffer, 0.25f);
    juce::MidiBuffer midi;
    reference.processBlock(referenceBuffer, midi);
    observed.processBlock(observedBuffer, midi);

    hypercolor::dsp::granular::GranularTelemetrySnapshot snapshot;
    context.expect(
        observed.tryPopGranularTelemetry(snapshot),
        "observed processor should expose telemetry"
    );
    context.expect(
        !buffersDiffer(referenceBuffer, observedBuffer, 0.0f),
        "telemetry publication and consumption must not alter audio output"
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

void renderVisualizerForTest(GranularVisualizer& visualizer);

void testEditorControlsStateAndResize(TestContext& context)
{
    HypercolorAudioProcessor source;
    setParameter(source, hypercolor::ParameterIDs::grainDensity, 55.5f);
    setParameter(source, hypercolor::ParameterIDs::grainSize, 222.2f);
    setParameter(source, hypercolor::ParameterIDs::grainPosition, 0.75f);
    setParameter(source, hypercolor::ParameterIDs::grainPitch, -5.5f);
    setParameter(source, hypercolor::ParameterIDs::grainReverse, 1.0f);

    juce::MemoryBlock state;
    source.getStateInformation(state);
    HypercolorAudioProcessor restored;
    restored.setStateInformation(
        state.getData(),
        static_cast<int>(state.getSize())
    );

    std::unique_ptr<juce::AudioProcessorEditor> editor(
        restored.createEditor()
    );
    context.expect(editor != nullptr, "processor should create its editor");

    if (editor == nullptr)
        return;

    context.expect(
        editor->getWidth() == 1100 && editor->getHeight() == 700,
        "editor should open at the redesigned default size"
    );

    const auto findSlider = [&editor](const char* parameterID)
    {
        return dynamic_cast<juce::Slider*>(
            editor->findChildWithID(parameterID)
        );
    };
    auto* density = findSlider(hypercolor::ParameterIDs::grainDensity);
    auto* size = findSlider(hypercolor::ParameterIDs::grainSize);
    auto* position = findSlider(hypercolor::ParameterIDs::grainPosition);
    auto* pitch = findSlider(hypercolor::ParameterIDs::grainPitch);
    auto* dryWet = findSlider(hypercolor::ParameterIDs::dryWet);
    auto* outputGain = findSlider(hypercolor::ParameterIDs::outputGain);
    auto* reverse = dynamic_cast<juce::Button*>(
        editor->findChildWithID(hypercolor::ParameterIDs::grainReverse)
    );
    auto* visualizer = dynamic_cast<GranularVisualizer*>(
        editor->findChildWithID(GranularVisualizer::componentID)
    );

    context.expect(density != nullptr, "editor should expose Density control");
    context.expect(size != nullptr, "editor should expose Size control");
    context.expect(position != nullptr, "editor should expose Position control");
    context.expect(pitch != nullptr, "editor should expose Pitch control");
    context.expect(reverse != nullptr, "editor should expose Reverse control");
    context.expect(dryWet != nullptr, "editor should retain Dry/Wet control");
    context.expect(outputGain != nullptr, "editor should retain Output Gain control");
    context.expect(visualizer != nullptr, "editor should contain granular visualizer");

    if (visualizer != nullptr)
    {
        hypercolor::dsp::granular::GranularTelemetrySnapshot emptySnapshot;
        context.expect(
            !visualizer->copyLatestSnapshot(emptySnapshot),
            "unprepared processor should show the waiting state"
        );
        renderVisualizerForTest(*visualizer);
    }

    if (density != nullptr)
    {
        context.expect(
            std::abs(density->getValue() - 55.5) < 0.01,
            "Density attachment should reflect restored state"
        );
    }

    if (size != nullptr)
    {
        context.expect(
            std::abs(size->getValue() - 222.2) < 0.11,
            "Size attachment should reflect restored state"
        );
    }

    if (position != nullptr)
    {
        context.expect(
            std::abs(position->getValue() - 0.75) < 0.002,
            "Position attachment should reflect restored state"
        );
        context.expect(
            position->getTextFromValue(position->getValue()).contains("75"),
            "Position control should display normalized value as percent"
        );
    }

    if (pitch != nullptr)
    {
        context.expect(
            std::abs(pitch->getValue() - (-5.5)) < 0.01,
            "Pitch attachment should reflect restored state"
        );
        context.expect(
            pitch->getTextFromValue(pitch->getValue()).contains("st"),
            "Pitch control should display semitone units"
        );
    }

    if (reverse != nullptr)
    {
        context.expect(
            reverse->getToggleState(),
            "Reverse attachment should reflect restored On state"
        );
        context.expect(
            reverse->getButtonText().contains("ON"),
            "Reverse button should state On explicitly"
        );
    }

    if (const auto* constrainer = editor->getConstrainer())
    {
        context.expect(
            constrainer->getMinimumWidth() == 760
                && constrainer->getMinimumHeight() == 560,
            "editor minimum size should keep visualizer and controls readable"
        );
        context.expect(
            constrainer->getMaximumWidth() == 1600
                && constrainer->getMaximumHeight() == 1000,
            "editor maximum size should support expanded visualizer layouts"
        );
    }
    else
    {
        context.expect(false, "resizable editor should have a bounds constrainer");
    }

    const auto controls = std::array<juce::Component*, 7> {
        density,
        size,
        position,
        pitch,
        reverse,
        dryWet,
        outputGain
    };

    for (const auto editorSize : {
             juce::Point<int> { 760, 560 },
             juce::Point<int> { 900, 620 },
             juce::Point<int> { 1100, 700 },
             juce::Point<int> { 1320, 840 },
             juce::Point<int> { 1600, 1000 } })
    {
        editor->setSize(editorSize.x, editorSize.y);

        for (const auto* control : controls)
        {
            if (control == nullptr)
                continue;

            context.expect(
                !control->getBounds().isEmpty(),
                "granular controls should remain visible at resize limits"
            );
            context.expect(
                editor->getLocalBounds().contains(control->getBounds()),
                "granular controls should remain inside editor bounds"
            );
        }

        for (std::size_t first = 0; first < controls.size(); ++first)
        {
            if (controls[first] == nullptr)
                continue;

            for (std::size_t second = first + 1;
                 second < controls.size();
                 ++second)
            {
                if (controls[second] == nullptr)
                    continue;

                context.expect(
                    !controls[first]->getBounds().intersects(
                        controls[second]->getBounds()
                    ),
                    "parameter controls should not overlap at supported sizes"
                );
            }
        }

        if (visualizer != nullptr)
        {
            context.expect(
                !visualizer->getBounds().isEmpty(),
                "visualizer should remain visible at resize limits"
            );
            context.expect(
                editor->getLocalBounds().contains(visualizer->getBounds()),
                "visualizer should remain inside editor bounds"
            );

            for (const auto* control : controls)
            {
                if (control != nullptr)
                {
                    context.expect(
                        !visualizer->getBounds().intersects(
                            control->getBounds()
                        ),
                        "visualizer must not overlap parameter controls"
                    );
                }
            }
        }

        juce::Image renderedEditor(
            juce::Image::ARGB,
            editorSize.x,
            editorSize.y,
            true
        );
        juce::Graphics editorGraphics(renderedEditor);
        editor->paintEntireComponent(editorGraphics, true);
    }
}

GranularVisualizer* findGranularVisualizer(
    juce::AudioProcessorEditor& editor
)
{
    return dynamic_cast<GranularVisualizer*>(
        editor.findChildWithID(GranularVisualizer::componentID)
    );
}

void renderVisualizerForTest(GranularVisualizer& visualizer)
{
    visualizer.setSize(800, 260);
    juce::Image image(juce::Image::ARGB, 800, 260, true);
    juce::Graphics graphics(image);
    visualizer.paint(graphics);
}

void testGranularVisualizerTelemetry(TestContext& context)
{
    HypercolorAudioProcessor stereoProcessor;
    setParameter(
        stereoProcessor,
        hypercolor::ParameterIDs::grainDensity,
        0.0f
    );
    stereoProcessor.prepareToPlay(sampleRate, 1600);

    for (std::size_t publication = 0;
         publication
            < hypercolor::dsp::granular::granularTelemetryQueueCapacity;
         ++publication)
    {
        processTelemetryBlock(stereoProcessor, 1600, 0.25f);
    }

    std::unique_ptr<juce::AudioProcessorEditor> stereoEditor(
        stereoProcessor.createEditor()
    );
    auto* stereoVisualizer = stereoEditor != nullptr
        ? findGranularVisualizer(*stereoEditor)
        : nullptr;
    context.expect(stereoVisualizer != nullptr, "stereo visualizer should exist");

    hypercolor::dsp::granular::GranularTelemetrySnapshot snapshot;

    if (stereoVisualizer != nullptr)
    {
        context.expect(
            !stereoVisualizer->copyLatestSnapshot(snapshot),
            "editor opening must not display retained telemetry"
        );
        context.expect(
            !stereoVisualizer->consumePendingTelemetry(),
            "drained retained queue should leave the waiting state"
        );
        renderVisualizerForTest(*stereoVisualizer);

        // Eight new snapshots fit; the ninth is dropped. One bounded consume
        // must select the newest of the eight available publications.
        for (std::size_t publication = 0;
             publication
                < hypercolor::dsp::granular::granularTelemetryQueueCapacity + 1;
             ++publication)
        {
            processTelemetryBlock(stereoProcessor, 1600, -0.4f);
        }

        context.expect(
            stereoVisualizer->consumePendingTelemetry(),
            "first post-open telemetry should leave the waiting state"
        );
        context.expect(
            stereoVisualizer->copyLatestSnapshot(snapshot),
            "new telemetry should become visible"
        );
        context.expect(snapshot.sequence == 15, "bounded drain should retain newest queued snapshot");
        context.expect(snapshot.numChannels == 2, "stereo telemetry channel count");
        context.expect(
            snapshot.waveformPointCount > 0
                && snapshot.waveformPointCount < snapshot.waveformPoints.size(),
            "short history should produce a partial waveform"
        );
        context.expect(snapshot.activeGrainCount == 0, "Density 0 should show no grains");
        context.expect(
            !stereoVisualizer->consumePendingTelemetry(),
            "one consume must perform no more than the eight available reads"
        );
        renderVisualizerForTest(*stereoVisualizer);

        constexpr int totalHistoryFrames = 192000;
        constexpr int processedFrames = 17 * 1600;

        for (int remaining = totalHistoryFrames - processedFrames;
             remaining > 0;)
        {
            const auto blockSize = juce::jmin(1600, remaining);
            processTelemetryBlock(stereoProcessor, blockSize, -0.4f);
            (void) stereoVisualizer->consumePendingTelemetry();
            remaining -= blockSize;
        }

        context.expect(
            stereoVisualizer->copyLatestSnapshot(snapshot),
            "full waveform snapshot should remain available"
        );
        context.expect(
            snapshot.waveformPointCount == snapshot.waveformPoints.size(),
            "four seconds of history should fill the waveform ring"
        );
        renderVisualizerForTest(*stereoVisualizer);
    }

    HypercolorAudioProcessor monoProcessor;
    context.expect(
        monoProcessor.setBusesLayout(
            { juce::AudioChannelSet::mono(), juce::AudioChannelSet::mono() }
        ),
        "mono layout should be accepted for visualizer test"
    );
    setParameter(
        monoProcessor,
        hypercolor::ParameterIDs::grainDensity,
        0.0f
    );
    monoProcessor.prepareToPlay(sampleRate, 1600);
    juce::AudioBuffer<float> monoBuffer(1, 1600);
    fillBuffer(monoBuffer, 0.5f);
    juce::MidiBuffer midi;
    monoProcessor.processBlock(monoBuffer, midi);

    std::unique_ptr<juce::AudioProcessorEditor> monoEditor(
        monoProcessor.createEditor()
    );
    auto* monoVisualizer = monoEditor != nullptr
        ? findGranularVisualizer(*monoEditor)
        : nullptr;
    context.expect(monoVisualizer != nullptr, "mono visualizer should exist");

    if (monoVisualizer != nullptr)
    {
        context.expect(
            !monoVisualizer->copyLatestSnapshot(snapshot),
            "mono editor should discard retained telemetry"
        );

        monoProcessor.processBlock(monoBuffer, midi);
        context.expect(
            monoVisualizer->consumePendingTelemetry(),
            "mono visualizer should consume a post-open publication"
        );
        context.expect(
            monoVisualizer->copyLatestSnapshot(snapshot),
            "mono post-open telemetry should become visible"
        );
        context.expect(snapshot.numChannels == 1, "mono telemetry channel count");
        renderVisualizerForTest(*monoVisualizer);
    }
}

void testGranularVisualizerGrainsAndLifecycle(TestContext& context)
{
    HypercolorAudioProcessor processor;
    setParameter(processor, hypercolor::ParameterIDs::grainDensity, 120.0f);
    setParameter(processor, hypercolor::ParameterIDs::grainReverse, 1.0f);
    processor.prepareToPlay(sampleRate, 1600);
    processTelemetryBlock(processor, 8000, 0.25f);

    std::uint64_t firstSequence = 0;

    {
        std::unique_ptr<juce::AudioProcessorEditor> editor(
            processor.createEditor()
        );
        auto* visualizer = editor != nullptr
            ? findGranularVisualizer(*editor)
            : nullptr;
        context.expect(visualizer != nullptr, "first visualizer should exist");

        if (visualizer != nullptr)
        {
            hypercolor::dsp::granular::GranularTelemetrySnapshot snapshot;
            context.expect(
                !visualizer->copyLatestSnapshot(snapshot),
                "first editor should discard telemetry accumulated before opening"
            );

            processTelemetryBlock(processor, 1600, 0.25f);
            context.expect(
                visualizer->consumePendingTelemetry(),
                "first editor should accept post-open grain telemetry"
            );
            context.expect(
                visualizer->copyLatestSnapshot(snapshot),
                "post-open grain telemetry should become visible"
            );
            context.expect(
                snapshot.activeGrainCount > 0,
                "active granular processing should be visible"
            );
            firstSequence = snapshot.sequence;
            renderVisualizerForTest(*visualizer);
        }
    }

    processTelemetryBlock(processor, 1600, 0.25f);

    std::unique_ptr<juce::AudioProcessorEditor> reopenedEditor(
        processor.createEditor()
    );
    auto* reopenedVisualizer = reopenedEditor != nullptr
        ? findGranularVisualizer(*reopenedEditor)
        : nullptr;
    context.expect(reopenedVisualizer != nullptr, "reopened visualizer should exist");

    if (reopenedVisualizer != nullptr)
    {
        hypercolor::dsp::granular::GranularTelemetrySnapshot snapshot;
        context.expect(
            !reopenedVisualizer->copyLatestSnapshot(snapshot),
            "reopened editor should discard telemetry produced while closed"
        );

        processTelemetryBlock(processor, 1600, 0.25f);
        context.expect(
            reopenedVisualizer->consumePendingTelemetry(),
            "reopened editor should accept the next publication"
        );
        context.expect(
            reopenedVisualizer->copyLatestSnapshot(snapshot),
            "reopened editor should display post-open telemetry"
        );
        context.expect(
            snapshot.sequence > firstSequence,
            "reopened editor should display a newer snapshot"
        );
    }
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
    setParameter(source, hypercolor::ParameterIDs::grainCharacter, 2.0f);
    setParameter(source, hypercolor::ParameterIDs::grainPitchRandom, 67.5f);
    setParameter(source, hypercolor::ParameterIDs::grainPositionRandom, 42.5f);
    setParameter(source, hypercolor::ParameterIDs::grainTimingJitter, 58.5f);
    setParameter(source, hypercolor::ParameterIDs::grainStereoSpread, 64.5f);
    setParameter(source, hypercolor::ParameterIDs::grainPanRandom, 29.5f);
    setParameter(source, hypercolor::ParameterIDs::grainEnvelope, 3.0f);

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
    context.expect(
        getParameterValue(restored, hypercolor::ParameterIDs::grainCharacter)
            == 2.0f,
        "Grain Character should survive state round-trip"
    );
    context.expect(
        std::abs(getParameterValue(
            restored,
            hypercolor::ParameterIDs::grainPitchRandom
        ) - 67.5f) < 0.01f,
        "Grain Pitch Random should survive state round-trip"
    );
    context.expect(
        std::abs(getParameterValue(
            restored,
            hypercolor::ParameterIDs::grainPositionRandom
        ) - 42.5f) < 0.01f,
        "Grain Position Random should survive state round-trip"
    );
    context.expect(
        std::abs(getParameterValue(
            restored,
            hypercolor::ParameterIDs::grainTimingJitter
        ) - 58.5f) < 0.01f,
        "Grain Timing Jitter should survive state round-trip"
    );
    context.expect(
        std::abs(getParameterValue(
            restored,
            hypercolor::ParameterIDs::grainStereoSpread
        ) - 64.5f) < 0.01f,
        "Grain Stereo Spread should survive state round-trip"
    );
    context.expect(
        std::abs(getParameterValue(
            restored,
            hypercolor::ParameterIDs::grainPanRandom
        ) - 29.5f) < 0.01f,
        "Grain Pan Random should survive state round-trip"
    );
    context.expect(
        getParameterValue(restored, hypercolor::ParameterIDs::grainEnvelope)
            == 3.0f,
        "Grain Envelope should survive state round-trip"
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
                 hypercolor::ParameterIDs::grainReverse,
                 hypercolor::ParameterIDs::grainCharacter,
                 hypercolor::ParameterIDs::grainPitchRandom,
                 hypercolor::ParameterIDs::grainPositionRandom,
                 hypercolor::ParameterIDs::grainTimingJitter,
                 hypercolor::ParameterIDs::grainStereoSpread,
                 hypercolor::ParameterIDs::grainPanRandom,
                 hypercolor::ParameterIDs::grainEnvelope })
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
        setParameter(restored, hypercolor::ParameterIDs::grainCharacter, 0.0f);
        setParameter(restored, hypercolor::ParameterIDs::grainPitchRandom, 88.0f);
        setParameter(
            restored,
            hypercolor::ParameterIDs::grainPositionRandom,
            77.0f
        );
        setParameter(
            restored,
            hypercolor::ParameterIDs::grainTimingJitter,
            66.0f
        );
        setParameter(
            restored,
            hypercolor::ParameterIDs::grainStereoSpread,
            55.0f
        );
        setParameter(
            restored,
            hypercolor::ParameterIDs::grainPanRandom,
            44.0f
        );
        setParameter(restored, hypercolor::ParameterIDs::grainEnvelope, 3.0f);
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
            getParameterValue(
                restored,
                hypercolor::ParameterIDs::grainCharacter
            ) == 1.0f,
            "legacy state should restore default Musical Character"
        );
        context.expect(
            getParameterValue(
                restored,
                hypercolor::ParameterIDs::grainPitchRandom
            ) == 0.0f,
            "legacy state should restore default Grain Pitch Random"
        );
        context.expect(
            getParameterValue(
                restored,
                hypercolor::ParameterIDs::grainPositionRandom
            ) == 0.0f,
            "legacy state should restore default Grain Position Random"
        );
        context.expect(
            getParameterValue(
                restored,
                hypercolor::ParameterIDs::grainTimingJitter
            ) == 0.0f,
            "legacy state should restore default Grain Timing Jitter"
        );
        context.expect(
            getParameterValue(
                restored,
                hypercolor::ParameterIDs::grainStereoSpread
            ) == 0.0f,
            "legacy state should restore default Grain Stereo Spread"
        );
        context.expect(
            getParameterValue(
                restored,
                hypercolor::ParameterIDs::grainPanRandom
            ) == 0.0f,
            "legacy state should restore default Grain Pan Random"
        );
        context.expect(
            getParameterValue(
                restored,
                hypercolor::ParameterIDs::grainEnvelope
            ) == 0.0f,
            "legacy state should restore default Hann envelope"
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
    testTelemetryPublicationAndSequence(context);
    testTelemetryPeriodicityAcrossRatesAndBlocks(context);
    testTelemetryQueueOverflowAndLifecycle(context);
    testTelemetryConsumerDoesNotChangeAudio(context);
    testStateRoundTrip(context);
    testEditorControlsStateAndResize(context);
    testGranularVisualizerTelemetry(context);
    testGranularVisualizerGrainsAndLifecycle(context);

    if (context.getFailures() == 0)
    {
        std::cout << "All HYPERCOLOR processor tests passed.\n";
        return 0;
    }

    std::cerr << context.getFailures() << " test(s) failed.\n";
    return 1;
}
