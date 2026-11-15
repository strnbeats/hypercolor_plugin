#pragma once

#include <JuceHeader.h>

#include "PluginParameters.h"
#include "dsp/granular/GranularEngine.h"

class HypercolorAudioProcessor final : public juce::AudioProcessor
{
public:
    HypercolorAudioProcessor();
    ~HypercolorAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(
        juce::AudioBuffer<float>& buffer,
        juce::MidiBuffer& midiMessages
    ) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;

    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getValueTreeState() noexcept;
    const juce::AudioProcessorValueTreeState& getValueTreeState() const noexcept;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout
        createParameterLayout();

    void updateParameterTargets() noexcept;
    void processChunk(
        juce::AudioBuffer<float>& buffer,
        int startSample,
        int numSamples,
        int numChannels
    ) noexcept;

    juce::AudioProcessorValueTreeState parameters;

    std::atomic<float>* dryWetParameter = nullptr;
    std::atomic<float>* outputGainParameter = nullptr;

    juce::AudioBuffer<float> dryBuffer;
    hypercolor::dsp::granular::GranularEngine granularEngine;
    bool granularEnginePrepared = false;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        dryWetSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        outputGainSmoother;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HypercolorAudioProcessor)
};
