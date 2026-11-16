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

    // Single-consumer API for the message thread. Sequence numbers identify
    // publication attempts; gaps mean snapshots were dropped because the
    // fixed SPSC queue was full.
    [[nodiscard]] bool tryPopGranularTelemetry(
        hypercolor::dsp::granular::GranularTelemetrySnapshot& destination
    ) noexcept;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout
        createParameterLayout();

    void updateParameterTargets() noexcept;
    [[nodiscard]] hypercolor::dsp::granular::GranularEngineParameters
        readGranularParameters() const noexcept;
    void processChunk(
        juce::AudioBuffer<float>& buffer,
        int startSample,
        int numSamples,
        int numChannels,
        const hypercolor::dsp::granular::GranularEngineParameters&
            granularParameters
    ) noexcept;
    void publishGranularTelemetry(int processedSamples) noexcept;

    juce::AudioProcessorValueTreeState parameters;

    std::atomic<float>* dryWetParameter = nullptr;
    std::atomic<float>* outputGainParameter = nullptr;
    std::atomic<float>* grainDensityParameter = nullptr;
    std::atomic<float>* grainSizeParameter = nullptr;
    std::atomic<float>* grainPositionParameter = nullptr;
    std::atomic<float>* grainPitchParameter = nullptr;
    std::atomic<float>* grainReverseParameter = nullptr;
    std::atomic<float>* grainCharacterParameter = nullptr;
    std::atomic<float>* grainPitchRandomParameter = nullptr;
    std::atomic<float>* grainPositionRandomParameter = nullptr;

    juce::AudioBuffer<float> dryBuffer;
    hypercolor::dsp::granular::GranularEngine granularEngine;
    hypercolor::dsp::granular::GranularTelemetryQueue granularTelemetryQueue;
    std::uint64_t telemetryPublishIntervalSamples = 0;
    std::uint64_t telemetrySamplesSincePublication = 0;
    std::uint64_t nextTelemetrySequence = 0;
    bool granularEnginePrepared = false;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        dryWetSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        outputGainSmoother;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HypercolorAudioProcessor)
};
