#pragma once

#include <JuceHeader.h>
#include <optional>

#include "PluginParameters.h"
#include "FxRackConfig.h"
#include "LfoConfig.h"
#include "dsp/granular/GranularEngine.h"
#include "dsp/fx/EffectChain.h"
#include "dsp/fx/SpaceReverb.h"
#include "dsp/fx/ModulationEffect.h"
#include "dsp/fx/DriveEffect.h"
#include "dsp/fx/DelayEffect.h"
#include "dsp/fx/TapeEffect.h"
#include "dsp/fx/BitcrushEffect.h"
#include "dsp/fx/FilterEffect.h"
#include "dsp/fx/CompressorEffect.h"

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
    [[nodiscard]] hypercolor::plugin::FxChainConfiguration
        getFxChainConfiguration() const noexcept;
    [[nodiscard]] bool setFxChainConfiguration(
        const hypercolor::plugin::FxChainConfiguration& configuration
    ) noexcept;
    [[nodiscard]] std::uint64_t allocateFxInstanceIdentity() noexcept;
    [[nodiscard]] hypercolor::plugin::FxMacroAssignmentSet
        getFxMacroAssignments() const;
    [[nodiscard]] bool setFxMacroAssignments(
        const hypercolor::plugin::FxMacroAssignmentSet& assignments
    );
    [[nodiscard]] hypercolor::plugin::LfoConfiguration getLfoConfiguration() const;
    [[nodiscard]] std::uint32_t getLfoConfigurationRevision() const noexcept;
    [[nodiscard]] float getLfoPhaseForDisplay(std::size_t moduleIndex) const noexcept;
    [[nodiscard]] bool setLfoConfiguration(
        const hypercolor::plugin::LfoConfiguration& configuration
    );
    [[nodiscard]] std::uint64_t allocateLfoIdentity() noexcept;

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
    void updateEffectBypassTargets() noexcept;
    [[nodiscard]] double readHostBpm() noexcept;
    [[nodiscard]] hypercolor::dsp::granular::GranularEngineParameters
        readGranularParameters(double hostBpm) const noexcept;
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
    void applyFxChainConfiguration() noexcept;
    void applyFxMacroAssignmentSnapshot() noexcept;
    void applyLfoConfigurationSnapshot() noexcept;
    void advanceLfoPhases(double bpm, std::optional<double> ppqPosition,
                          int numSamples) noexcept;
    void updateFxModuleParameters(double bpm) noexcept;
    void updateLfoOverrides(double bpm, std::optional<double> ppqPosition,
                            int segmentSamples) noexcept;
    [[nodiscard]] float readFxParameterValue(
        std::size_t type, std::size_t slot, std::size_t parameterIndex,
        float fallback = 0.0f
    ) const noexcept;
    [[nodiscard]] float readFxBaseParameterValue(
        std::size_t type, std::size_t slot, std::size_t parameterIndex,
        float fallback = 0.0f
    ) const noexcept;

    juce::AudioProcessorValueTreeState parameters;

    std::atomic<float>* dryWetParameter = nullptr;
    std::atomic<float>* outputGainParameter = nullptr;
    std::atomic<float>* grainDensityParameter = nullptr;
    std::atomic<float>* grainDensityModeParameter = nullptr;
    std::atomic<float>* grainDensityDivisionParameter = nullptr;
    std::atomic<float>* granularSourceModeParameter = nullptr;
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
    std::atomic<float>* macroXParameter = nullptr;
    std::atomic<float>* macroYParameter = nullptr;
    std::atomic<float>* reverbEnabledParameter = nullptr;
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
    std::atomic<float>* modulationEnabledParameter = nullptr;
    std::atomic<float>* driveModeParameter = nullptr;
    std::atomic<float>* driveEnabledParameter = nullptr;
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
    std::atomic<float>* delayEnabledParameter = nullptr;
    std::atomic<float>* tapeDriveParameter = nullptr;
    std::atomic<float>* tapeEnabledParameter = nullptr;
    std::atomic<float>* tapeWowParameter = nullptr;
    std::atomic<float>* tapeFlutterParameter = nullptr;
    std::atomic<float>* tapeWearParameter = nullptr;
    std::atomic<float>* tapeMixParameter = nullptr;
    std::atomic<float>* tapeOutputParameter = nullptr;
    std::atomic<float>* bitcrushBitsParameter = nullptr;
    std::atomic<float>* bitcrushEnabledParameter = nullptr;
    std::atomic<float>* bitcrushRateParameter = nullptr;
    std::atomic<float>* bitcrushModeParameter = nullptr;
    std::atomic<float>* bitcrushMixParameter = nullptr;
    std::atomic<float>* bitcrushOutputParameter = nullptr;
    std::atomic<float>* filterModeParameter = nullptr;
    std::atomic<float>* filterEnabledParameter = nullptr;
    std::atomic<float>* filterCutoffParameter = nullptr;
    std::atomic<float>* filterResonanceParameter = nullptr;
    std::atomic<float>* filterDriveParameter = nullptr;
    std::atomic<float>* filterMixParameter = nullptr;
    std::atomic<float>* filterOutputParameter = nullptr;
    std::atomic<float>* compressorThresholdParameter = nullptr;
    std::atomic<float>* compressorEnabledParameter = nullptr;
    std::atomic<float>* compressorRatioParameter = nullptr;
    std::atomic<float>* compressorAttackParameter = nullptr;
    std::atomic<float>* compressorReleaseParameter = nullptr;
    std::atomic<float>* compressorKneeParameter = nullptr;
    std::atomic<float>* compressorMakeupParameter = nullptr;
    std::atomic<float>* compressorMixParameter = nullptr;
    std::atomic<float>* compressorStereoLinkParameter = nullptr;

    struct FxRawParameterBank
    {
        std::atomic<float>* enabled = nullptr;
        std::array<std::atomic<float>*, 9> values {};
    };
    std::array<std::array<FxRawParameterBank, hypercolor::plugin::maximumFxInstancesPerType>,
                hypercolor::plugin::fxTypeCount> fxRawParameters {};
    std::array<std::array<hypercolor::dsp::fx::EffectModule*, hypercolor::plugin::maximumFxInstancesPerType>,
                hypercolor::plugin::fxTypeCount> fxModuleInstances {};

    juce::AudioBuffer<float> dryBuffer;
    hypercolor::dsp::granular::GranularEngine granularEngine;
    hypercolor::dsp::fx::SpaceReverb spaceReverb;
    hypercolor::dsp::fx::ModulationEffect modulationEffect;
    hypercolor::dsp::fx::DriveEffect driveEffect;
    hypercolor::dsp::fx::DelayEffect delayEffect;
    hypercolor::dsp::fx::TapeEffect tapeEffect;
    hypercolor::dsp::fx::BitcrushEffect bitcrushEffect;
    hypercolor::dsp::fx::FilterEffect filterEffect;
    hypercolor::dsp::fx::CompressorEffect compressorEffect;
    std::array<hypercolor::dsp::fx::SpaceReverb, 2> extraSpaceReverbs;
    std::array<hypercolor::dsp::fx::ModulationEffect, 2> extraModulationEffects;
    std::array<hypercolor::dsp::fx::DriveEffect, 2> extraDriveEffects;
    std::array<hypercolor::dsp::fx::TapeEffect, 2> extraTapeEffects;
    std::array<hypercolor::dsp::fx::BitcrushEffect, 2> extraBitcrushEffects;
    std::array<hypercolor::dsp::fx::FilterEffect, 2> extraFilterEffects;
    std::array<hypercolor::dsp::fx::CompressorEffect, 2> extraCompressorEffects;
    std::array<hypercolor::dsp::fx::DelayEffect, 2> extraDelayEffects;
    hypercolor::dsp::fx::EffectChain effectChain;
    hypercolor::dsp::granular::GranularTelemetryQueue granularTelemetryQueue;
    std::uint64_t telemetryPublishIntervalSamples = 0;
    std::uint64_t telemetrySamplesSincePublication = 0;
    std::uint64_t nextTelemetrySequence = 0;
    std::atomic<float> lastValidHostBpm { 120.0f };
    std::atomic<std::uint32_t> fxConfigurationSequence { 0 };
    std::atomic<std::uint8_t> fxConfigurationCount { 0 };
    std::array<std::atomic<std::uint8_t>, hypercolor::plugin::maximumFxChainModules>
        fxConfigurationPacked {};
    std::array<std::atomic<std::uint64_t>, hypercolor::plugin::maximumFxChainModules>
        fxConfigurationIdentities {};
    std::atomic<std::uint64_t> nextFxInstanceIdentity { 25 };
    std::array<std::array<std::uint64_t, hypercolor::plugin::maximumFxInstancesPerType>,
               hypercolor::plugin::fxTypeCount> audioFxInstanceIdentities {};

    struct MacroAssignmentRuntimeAtomic
    {
        std::atomic<bool> active { false };
        std::atomic<std::uint8_t> axis { 0 };
        std::atomic<std::uint8_t> type { 0 };
        std::atomic<std::uint8_t> slot { 0 };
        std::atomic<std::uint8_t> parameterIndex { 0 };
        std::atomic<std::uint64_t> instanceIdentity { 0 };
        std::atomic<float> minimum { 0.0f };
        std::atomic<float> maximum { 1.0f };
        std::atomic<float> parameterStart { 0.0f };
        std::atomic<float> parameterInterval { 0.0f };
    };
    struct MacroAssignmentRuntime
    {
        bool active = false;
        hypercolor::plugin::MacroAxis axis = hypercolor::plugin::MacroAxis::x;
        std::uint8_t type = 0;
        std::uint8_t slot = 0;
        std::uint8_t parameterIndex = 0;
        std::uint64_t instanceIdentity = 0;
        float minimum = 0.0f;
        float maximum = 1.0f;
        float parameterStart = 0.0f;
        float parameterInterval = 0.0f;
    };
    struct MacroOverride
    {
        bool active = false;
        float value = 0.0f;
    };
    std::array<MacroAssignmentRuntimeAtomic,
               hypercolor::plugin::maximumFxMacroAssignments> macroAssignmentRuntimeAtomic {};
    std::array<MacroAssignmentRuntime,
               hypercolor::plugin::maximumFxMacroAssignments> audioMacroAssignments {};
    std::array<std::array<std::array<MacroOverride, 9>,
                          hypercolor::plugin::maximumFxInstancesPerType>,
               hypercolor::plugin::fxTypeCount> audioFxMacroOverrides {};
    std::atomic<std::uint32_t> macroAssignmentSequence { 0 };
    std::atomic<std::uint8_t> macroAssignmentCount { 0 };
    juce::CriticalSection macroAssignmentsLock;
    hypercolor::plugin::FxMacroAssignmentSet macroAssignmentsMessageThread;
    struct LfoModuleRuntime
    {
        std::uint64_t identity = 0;
        bool enabled = false;
        hypercolor::dsp::modulation::LfoWaveform waveform
            = hypercolor::dsp::modulation::LfoWaveform::sine;
        hypercolor::plugin::LfoRateMode rateMode = hypercolor::plugin::LfoRateMode::free;
        hypercolor::plugin::LfoDivision division = hypercolor::plugin::LfoDivision::quarter;
        float rateHz = 1.0f, depth = 1.0f, phase = 0.0f, offset = 0.0f;
        hypercolor::dsp::modulation::Lfo::CustomWaveform customWaveform {};
    };
    struct LfoAssignmentRuntime
    {
        bool active = false;
        std::uint64_t lfoIdentity = 0, fxIdentity = 0;
        std::uint8_t type = 0, slot = 0, parameterIndex = 0, lfoSlot = 0;
        float amount = 1.0f;
        bool bipolar = true, inverted = false;
        float parameterStart = 0.0f, parameterEnd = 1.0f, parameterInterval = 0.0f;
        float parameterSkew = 1.0f;
    };
    struct LfoOverride { bool active = false; float value = 0.0f; };
    struct LfoTargetRange
    {
        float start = 0.0f, end = 1.0f, skew = 1.0f, interval = 0.0f;
    };
    struct LfoModuleRuntimeAtomic
    {
        std::atomic<std::uint64_t> identity { 0 };
        std::atomic<bool> enabled { false };
        std::atomic<std::uint8_t> waveform { 0 }, rateMode { 0 }, division { 3 };
        std::atomic<float> rateHz { 1.0f }, depth { 1.0f }, phase { 0.0f }, offset { 0.0f };
        std::array<std::atomic<float>, hypercolor::dsp::modulation::Lfo::customWaveformSize>
            customWaveform {};
    };
    struct LfoAssignmentRuntimeAtomic
    {
        std::atomic<bool> active { false }, bipolar { true }, inverted { false };
        std::atomic<std::uint64_t> lfoIdentity { 0 }, fxIdentity { 0 };
        std::atomic<std::uint8_t> type { 0 }, slot { 0 }, parameterIndex { 0 }, lfoSlot { 0 };
        std::atomic<float> amount { 1.0f }, parameterStart { 0.0f }, parameterEnd { 1.0f }, parameterInterval { 0.0f }, parameterSkew { 1.0f };
    };
    std::array<LfoModuleRuntimeAtomic, hypercolor::plugin::maximumLfoModules>
        lfoRuntimeAtomic {};
    std::array<LfoAssignmentRuntimeAtomic, hypercolor::plugin::maximumLfoAssignments>
        lfoAssignmentRuntimeAtomic {};
    std::array<LfoModuleRuntime, hypercolor::plugin::maximumLfoModules>
        audioLfoModules {};
    std::array<LfoAssignmentRuntime, hypercolor::plugin::maximumLfoAssignments>
        audioLfoAssignments {};
    std::array<hypercolor::dsp::modulation::Lfo, hypercolor::plugin::maximumLfoModules>
        lfoOscillators {};
    std::array<std::atomic<float>, hypercolor::plugin::maximumLfoModules>
        lfoDisplayPhases {};
    std::array<std::array<std::array<LfoOverride, 9>, hypercolor::plugin::maximumFxInstancesPerType>,
               hypercolor::plugin::fxTypeCount> audioFxLfoOverrides {};
    std::array<std::array<std::array<LfoTargetRange, 9>, hypercolor::plugin::maximumFxInstancesPerType>,
               hypercolor::plugin::fxTypeCount> audioFxLfoRanges {};
    std::atomic<std::uint32_t> lfoConfigurationSequence { 0 };
    std::atomic<std::uint8_t> lfoModuleCount { 0 };
    std::atomic<std::uint16_t> lfoAssignmentCount { 0 };
    std::atomic<std::uint64_t> nextLfoIdentity { 1 };
    juce::CriticalSection lfoConfigurationLock;
    hypercolor::plugin::LfoConfiguration lfoConfigurationMessageThread;
    double currentBlockBpm = 120.0;
    double currentBlockPpq = 0.0;
    bool currentBlockPpqValid = false;
    std::uint32_t audioLfoSnapshotSequence = 0;
    bool granularEnginePrepared = false;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        dryWetSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        outputGainSmoother;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HypercolorAudioProcessor)
};
