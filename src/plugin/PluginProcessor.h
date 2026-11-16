#pragma once

#include <JuceHeader.h>

#include "PluginParameters.h"
#include "dsp/granular/GranularEngine.h"
#include "dsp/fx/EffectChain.h"
#include "dsp/fx/SpaceReverb.h"
#include "dsp/fx/ModulationEffect.h"
#include "dsp/fx/DriveEffect.h"
#include "dsp/fx/DelayEffect.h"
#include "dsp/fx/TapeEffect.h"

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
    [[nodiscard]] hypercolor::dsp::fx::SpaceReverbParameters
        readReverbParameters() const noexcept;
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
    std::atomic<float>* grainTimingJitterParameter = nullptr;
    std::atomic<float>* grainStereoSpreadParameter = nullptr;
    std::atomic<float>* grainPanRandomParameter = nullptr;
    std::atomic<float>* grainEnvelopeParameter = nullptr;
    std::atomic<float>* reverbMixParameter = nullptr;
    std::atomic<float>* reverbSizeParameter = nullptr;
    std::atomic<float>* reverbDecayParameter = nullptr;
    std::atomic<float>* reverbDampingParameter = nullptr;
    std::atomic<float>* reverbPreDelayParameter = nullptr;
    std::atomic<float>* reverbWidthParameter = nullptr;
    std::atomic<float>* modulationModeParameter = nullptr;
    std::atomic<float>* modulationRateParameter = nullptr;
    std::atomic<float>* modulationDepthParameter = nullptr;
    std::atomic<float>* modulationBaseDelayParameter = nullptr;
    std::atomic<float>* modulationFeedbackParameter = nullptr;
    std::atomic<float>* modulationMixParameter = nullptr;
    std::atomic<float>* modulationWidthParameter = nullptr;
    std::atomic<float>* driveModeParameter = nullptr;
    std::atomic<float>* driveAmountParameter = nullptr;
    std::atomic<float>* driveToneParameter = nullptr;
    std::atomic<float>* driveMixParameter = nullptr;
    std::atomic<float>* driveOutputParameter = nullptr;
    std::atomic<float>* delayModeParameter = nullptr;
    std::atomic<float>* delaySyncParameter = nullptr;
    std::atomic<float>* delayTimeParameter = nullptr;
    std::atomic<float>* delayDivisionParameter = nullptr;
    std::atomic<float>* delayFeedbackParameter = nullptr;
    std::atomic<float>* delayLowCutParameter = nullptr;
    std::atomic<float>* delayHighCutParameter = nullptr;
    std::atomic<float>* delayMixParameter = nullptr;
    std::atomic<float>* delayWidthParameter = nullptr;
    std::atomic<float>* tapeDriveParameter = nullptr;
    std::atomic<float>* tapeWowParameter = nullptr;
    std::atomic<float>* tapeFlutterParameter = nullptr;
    std::atomic<float>* tapeWearParameter = nullptr;
    std::atomic<float>* tapeMixParameter = nullptr;
    std::atomic<float>* tapeOutputParameter = nullptr;

    juce::AudioBuffer<float> dryBuffer;
    hypercolor::dsp::granular::GranularEngine granularEngine;
    hypercolor::dsp::fx::SpaceReverb spaceReverb;
    hypercolor::dsp::fx::ModulationEffect modulationEffect;
    hypercolor::dsp::fx::DriveEffect driveEffect;
    hypercolor::dsp::fx::DelayEffect delayEffect;
    hypercolor::dsp::fx::TapeEffect tapeEffect;
    hypercolor::dsp::fx::EffectChain effectChain;
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
