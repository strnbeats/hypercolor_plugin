#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "DensitySync.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace
{
static_assert(
    std::atomic<float>::is_always_lock_free,
    "Host BPM cache must remain lock-free on the audio thread"
);
static_assert(std::atomic<std::uint8_t>::is_always_lock_free
              && std::atomic<std::uint32_t>::is_always_lock_free
              && std::atomic<std::uint16_t>::is_always_lock_free
              && std::atomic<std::uint64_t>::is_always_lock_free
              && std::atomic<bool>::is_always_lock_free,
              "FX and macro snapshots must remain lock-free on the audio thread");

constexpr auto stateTreeType = "HYPERCOLOR_STATE";
constexpr auto stateSchemaProperty = "schemaVersion";
constexpr auto fxChainStateType = "FX_CHAIN";
constexpr auto fxMacroAssignmentsStateType = "FX_MACRO_ASSIGNMENTS";
constexpr auto lfoConfigurationStateType = "LFO_CONFIGURATION";
constexpr auto nextFxIdentityProperty = "nextFxIdentity";
constexpr auto nextLfoIdentityProperty = "nextLfoIdentity";
constexpr int currentStateSchemaVersion = 7;
// Large enough to leave room for future bounded preset/state data while still
// rejecting clearly unreasonable input before parsing it as XML.
constexpr int maxStateSizeBytes = 64 * 1024 * 1024;
constexpr double parameterSmoothingSeconds = 0.02;
constexpr double telemetryUpdatesPerSecond = 30.0;
constexpr std::array<const char*, 8> fxEnabledLegacyIDs {
    hypercolor::ParameterIDs::reverbEnabled,
    hypercolor::ParameterIDs::modulationEnabled,
    hypercolor::ParameterIDs::driveEnabled,
    hypercolor::ParameterIDs::tapeEnabled,
    hypercolor::ParameterIDs::bitcrushEnabled,
    hypercolor::ParameterIDs::filterEnabled,
    hypercolor::ParameterIDs::compressorEnabled,
    hypercolor::ParameterIDs::delayEnabled
};
constexpr std::array<std::array<const char*, 9>, 8> fxLegacyParameterIDs {{
    {{ hypercolor::ParameterIDs::reverbMix, hypercolor::ParameterIDs::reverbSize, hypercolor::ParameterIDs::reverbDecay, hypercolor::ParameterIDs::reverbDamping, hypercolor::ParameterIDs::reverbPreDelay, hypercolor::ParameterIDs::reverbWidth, nullptr, nullptr, nullptr }},
    {{ hypercolor::ParameterIDs::modulationMode, hypercolor::ParameterIDs::modulationRate, hypercolor::ParameterIDs::modulationDepth, hypercolor::ParameterIDs::modulationBaseDelay, hypercolor::ParameterIDs::modulationFeedback, hypercolor::ParameterIDs::modulationMix, hypercolor::ParameterIDs::modulationWidth, nullptr, nullptr }},
    {{ hypercolor::ParameterIDs::driveMode, hypercolor::ParameterIDs::driveAmount, hypercolor::ParameterIDs::driveTone, hypercolor::ParameterIDs::driveMix, hypercolor::ParameterIDs::driveOutput, nullptr, nullptr, nullptr, nullptr }},
    {{ hypercolor::ParameterIDs::tapeDrive, hypercolor::ParameterIDs::tapeWow, hypercolor::ParameterIDs::tapeFlutter, hypercolor::ParameterIDs::tapeWear, hypercolor::ParameterIDs::tapeMix, hypercolor::ParameterIDs::tapeOutput, nullptr, nullptr, nullptr }},
    {{ hypercolor::ParameterIDs::bitcrushBits, hypercolor::ParameterIDs::bitcrushRate, hypercolor::ParameterIDs::bitcrushMode, hypercolor::ParameterIDs::bitcrushMix, hypercolor::ParameterIDs::bitcrushOutput, nullptr, nullptr, nullptr, nullptr }},
    {{ hypercolor::ParameterIDs::filterMode, hypercolor::ParameterIDs::filterCutoff, hypercolor::ParameterIDs::filterResonance, hypercolor::ParameterIDs::filterDrive, hypercolor::ParameterIDs::filterMix, hypercolor::ParameterIDs::filterOutput, nullptr, nullptr, nullptr }},
    {{ hypercolor::ParameterIDs::compressorThreshold, hypercolor::ParameterIDs::compressorRatio, hypercolor::ParameterIDs::compressorAttack, hypercolor::ParameterIDs::compressorRelease, hypercolor::ParameterIDs::compressorKnee, hypercolor::ParameterIDs::compressorMakeup, hypercolor::ParameterIDs::compressorMix, hypercolor::ParameterIDs::compressorStereoLink, nullptr }},
    {{ hypercolor::ParameterIDs::delayMode, hypercolor::ParameterIDs::delaySync, hypercolor::ParameterIDs::delayTime, hypercolor::ParameterIDs::delayDivision, hypercolor::ParameterIDs::delayFeedback, hypercolor::ParameterIDs::delayLowCut, hypercolor::ParameterIDs::delayHighCut, hypercolor::ParameterIDs::delayMix, hypercolor::ParameterIDs::delayWidth }}
}};

constexpr float dryWetMinimum = 0.0f;
constexpr float dryWetMaximum = 100.0f;
constexpr float dryWetDefault = 100.0f;

constexpr float outputGainMinimumDb = -24.0f;
constexpr float outputGainMaximumDb = 12.0f;
constexpr float outputGainDefaultDb = 0.0f;

constexpr float granularRandomMinimumPercent = 0.0f;
constexpr float granularRandomMaximumPercent = 100.0f;
constexpr float granularRandomDefaultPercent = 0.0f;
constexpr int granularCharacterMinimumIndex = static_cast<int>(
    hypercolor::dsp::granular::GranularCharacter::ControlledChaos
);
constexpr int granularCharacterMaximumIndex = static_cast<int>(
    hypercolor::dsp::granular::GranularCharacter::Atmospheric
);
constexpr int granularCharacterDefaultIndex = static_cast<int>(
    hypercolor::dsp::granular::GranularCharacter::Musical
);
constexpr int grainEnvelopeMinimumIndex = 0;
constexpr int grainEnvelopeMaximumIndex = 3;
constexpr int grainEnvelopeDefaultIndex = 0;
constexpr int grainDensityModeMinimumIndex = 0;
constexpr int grainDensityModeMaximumIndex = 1;
constexpr int grainDensityModeDefaultIndex = 0;
constexpr int granularSourceModeMinimumIndex = 0;
constexpr int granularSourceModeMaximumIndex = 1;
constexpr int granularSourceModeLiveIndex = 0;
constexpr int grainDensityDivisionMinimumIndex = static_cast<int>(
    hypercolor::density::Division::quarter
);
constexpr int grainDensityDivisionMaximumIndex = static_cast<int>(
    hypercolor::density::Division::sixtyFourthTriplet
);
constexpr int grainDensityDivisionDefaultIndex = static_cast<int>(
    hypercolor::density::Division::thirtySecond
);

constexpr std::array fxEnableParameterIDs {
    hypercolor::ParameterIDs::reverbEnabled,
    hypercolor::ParameterIDs::modulationEnabled,
    hypercolor::ParameterIDs::driveEnabled,
    hypercolor::ParameterIDs::tapeEnabled,
    hypercolor::ParameterIDs::bitcrushEnabled,
    hypercolor::ParameterIDs::filterEnabled,
    hypercolor::ParameterIDs::compressorEnabled,
    hypercolor::ParameterIDs::delayEnabled
};

constexpr hypercolor::dsp::granular::GranularEngineParameters
    defaultGranularParameters;

bool readFiniteNumber(const juce::var& value, double& result)
{
    if (value.isBool())
    {
        result = static_cast<bool>(value) ? 1.0 : 0.0;
        return true;
    }

    if (value.isInt() || value.isInt64() || value.isDouble())
    {
        result = static_cast<double>(value);
        return std::isfinite(result);
    }

    if (!value.isString())
        return false;

    const auto text = value.toString().trim();

    if (text.isEmpty())
        return false;

    const auto* begin = text.toRawUTF8();
    char* end = nullptr;
    errno = 0;
    const auto parsed = std::strtod(begin, &end);

    if (begin == end
        || end == nullptr
        || *end != '\0'
        || errno == ERANGE
        || !std::isfinite(parsed))
    {
        return false;
    }

    result = parsed;
    return true;
}

bool hasValidParameterValue(
    const juce::ValueTree& state,
    const juce::String& parameterID,
    float minimum,
    float maximum
)
{
    const auto child = state.getChildWithProperty("id", parameterID);

    if (!child.isValid() || !child.hasProperty("value"))
        return false;

    double numericValue = 0.0;

    if (!readFiniteNumber(child.getProperty("value"), numericValue))
        return false;

    return numericValue >= minimum
        && numericValue <= maximum;
}

bool hasValidBooleanParameterValue(
    const juce::ValueTree& state,
    const juce::String& parameterID
)
{
    const auto child = state.getChildWithProperty("id", parameterID);

    if (!child.isValid() || !child.hasProperty("value"))
        return false;

    double numericValue = 0.0;
    return readFiniteNumber(child.getProperty("value"), numericValue)
        && (numericValue == 0.0 || numericValue == 1.0);
}

bool hasValidChoiceParameterValue(
    const juce::ValueTree& state,
    const juce::String& parameterID,
    int minimum,
    int maximum
)
{
    const auto child = state.getChildWithProperty("id", parameterID);

    if (!child.isValid() || !child.hasProperty("value"))
        return false;

    double numericValue = 0.0;
    return readFiniteNumber(child.getProperty("value"), numericValue)
        && numericValue == std::floor(numericValue)
        && numericValue >= static_cast<double>(minimum)
        && numericValue <= static_cast<double>(maximum);
}

hypercolor::dsp::granular::GranularCharacter readGranularCharacter(
    float value
) noexcept
{
    if (!std::isfinite(value))
        return hypercolor::dsp::granular::GranularCharacter::Musical;

    const auto index = static_cast<int>(std::round(std::clamp(
        value,
        static_cast<float>(granularCharacterMinimumIndex),
        static_cast<float>(granularCharacterMaximumIndex)
    )));
    return static_cast<hypercolor::dsp::granular::GranularCharacter>(index);
}

hypercolor::dsp::granular::GrainWindow readGrainEnvelope(float value) noexcept
{
    if (!std::isfinite(value))
        return hypercolor::dsp::granular::GrainWindow::hann;

    const auto index = static_cast<int>(std::round(std::clamp(
        value,
        static_cast<float>(grainEnvelopeMinimumIndex),
        static_cast<float>(grainEnvelopeMaximumIndex)
    )));
    return static_cast<hypercolor::dsp::granular::GrainWindow>(index);
}

bool addDefaultParameterIfMissing(
    juce::ValueTree& state,
    const juce::ValueTree& parameterTemplateState,
    const juce::String& parameterID,
    double defaultValue
)
{
    if (state.getChildWithProperty("id", parameterID).isValid())
        return true;

    const auto templateChild = parameterTemplateState.getChildWithProperty(
        "id",
        parameterID
    );

    if (!templateChild.isValid())
        return false;

    auto defaultChild = templateChild.createCopy();
    defaultChild.setProperty("value", defaultValue, nullptr);
    state.addChild(defaultChild, -1, nullptr);
    return true;
}

bool migrateLegacyDensityDivisionOrder(
    juce::ValueTree& state,
    int schemaVersion
)
{
    // Schema 0/1 states used the original order:
    // 1/4, 1/8, 1/16, 1/32, 1/4T, 1/8T, 1/16T, 1/32T.
    // A present parameter is therefore migrated by semantic index.  Missing
    // parameters are deliberately left untouched so the normal defaulting
    // path can provide the new 1/32 default at index 6.
    if (schemaVersion >= 2)
        return true;

    auto divisionState = state.getChildWithProperty(
        "id",
        hypercolor::ParameterIDs::grainDensityDivision
    );
    if (!divisionState.isValid())
        return true;

    double rawValue = 0.0;
    if (!readFiniteNumber(divisionState.getProperty("value"), rawValue)
        || rawValue != std::floor(rawValue)
        || rawValue < 0.0
        || rawValue > 7.0)
        return false;

    constexpr std::array legacyToCurrent { 0, 2, 4, 6, 1, 3, 5, 7 };
    const auto legacyIndex = static_cast<std::size_t>(rawValue);
    divisionState.setProperty(
        "value",
        legacyToCurrent[legacyIndex],
        nullptr
    );
    return true;
}
}

HypercolorAudioProcessor::HypercolorAudioProcessor()
    : AudioProcessor(
        BusesProperties()
            .withInput(
                "Input",
                juce::AudioChannelSet::stereo(),
                true
            )
            .withOutput(
                "Output",
                juce::AudioChannelSet::stereo(),
                true
            )
    ),
      parameters(
          *this,
          nullptr,
          stateTreeType,
          createParameterLayout()
      )
{
    fxModuleInstances[0] = { &spaceReverb, &extraSpaceReverbs[0], &extraSpaceReverbs[1] };
    fxModuleInstances[1] = { &modulationEffect, &extraModulationEffects[0], &extraModulationEffects[1] };
    fxModuleInstances[2] = { &driveEffect, &extraDriveEffects[0], &extraDriveEffects[1] };
    fxModuleInstances[3] = { &tapeEffect, &extraTapeEffects[0], &extraTapeEffects[1] };
    fxModuleInstances[4] = { &bitcrushEffect, &extraBitcrushEffects[0], &extraBitcrushEffects[1] };
    fxModuleInstances[5] = { &filterEffect, &extraFilterEffects[0], &extraFilterEffects[1] };
    fxModuleInstances[6] = { &compressorEffect, &extraCompressorEffects[0], &extraCompressorEffects[1] };
    fxModuleInstances[7] = { &delayEffect, &extraDelayEffects[0], &extraDelayEffects[1] };

    for (auto& typeModules : fxModuleInstances)
        for (auto* module : typeModules)
            (void) effectChain.addModule(module);

    // Keep the historical eight-effect signal order as the default chain.
    constexpr std::array<std::uint8_t, 8> legacyOrder { 0, 3, 6, 9, 12, 15, 18, 21 };
    (void) effectChain.setActiveOrder(legacyOrder);

    hypercolor::plugin::FxChainConfiguration defaultFxConfiguration;
    defaultFxConfiguration.count = 8;
    constexpr std::array<hypercolor::plugin::FxType, 8> initialTypes {
        hypercolor::plugin::FxType::space,
        hypercolor::plugin::FxType::modulation,
        hypercolor::plugin::FxType::drive,
        hypercolor::plugin::FxType::tape,
        hypercolor::plugin::FxType::bitcrush,
        hypercolor::plugin::FxType::filter,
        hypercolor::plugin::FxType::dynamics,
        hypercolor::plugin::FxType::delay
    };
    for (std::size_t i = 0; i < initialTypes.size(); ++i)
        defaultFxConfiguration.order[i] = {
            initialTypes[i], 0,
            hypercolor::plugin::legacyFxIdentity(initialTypes[i], 0)
        };
    (void) setFxChainConfiguration(defaultFxConfiguration);

    for (std::size_t type = 0; type < hypercolor::plugin::fxTypeCount; ++type)
    {
        for (std::size_t slot = 0; slot < hypercolor::plugin::maximumFxInstancesPerType; ++slot)
        {
            auto& bank = fxRawParameters[type][slot];
            bank.enabled = parameters.getRawParameterValue(
                hypercolor::plugin::fxEnabledParameterID(
                    static_cast<hypercolor::plugin::FxType>(type),
                    static_cast<std::uint8_t>(slot),
                    fxEnabledLegacyIDs[type]
                )
            );
            for (std::size_t value = 0; value < fxLegacyParameterIDs[type].size(); ++value)
            {
                const auto* legacyID = fxLegacyParameterIDs[type][value];
                if (legacyID != nullptr)
                    bank.values[value] = parameters.getRawParameterValue(
                        hypercolor::plugin::fxParameterID(
                            static_cast<hypercolor::plugin::FxType>(type),
                            static_cast<std::uint8_t>(slot),
                            legacyID
                        )
                    );
            }
        }
    }
    parameters.state.setProperty(
        stateSchemaProperty,
        currentStateSchemaVersion,
        nullptr
    );

    dryWetParameter = parameters.getRawParameterValue(
        hypercolor::ParameterIDs::dryWet
    );
    outputGainParameter = parameters.getRawParameterValue(
        hypercolor::ParameterIDs::outputGain
    );
    grainDensityParameter = parameters.getRawParameterValue(
        hypercolor::ParameterIDs::grainDensity
    );
    grainDensityModeParameter = parameters.getRawParameterValue(
        hypercolor::ParameterIDs::grainDensityMode
    );
    grainDensityDivisionParameter = parameters.getRawParameterValue(
        hypercolor::ParameterIDs::grainDensityDivision
    );
    granularSourceModeParameter = parameters.getRawParameterValue(
        hypercolor::ParameterIDs::granularSourceMode
    );
    grainSizeParameter = parameters.getRawParameterValue(
        hypercolor::ParameterIDs::grainSize
    );
    grainPositionParameter = parameters.getRawParameterValue(
        hypercolor::ParameterIDs::grainPosition
    );
    grainPitchParameter = parameters.getRawParameterValue(
        hypercolor::ParameterIDs::grainPitch
    );
    grainReverseParameter = parameters.getRawParameterValue(
        hypercolor::ParameterIDs::grainReverse
    );
    grainCharacterParameter = parameters.getRawParameterValue(
        hypercolor::ParameterIDs::grainCharacter
    );
    grainPitchRandomParameter = parameters.getRawParameterValue(
        hypercolor::ParameterIDs::grainPitchRandom
    );
    grainPositionRandomParameter = parameters.getRawParameterValue(
        hypercolor::ParameterIDs::grainPositionRandom
    );
    grainTimingJitterParameter = parameters.getRawParameterValue(
        hypercolor::ParameterIDs::grainTimingJitter
    );
    grainStereoSpreadParameter = parameters.getRawParameterValue(
        hypercolor::ParameterIDs::grainStereoSpread
    );
    grainPanRandomParameter = parameters.getRawParameterValue(
        hypercolor::ParameterIDs::grainPanRandom
    );
    grainEnvelopeParameter = parameters.getRawParameterValue(
        hypercolor::ParameterIDs::grainEnvelope
    );
    macroXParameter = parameters.getRawParameterValue(
        hypercolor::ParameterIDs::macroX
    );
    macroYParameter = parameters.getRawParameterValue(
        hypercolor::ParameterIDs::macroY
    );
    reverbEnabledParameter = parameters.getRawParameterValue(
        hypercolor::ParameterIDs::reverbEnabled
    );
    reverbMixParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::reverbMix);
    reverbSizeParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::reverbSize);
    reverbDecayParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::reverbDecay);
    reverbDampingParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::reverbDamping);
    reverbPreDelayParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::reverbPreDelay);
    reverbWidthParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::reverbWidth);
    modulationModeParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::modulationMode);
    modulationRateParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::modulationRate);
    modulationDepthParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::modulationDepth);
    modulationBaseDelayParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::modulationBaseDelay);
    modulationFeedbackParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::modulationFeedback);
    modulationMixParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::modulationMix);
    modulationWidthParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::modulationWidth);
    modulationEnabledParameter = parameters.getRawParameterValue(
        hypercolor::ParameterIDs::modulationEnabled
    );
    driveModeParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::driveMode);
    driveEnabledParameter = parameters.getRawParameterValue(
        hypercolor::ParameterIDs::driveEnabled
    );
    driveAmountParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::driveAmount);
    driveToneParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::driveTone);
    driveMixParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::driveMix);
    driveOutputParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::driveOutput);
    delayModeParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::delayMode);
    delaySyncParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::delaySync);
    delayTimeParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::delayTime);
    delayDivisionParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::delayDivision);
    delayFeedbackParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::delayFeedback);
    delayLowCutParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::delayLowCut);
    delayHighCutParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::delayHighCut);
    delayMixParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::delayMix);
    delayWidthParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::delayWidth);
    delayEnabledParameter = parameters.getRawParameterValue(
        hypercolor::ParameterIDs::delayEnabled
    );
    tapeDriveParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::tapeDrive);
    tapeEnabledParameter = parameters.getRawParameterValue(
        hypercolor::ParameterIDs::tapeEnabled
    );
    tapeWowParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::tapeWow);
    tapeFlutterParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::tapeFlutter);
    tapeWearParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::tapeWear);
    tapeMixParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::tapeMix);
    tapeOutputParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::tapeOutput);
    bitcrushBitsParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::bitcrushBits);
    bitcrushEnabledParameter = parameters.getRawParameterValue(
        hypercolor::ParameterIDs::bitcrushEnabled
    );
    bitcrushRateParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::bitcrushRate);
    bitcrushModeParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::bitcrushMode);
    bitcrushMixParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::bitcrushMix);
    bitcrushOutputParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::bitcrushOutput);
    filterModeParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::filterMode);
    filterEnabledParameter = parameters.getRawParameterValue(
        hypercolor::ParameterIDs::filterEnabled
    );
    filterCutoffParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::filterCutoff);
    filterResonanceParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::filterResonance);
    filterDriveParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::filterDrive);
    filterMixParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::filterMix);
    filterOutputParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::filterOutput);
    compressorThresholdParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::compressorThreshold);
    compressorEnabledParameter = parameters.getRawParameterValue(
        hypercolor::ParameterIDs::compressorEnabled
    );
    compressorRatioParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::compressorRatio);
    compressorAttackParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::compressorAttack);
    compressorReleaseParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::compressorRelease);
    compressorKneeParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::compressorKnee);
    compressorMakeupParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::compressorMakeup);
    compressorMixParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::compressorMix);
    compressorStereoLinkParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::compressorStereoLink);

    jassert(dryWetParameter != nullptr);
    jassert(outputGainParameter != nullptr);
    jassert(grainDensityParameter != nullptr);
    jassert(grainDensityModeParameter != nullptr);
    jassert(grainDensityDivisionParameter != nullptr);
    jassert(granularSourceModeParameter != nullptr);
    jassert(grainSizeParameter != nullptr);
    jassert(grainPositionParameter != nullptr);
    jassert(grainPitchParameter != nullptr);
    jassert(grainReverseParameter != nullptr);
    jassert(grainCharacterParameter != nullptr);
    jassert(grainPitchRandomParameter != nullptr);
    jassert(grainPositionRandomParameter != nullptr);
    jassert(grainTimingJitterParameter != nullptr);
    jassert(grainStereoSpreadParameter != nullptr);
    jassert(grainPanRandomParameter != nullptr);
    jassert(grainEnvelopeParameter != nullptr);
    jassert(reverbEnabledParameter != nullptr);
    jassert(reverbMixParameter != nullptr);
    jassert(reverbSizeParameter != nullptr);
    jassert(reverbDecayParameter != nullptr);
    jassert(reverbDampingParameter != nullptr);
    jassert(reverbPreDelayParameter != nullptr);
    jassert(reverbWidthParameter != nullptr);
    jassert(modulationModeParameter != nullptr);
    jassert(modulationRateParameter != nullptr);
    jassert(modulationDepthParameter != nullptr);
    jassert(modulationBaseDelayParameter != nullptr);
    jassert(modulationFeedbackParameter != nullptr);
    jassert(modulationMixParameter != nullptr);
    jassert(modulationWidthParameter != nullptr);
    jassert(modulationEnabledParameter != nullptr);
    jassert(driveModeParameter != nullptr);
    jassert(driveEnabledParameter != nullptr);
    jassert(driveAmountParameter != nullptr);
    jassert(driveToneParameter != nullptr);
    jassert(driveMixParameter != nullptr);
    jassert(driveOutputParameter != nullptr);
    jassert(delayModeParameter != nullptr);
    jassert(delaySyncParameter != nullptr);
    jassert(delayTimeParameter != nullptr);
    jassert(delayDivisionParameter != nullptr);
    jassert(delayFeedbackParameter != nullptr);
    jassert(delayLowCutParameter != nullptr);
    jassert(delayHighCutParameter != nullptr);
    jassert(delayMixParameter != nullptr);
    jassert(delayWidthParameter != nullptr);
    jassert(delayEnabledParameter != nullptr);
    jassert(tapeDriveParameter != nullptr);
    jassert(tapeEnabledParameter != nullptr);
    jassert(tapeWowParameter != nullptr);
    jassert(tapeFlutterParameter != nullptr);
    jassert(tapeWearParameter != nullptr);
    jassert(tapeMixParameter != nullptr);
    jassert(tapeOutputParameter != nullptr);
    jassert(bitcrushBitsParameter != nullptr);
    jassert(bitcrushEnabledParameter != nullptr);
    jassert(bitcrushRateParameter != nullptr);
    jassert(bitcrushModeParameter != nullptr);
    jassert(bitcrushMixParameter != nullptr);
    jassert(bitcrushOutputParameter != nullptr);
    jassert(filterModeParameter != nullptr);
    jassert(filterEnabledParameter != nullptr);
    jassert(filterCutoffParameter != nullptr);
    jassert(filterResonanceParameter != nullptr);
    jassert(filterDriveParameter != nullptr);
    jassert(filterMixParameter != nullptr);
    jassert(filterOutputParameter != nullptr);
    jassert(compressorThresholdParameter != nullptr);
    jassert(compressorEnabledParameter != nullptr);
    jassert(compressorRatioParameter != nullptr);
    jassert(compressorAttackParameter != nullptr);
    jassert(compressorReleaseParameter != nullptr);
    jassert(compressorKneeParameter != nullptr);
    jassert(compressorMakeupParameter != nullptr);
    jassert(compressorMixParameter != nullptr);
    jassert(compressorStereoLinkParameter != nullptr);

    dryWetSmoother.setCurrentAndTargetValue(dryWetDefault / 100.0f);
    outputGainSmoother.setCurrentAndTargetValue(1.0f);
}

juce::AudioProcessorValueTreeState::ParameterLayout
HypercolorAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    const auto percentAttributes = juce::AudioParameterFloatAttributes()
        .withLabel("%")
        .withStringFromValueFunction(
            [](float value, int)
            {
                return juce::String(value, 1);
            }
        );

    const auto decibelAttributes = juce::AudioParameterFloatAttributes()
        .withLabel("dB")
        .withStringFromValueFunction(
            [](float value, int)
            {
                return juce::String(value, 1);
            }
        );

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { hypercolor::ParameterIDs::dryWet, 1 },
        "Dry/Wet",
        juce::NormalisableRange<float> {
            dryWetMinimum,
            dryWetMaximum,
            0.1f
        },
        dryWetDefault,
        percentAttributes
    ));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { hypercolor::ParameterIDs::grainEnvelope, 1 },
        "Grain Envelope",
        juce::StringArray { "Hann", "Triangle", "Smooth", "Exponential" },
        grainEnvelopeDefaultIndex
    ));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { hypercolor::ParameterIDs::macroX, 1 },
        "Macro X",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f },
        0.5f
    ));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { hypercolor::ParameterIDs::macroY, 1 },
        "Macro Y",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f },
        0.5f
    ));

    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { hypercolor::ParameterIDs::reverbEnabled, 1 }, "Reverb Enabled", false));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::reverbMix, 1 }, "Reverb Mix", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 30.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::reverbSize, 1 }, "Reverb Size", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 50.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::reverbDecay, 1 }, "Reverb Decay", juce::NormalisableRange<float> { 0.1f, 20.0f, 0.01f }, 4.0f, juce::AudioParameterFloatAttributes().withLabel("s")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::reverbDamping, 1 }, "Reverb Damping", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 40.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::reverbPreDelay, 1 }, "Reverb Pre-Delay", juce::NormalisableRange<float> { 0.0f, 200.0f, 0.1f }, 20.0f, juce::AudioParameterFloatAttributes().withLabel("ms")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::reverbWidth, 1 }, "Reverb Width", juce::NormalisableRange<float> { 0.0f, 200.0f, 0.1f }, 100.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { hypercolor::ParameterIDs::modulationEnabled, 1 }, "Modulation Enabled", false));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { hypercolor::ParameterIDs::modulationMode, 1 }, "Modulation Mode", juce::StringArray { "Chorus", "Flanger" }, 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::modulationRate, 1 }, "Modulation Rate", juce::NormalisableRange<float> { 0.01f, 10.0f, 0.01f }, 0.3f, juce::AudioParameterFloatAttributes().withLabel("Hz")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::modulationDepth, 1 }, "Modulation Depth", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 50.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::modulationBaseDelay, 1 }, "Modulation Base Delay", juce::NormalisableRange<float> { 0.1f, 20.0f, 0.01f }, 7.0f, juce::AudioParameterFloatAttributes().withLabel("ms")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::modulationFeedback, 1 }, "Modulation Feedback", juce::NormalisableRange<float> { -95.0f, 95.0f, 0.1f }, 0.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::modulationMix, 1 }, "Modulation Mix", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 30.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::modulationWidth, 1 }, "Modulation Width", juce::NormalisableRange<float> { 0.0f, 200.0f, 0.1f }, 100.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { hypercolor::ParameterIDs::driveEnabled, 1 }, "Drive Enabled", false));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { hypercolor::ParameterIDs::driveMode, 1 }, "Drive Mode", juce::StringArray { "Soft Clip", "Hard Clip", "Tube" }, 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::driveAmount, 1 }, "Drive", juce::NormalisableRange<float> { 0.0f, 36.0f, 0.1f }, 0.0f, juce::AudioParameterFloatAttributes().withLabel("dB")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::driveTone, 1 }, "Drive Tone", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 50.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::driveMix, 1 }, "Drive Mix", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 100.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::driveOutput, 1 }, "Drive Output", juce::NormalisableRange<float> { -24.0f, 12.0f, 0.1f }, 0.0f, juce::AudioParameterFloatAttributes().withLabel("dB")));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { hypercolor::ParameterIDs::delayEnabled, 1 }, "Delay Enabled", false));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { hypercolor::ParameterIDs::delayMode, 1 }, "Delay Mode", juce::StringArray { "Stereo", "Ping-Pong" }, 0));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { hypercolor::ParameterIDs::delaySync, 1 }, "Delay Sync", false));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::delayTime, 1 }, "Delay Time", juce::NormalisableRange<float> { 1.0f, 2000.0f, 0.1f }, 350.0f, juce::AudioParameterFloatAttributes().withLabel("ms")));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { hypercolor::ParameterIDs::delayDivision, 1 }, "Delay Division", juce::StringArray { "1/1", "1/2", "1/4", "1/8", "1/16", "1/4 dotted", "1/8 dotted", "1/4 triplet", "1/8 triplet" }, 2));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::delayFeedback, 1 }, "Delay Feedback", juce::NormalisableRange<float> { 0.0f, 95.0f, 0.1f }, 35.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::delayLowCut, 1 }, "Delay Low Cut", juce::NormalisableRange<float> { 20.0f, 2000.0f, 1.0f }, 100.0f, juce::AudioParameterFloatAttributes().withLabel("Hz")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::delayHighCut, 1 }, "Delay High Cut", juce::NormalisableRange<float> { 1000.0f, 20000.0f, 1.0f }, 12000.0f, juce::AudioParameterFloatAttributes().withLabel("Hz")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::delayMix, 1 }, "Delay Mix", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 30.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::delayWidth, 1 }, "Delay Width", juce::NormalisableRange<float> { 0.0f, 200.0f, 0.1f }, 100.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { hypercolor::ParameterIDs::tapeEnabled, 1 }, "Tape Enabled", false));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::tapeDrive, 1 }, "Tape Drive", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 35.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::tapeWow, 1 }, "Tape Wow", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 15.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::tapeFlutter, 1 }, "Tape Flutter", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 10.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::tapeWear, 1 }, "Tape Wear", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 15.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::tapeMix, 1 }, "Tape Mix", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 100.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::tapeOutput, 1 }, "Tape Output", juce::NormalisableRange<float> { -24.0f, 12.0f, 0.1f }, 0.0f, juce::AudioParameterFloatAttributes().withLabel("dB")));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { hypercolor::ParameterIDs::bitcrushEnabled, 1 }, "Bitcrush Enabled", false));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::bitcrushBits, 1 }, "Bitcrush Bits", juce::NormalisableRange<float> { 4.0f, 24.0f, 1.0f }, 16.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::bitcrushRate, 1 }, "Bitcrush Rate", juce::NormalisableRange<float> { 500.0f, 44100.0f, 1.0f }, 22050.0f, juce::AudioParameterFloatAttributes().withLabel("Hz")));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { hypercolor::ParameterIDs::bitcrushMode, 1 }, "Bitcrush Mode", juce::StringArray { "Round", "Truncate" }, 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::bitcrushMix, 1 }, "Bitcrush Mix", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 100.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::bitcrushOutput, 1 }, "Bitcrush Output", juce::NormalisableRange<float> { -24.0f, 12.0f, 0.1f }, 0.0f, juce::AudioParameterFloatAttributes().withLabel("dB")));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { hypercolor::ParameterIDs::filterEnabled, 1 }, "Filter Enabled", false));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { hypercolor::ParameterIDs::filterMode, 1 }, "Filter Mode", juce::StringArray { "Low-Pass", "High-Pass", "Band-Pass", "Notch" }, 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::filterCutoff, 1 }, "Filter Cutoff", juce::NormalisableRange<float> { 20.0f, 20000.0f, 1.0f, 0.35f }, 10000.0f, juce::AudioParameterFloatAttributes().withLabel("Hz")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::filterResonance, 1 }, "Filter Resonance", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 20.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::filterDrive, 1 }, "Filter Drive", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 0.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::filterMix, 1 }, "Filter Mix", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 100.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::filterOutput, 1 }, "Filter Output", juce::NormalisableRange<float> { -24.0f, 12.0f, 0.1f }, 0.0f, juce::AudioParameterFloatAttributes().withLabel("dB")));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { hypercolor::ParameterIDs::compressorEnabled, 1 }, "Compressor Enabled", false));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::compressorThreshold, 1 }, "Compressor Threshold", juce::NormalisableRange<float> { -60.0f, 0.0f, 0.1f }, -18.0f, juce::AudioParameterFloatAttributes().withLabel("dB")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::compressorRatio, 1 }, "Compressor Ratio", juce::NormalisableRange<float> { 1.0f, 20.0f, 0.1f }, 4.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::compressorAttack, 1 }, "Compressor Attack", juce::NormalisableRange<float> { 0.1f, 100.0f, 0.1f }, 10.0f, juce::AudioParameterFloatAttributes().withLabel("ms")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::compressorRelease, 1 }, "Compressor Release", juce::NormalisableRange<float> { 10.0f, 1000.0f, 0.1f }, 100.0f, juce::AudioParameterFloatAttributes().withLabel("ms")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::compressorKnee, 1 }, "Compressor Knee", juce::NormalisableRange<float> { 0.0f, 24.0f, 0.1f }, 6.0f, juce::AudioParameterFloatAttributes().withLabel("dB")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::compressorMakeup, 1 }, "Compressor Makeup", juce::NormalisableRange<float> { 0.0f, 24.0f, 0.1f }, 0.0f, juce::AudioParameterFloatAttributes().withLabel("dB")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::compressorMix, 1 }, "Compressor Mix", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 100.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::compressorStereoLink, 1 }, "Compressor Stereo Link", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 100.0f, percentAttributes));


    // Fixed APVTS parameter banks for additional FX instances. Slot zero keeps
    // every legacy parameter ID; these IDs remain stable for host automation.
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { hypercolor::plugin::fxEnabledParameterID(hypercolor::plugin::FxType::space, 1, hypercolor::ParameterIDs::reverbEnabled), 1 }, "Reverb Enabled #2", false));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::space, 1, hypercolor::ParameterIDs::reverbMix), 1 }, "Reverb Mix #2", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 30.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::space, 1, hypercolor::ParameterIDs::reverbSize), 1 }, "Reverb Size #2", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 50.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::space, 1, hypercolor::ParameterIDs::reverbDecay), 1 }, "Reverb Decay #2", juce::NormalisableRange<float> { 0.1f, 20.0f, 0.01f }, 4.0f, juce::AudioParameterFloatAttributes().withLabel("s")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::space, 1, hypercolor::ParameterIDs::reverbDamping), 1 }, "Reverb Damping #2", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 40.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::space, 1, hypercolor::ParameterIDs::reverbPreDelay), 1 }, "Reverb Pre-Delay #2", juce::NormalisableRange<float> { 0.0f, 200.0f, 0.1f }, 20.0f, juce::AudioParameterFloatAttributes().withLabel("ms")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::space, 1, hypercolor::ParameterIDs::reverbWidth), 1 }, "Reverb Width #2", juce::NormalisableRange<float> { 0.0f, 200.0f, 0.1f }, 100.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { hypercolor::plugin::fxEnabledParameterID(hypercolor::plugin::FxType::modulation, 1, hypercolor::ParameterIDs::modulationEnabled), 1 }, "Modulation Enabled #2", false));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::modulation, 1, hypercolor::ParameterIDs::modulationMode), 1 }, "Modulation Mode #2", juce::StringArray { "Chorus", "Flanger" }, 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::modulation, 1, hypercolor::ParameterIDs::modulationRate), 1 }, "Modulation Rate #2", juce::NormalisableRange<float> { 0.01f, 10.0f, 0.01f }, 0.3f, juce::AudioParameterFloatAttributes().withLabel("Hz")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::modulation, 1, hypercolor::ParameterIDs::modulationDepth), 1 }, "Modulation Depth #2", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 50.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::modulation, 1, hypercolor::ParameterIDs::modulationBaseDelay), 1 }, "Modulation Base Delay #2", juce::NormalisableRange<float> { 0.1f, 20.0f, 0.01f }, 7.0f, juce::AudioParameterFloatAttributes().withLabel("ms")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::modulation, 1, hypercolor::ParameterIDs::modulationFeedback), 1 }, "Modulation Feedback #2", juce::NormalisableRange<float> { -95.0f, 95.0f, 0.1f }, 0.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::modulation, 1, hypercolor::ParameterIDs::modulationMix), 1 }, "Modulation Mix #2", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 30.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::modulation, 1, hypercolor::ParameterIDs::modulationWidth), 1 }, "Modulation Width #2", juce::NormalisableRange<float> { 0.0f, 200.0f, 0.1f }, 100.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { hypercolor::plugin::fxEnabledParameterID(hypercolor::plugin::FxType::drive, 1, hypercolor::ParameterIDs::driveEnabled), 1 }, "Drive Enabled #2", false));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::drive, 1, hypercolor::ParameterIDs::driveMode), 1 }, "Drive Mode #2", juce::StringArray { "Soft Clip", "Hard Clip", "Tube" }, 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::drive, 1, hypercolor::ParameterIDs::driveAmount), 1 }, "Drive #2", juce::NormalisableRange<float> { 0.0f, 36.0f, 0.1f }, 0.0f, juce::AudioParameterFloatAttributes().withLabel("dB")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::drive, 1, hypercolor::ParameterIDs::driveTone), 1 }, "Drive Tone #2", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 50.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::drive, 1, hypercolor::ParameterIDs::driveMix), 1 }, "Drive Mix #2", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 100.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::drive, 1, hypercolor::ParameterIDs::driveOutput), 1 }, "Drive Output #2", juce::NormalisableRange<float> { -24.0f, 12.0f, 0.1f }, 0.0f, juce::AudioParameterFloatAttributes().withLabel("dB")));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { hypercolor::plugin::fxEnabledParameterID(hypercolor::plugin::FxType::delay, 1, hypercolor::ParameterIDs::delayEnabled), 1 }, "Delay Enabled #2", false));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::delay, 1, hypercolor::ParameterIDs::delayMode), 1 }, "Delay Mode #2", juce::StringArray { "Stereo", "Ping-Pong" }, 0));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::delay, 1, hypercolor::ParameterIDs::delaySync), 1 }, "Delay Sync #2", false));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::delay, 1, hypercolor::ParameterIDs::delayTime), 1 }, "Delay Time #2", juce::NormalisableRange<float> { 1.0f, 2000.0f, 0.1f }, 350.0f, juce::AudioParameterFloatAttributes().withLabel("ms")));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::delay, 1, hypercolor::ParameterIDs::delayDivision), 1 }, "Delay Division #2", juce::StringArray { "1/1", "1/2", "1/4", "1/8", "1/16", "1/4 dotted", "1/8 dotted", "1/4 triplet", "1/8 triplet" }, 2));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::delay, 1, hypercolor::ParameterIDs::delayFeedback), 1 }, "Delay Feedback #2", juce::NormalisableRange<float> { 0.0f, 95.0f, 0.1f }, 35.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::delay, 1, hypercolor::ParameterIDs::delayLowCut), 1 }, "Delay Low Cut #2", juce::NormalisableRange<float> { 20.0f, 2000.0f, 1.0f }, 100.0f, juce::AudioParameterFloatAttributes().withLabel("Hz")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::delay, 1, hypercolor::ParameterIDs::delayHighCut), 1 }, "Delay High Cut #2", juce::NormalisableRange<float> { 1000.0f, 20000.0f, 1.0f }, 12000.0f, juce::AudioParameterFloatAttributes().withLabel("Hz")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::delay, 1, hypercolor::ParameterIDs::delayMix), 1 }, "Delay Mix #2", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 30.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::delay, 1, hypercolor::ParameterIDs::delayWidth), 1 }, "Delay Width #2", juce::NormalisableRange<float> { 0.0f, 200.0f, 0.1f }, 100.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { hypercolor::plugin::fxEnabledParameterID(hypercolor::plugin::FxType::tape, 1, hypercolor::ParameterIDs::tapeEnabled), 1 }, "Tape Enabled #2", false));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::tape, 1, hypercolor::ParameterIDs::tapeDrive), 1 }, "Tape Drive #2", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 35.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::tape, 1, hypercolor::ParameterIDs::tapeWow), 1 }, "Tape Wow #2", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 15.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::tape, 1, hypercolor::ParameterIDs::tapeFlutter), 1 }, "Tape Flutter #2", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 10.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::tape, 1, hypercolor::ParameterIDs::tapeWear), 1 }, "Tape Wear #2", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 15.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::tape, 1, hypercolor::ParameterIDs::tapeMix), 1 }, "Tape Mix #2", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 100.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::tape, 1, hypercolor::ParameterIDs::tapeOutput), 1 }, "Tape Output #2", juce::NormalisableRange<float> { -24.0f, 12.0f, 0.1f }, 0.0f, juce::AudioParameterFloatAttributes().withLabel("dB")));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { hypercolor::plugin::fxEnabledParameterID(hypercolor::plugin::FxType::bitcrush, 1, hypercolor::ParameterIDs::bitcrushEnabled), 1 }, "Bitcrush Enabled #2", false));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::bitcrush, 1, hypercolor::ParameterIDs::bitcrushBits), 1 }, "Bitcrush Bits #2", juce::NormalisableRange<float> { 4.0f, 24.0f, 1.0f }, 16.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::bitcrush, 1, hypercolor::ParameterIDs::bitcrushRate), 1 }, "Bitcrush Rate #2", juce::NormalisableRange<float> { 500.0f, 44100.0f, 1.0f }, 22050.0f, juce::AudioParameterFloatAttributes().withLabel("Hz")));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::bitcrush, 1, hypercolor::ParameterIDs::bitcrushMode), 1 }, "Bitcrush Mode #2", juce::StringArray { "Round", "Truncate" }, 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::bitcrush, 1, hypercolor::ParameterIDs::bitcrushMix), 1 }, "Bitcrush Mix #2", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 100.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::bitcrush, 1, hypercolor::ParameterIDs::bitcrushOutput), 1 }, "Bitcrush Output #2", juce::NormalisableRange<float> { -24.0f, 12.0f, 0.1f }, 0.0f, juce::AudioParameterFloatAttributes().withLabel("dB")));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { hypercolor::plugin::fxEnabledParameterID(hypercolor::plugin::FxType::filter, 1, hypercolor::ParameterIDs::filterEnabled), 1 }, "Filter Enabled #2", false));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::filter, 1, hypercolor::ParameterIDs::filterMode), 1 }, "Filter Mode #2", juce::StringArray { "Low-Pass", "High-Pass", "Band-Pass", "Notch" }, 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::filter, 1, hypercolor::ParameterIDs::filterCutoff), 1 }, "Filter Cutoff #2", juce::NormalisableRange<float> { 20.0f, 20000.0f, 1.0f, 0.35f }, 10000.0f, juce::AudioParameterFloatAttributes().withLabel("Hz")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::filter, 1, hypercolor::ParameterIDs::filterResonance), 1 }, "Filter Resonance #2", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 20.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::filter, 1, hypercolor::ParameterIDs::filterDrive), 1 }, "Filter Drive #2", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 0.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::filter, 1, hypercolor::ParameterIDs::filterMix), 1 }, "Filter Mix #2", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 100.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::filter, 1, hypercolor::ParameterIDs::filterOutput), 1 }, "Filter Output #2", juce::NormalisableRange<float> { -24.0f, 12.0f, 0.1f }, 0.0f, juce::AudioParameterFloatAttributes().withLabel("dB")));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { hypercolor::plugin::fxEnabledParameterID(hypercolor::plugin::FxType::dynamics, 1, hypercolor::ParameterIDs::compressorEnabled), 1 }, "Compressor Enabled #2", false));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::dynamics, 1, hypercolor::ParameterIDs::compressorThreshold), 1 }, "Compressor Threshold #2", juce::NormalisableRange<float> { -60.0f, 0.0f, 0.1f }, -18.0f, juce::AudioParameterFloatAttributes().withLabel("dB")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::dynamics, 1, hypercolor::ParameterIDs::compressorRatio), 1 }, "Compressor Ratio #2", juce::NormalisableRange<float> { 1.0f, 20.0f, 0.1f }, 4.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::dynamics, 1, hypercolor::ParameterIDs::compressorAttack), 1 }, "Compressor Attack #2", juce::NormalisableRange<float> { 0.1f, 100.0f, 0.1f }, 10.0f, juce::AudioParameterFloatAttributes().withLabel("ms")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::dynamics, 1, hypercolor::ParameterIDs::compressorRelease), 1 }, "Compressor Release #2", juce::NormalisableRange<float> { 10.0f, 1000.0f, 0.1f }, 100.0f, juce::AudioParameterFloatAttributes().withLabel("ms")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::dynamics, 1, hypercolor::ParameterIDs::compressorKnee), 1 }, "Compressor Knee #2", juce::NormalisableRange<float> { 0.0f, 24.0f, 0.1f }, 6.0f, juce::AudioParameterFloatAttributes().withLabel("dB")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::dynamics, 1, hypercolor::ParameterIDs::compressorMakeup), 1 }, "Compressor Makeup #2", juce::NormalisableRange<float> { 0.0f, 24.0f, 0.1f }, 0.0f, juce::AudioParameterFloatAttributes().withLabel("dB")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::dynamics, 1, hypercolor::ParameterIDs::compressorMix), 1 }, "Compressor Mix #2", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 100.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::dynamics, 1, hypercolor::ParameterIDs::compressorStereoLink), 1 }, "Compressor Stereo Link #2", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 100.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { hypercolor::plugin::fxEnabledParameterID(hypercolor::plugin::FxType::space, 2, hypercolor::ParameterIDs::reverbEnabled), 1 }, "Reverb Enabled #3", false));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::space, 2, hypercolor::ParameterIDs::reverbMix), 1 }, "Reverb Mix #3", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 30.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::space, 2, hypercolor::ParameterIDs::reverbSize), 1 }, "Reverb Size #3", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 50.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::space, 2, hypercolor::ParameterIDs::reverbDecay), 1 }, "Reverb Decay #3", juce::NormalisableRange<float> { 0.1f, 20.0f, 0.01f }, 4.0f, juce::AudioParameterFloatAttributes().withLabel("s")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::space, 2, hypercolor::ParameterIDs::reverbDamping), 1 }, "Reverb Damping #3", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 40.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::space, 2, hypercolor::ParameterIDs::reverbPreDelay), 1 }, "Reverb Pre-Delay #3", juce::NormalisableRange<float> { 0.0f, 200.0f, 0.1f }, 20.0f, juce::AudioParameterFloatAttributes().withLabel("ms")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::space, 2, hypercolor::ParameterIDs::reverbWidth), 1 }, "Reverb Width #3", juce::NormalisableRange<float> { 0.0f, 200.0f, 0.1f }, 100.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { hypercolor::plugin::fxEnabledParameterID(hypercolor::plugin::FxType::modulation, 2, hypercolor::ParameterIDs::modulationEnabled), 1 }, "Modulation Enabled #3", false));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::modulation, 2, hypercolor::ParameterIDs::modulationMode), 1 }, "Modulation Mode #3", juce::StringArray { "Chorus", "Flanger" }, 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::modulation, 2, hypercolor::ParameterIDs::modulationRate), 1 }, "Modulation Rate #3", juce::NormalisableRange<float> { 0.01f, 10.0f, 0.01f }, 0.3f, juce::AudioParameterFloatAttributes().withLabel("Hz")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::modulation, 2, hypercolor::ParameterIDs::modulationDepth), 1 }, "Modulation Depth #3", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 50.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::modulation, 2, hypercolor::ParameterIDs::modulationBaseDelay), 1 }, "Modulation Base Delay #3", juce::NormalisableRange<float> { 0.1f, 20.0f, 0.01f }, 7.0f, juce::AudioParameterFloatAttributes().withLabel("ms")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::modulation, 2, hypercolor::ParameterIDs::modulationFeedback), 1 }, "Modulation Feedback #3", juce::NormalisableRange<float> { -95.0f, 95.0f, 0.1f }, 0.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::modulation, 2, hypercolor::ParameterIDs::modulationMix), 1 }, "Modulation Mix #3", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 30.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::modulation, 2, hypercolor::ParameterIDs::modulationWidth), 1 }, "Modulation Width #3", juce::NormalisableRange<float> { 0.0f, 200.0f, 0.1f }, 100.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { hypercolor::plugin::fxEnabledParameterID(hypercolor::plugin::FxType::drive, 2, hypercolor::ParameterIDs::driveEnabled), 1 }, "Drive Enabled #3", false));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::drive, 2, hypercolor::ParameterIDs::driveMode), 1 }, "Drive Mode #3", juce::StringArray { "Soft Clip", "Hard Clip", "Tube" }, 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::drive, 2, hypercolor::ParameterIDs::driveAmount), 1 }, "Drive #3", juce::NormalisableRange<float> { 0.0f, 36.0f, 0.1f }, 0.0f, juce::AudioParameterFloatAttributes().withLabel("dB")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::drive, 2, hypercolor::ParameterIDs::driveTone), 1 }, "Drive Tone #3", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 50.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::drive, 2, hypercolor::ParameterIDs::driveMix), 1 }, "Drive Mix #3", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 100.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::drive, 2, hypercolor::ParameterIDs::driveOutput), 1 }, "Drive Output #3", juce::NormalisableRange<float> { -24.0f, 12.0f, 0.1f }, 0.0f, juce::AudioParameterFloatAttributes().withLabel("dB")));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { hypercolor::plugin::fxEnabledParameterID(hypercolor::plugin::FxType::delay, 2, hypercolor::ParameterIDs::delayEnabled), 1 }, "Delay Enabled #3", false));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::delay, 2, hypercolor::ParameterIDs::delayMode), 1 }, "Delay Mode #3", juce::StringArray { "Stereo", "Ping-Pong" }, 0));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::delay, 2, hypercolor::ParameterIDs::delaySync), 1 }, "Delay Sync #3", false));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::delay, 2, hypercolor::ParameterIDs::delayTime), 1 }, "Delay Time #3", juce::NormalisableRange<float> { 1.0f, 2000.0f, 0.1f }, 350.0f, juce::AudioParameterFloatAttributes().withLabel("ms")));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::delay, 2, hypercolor::ParameterIDs::delayDivision), 1 }, "Delay Division #3", juce::StringArray { "1/1", "1/2", "1/4", "1/8", "1/16", "1/4 dotted", "1/8 dotted", "1/4 triplet", "1/8 triplet" }, 2));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::delay, 2, hypercolor::ParameterIDs::delayFeedback), 1 }, "Delay Feedback #3", juce::NormalisableRange<float> { 0.0f, 95.0f, 0.1f }, 35.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::delay, 2, hypercolor::ParameterIDs::delayLowCut), 1 }, "Delay Low Cut #3", juce::NormalisableRange<float> { 20.0f, 2000.0f, 1.0f }, 100.0f, juce::AudioParameterFloatAttributes().withLabel("Hz")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::delay, 2, hypercolor::ParameterIDs::delayHighCut), 1 }, "Delay High Cut #3", juce::NormalisableRange<float> { 1000.0f, 20000.0f, 1.0f }, 12000.0f, juce::AudioParameterFloatAttributes().withLabel("Hz")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::delay, 2, hypercolor::ParameterIDs::delayMix), 1 }, "Delay Mix #3", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 30.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::delay, 2, hypercolor::ParameterIDs::delayWidth), 1 }, "Delay Width #3", juce::NormalisableRange<float> { 0.0f, 200.0f, 0.1f }, 100.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { hypercolor::plugin::fxEnabledParameterID(hypercolor::plugin::FxType::tape, 2, hypercolor::ParameterIDs::tapeEnabled), 1 }, "Tape Enabled #3", false));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::tape, 2, hypercolor::ParameterIDs::tapeDrive), 1 }, "Tape Drive #3", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 35.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::tape, 2, hypercolor::ParameterIDs::tapeWow), 1 }, "Tape Wow #3", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 15.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::tape, 2, hypercolor::ParameterIDs::tapeFlutter), 1 }, "Tape Flutter #3", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 10.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::tape, 2, hypercolor::ParameterIDs::tapeWear), 1 }, "Tape Wear #3", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 15.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::tape, 2, hypercolor::ParameterIDs::tapeMix), 1 }, "Tape Mix #3", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 100.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::tape, 2, hypercolor::ParameterIDs::tapeOutput), 1 }, "Tape Output #3", juce::NormalisableRange<float> { -24.0f, 12.0f, 0.1f }, 0.0f, juce::AudioParameterFloatAttributes().withLabel("dB")));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { hypercolor::plugin::fxEnabledParameterID(hypercolor::plugin::FxType::bitcrush, 2, hypercolor::ParameterIDs::bitcrushEnabled), 1 }, "Bitcrush Enabled #3", false));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::bitcrush, 2, hypercolor::ParameterIDs::bitcrushBits), 1 }, "Bitcrush Bits #3", juce::NormalisableRange<float> { 4.0f, 24.0f, 1.0f }, 16.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::bitcrush, 2, hypercolor::ParameterIDs::bitcrushRate), 1 }, "Bitcrush Rate #3", juce::NormalisableRange<float> { 500.0f, 44100.0f, 1.0f }, 22050.0f, juce::AudioParameterFloatAttributes().withLabel("Hz")));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::bitcrush, 2, hypercolor::ParameterIDs::bitcrushMode), 1 }, "Bitcrush Mode #3", juce::StringArray { "Round", "Truncate" }, 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::bitcrush, 2, hypercolor::ParameterIDs::bitcrushMix), 1 }, "Bitcrush Mix #3", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 100.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::bitcrush, 2, hypercolor::ParameterIDs::bitcrushOutput), 1 }, "Bitcrush Output #3", juce::NormalisableRange<float> { -24.0f, 12.0f, 0.1f }, 0.0f, juce::AudioParameterFloatAttributes().withLabel("dB")));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { hypercolor::plugin::fxEnabledParameterID(hypercolor::plugin::FxType::filter, 2, hypercolor::ParameterIDs::filterEnabled), 1 }, "Filter Enabled #3", false));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::filter, 2, hypercolor::ParameterIDs::filterMode), 1 }, "Filter Mode #3", juce::StringArray { "Low-Pass", "High-Pass", "Band-Pass", "Notch" }, 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::filter, 2, hypercolor::ParameterIDs::filterCutoff), 1 }, "Filter Cutoff #3", juce::NormalisableRange<float> { 20.0f, 20000.0f, 1.0f, 0.35f }, 10000.0f, juce::AudioParameterFloatAttributes().withLabel("Hz")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::filter, 2, hypercolor::ParameterIDs::filterResonance), 1 }, "Filter Resonance #3", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 20.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::filter, 2, hypercolor::ParameterIDs::filterDrive), 1 }, "Filter Drive #3", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 0.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::filter, 2, hypercolor::ParameterIDs::filterMix), 1 }, "Filter Mix #3", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 100.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::filter, 2, hypercolor::ParameterIDs::filterOutput), 1 }, "Filter Output #3", juce::NormalisableRange<float> { -24.0f, 12.0f, 0.1f }, 0.0f, juce::AudioParameterFloatAttributes().withLabel("dB")));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { hypercolor::plugin::fxEnabledParameterID(hypercolor::plugin::FxType::dynamics, 2, hypercolor::ParameterIDs::compressorEnabled), 1 }, "Compressor Enabled #3", false));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::dynamics, 2, hypercolor::ParameterIDs::compressorThreshold), 1 }, "Compressor Threshold #3", juce::NormalisableRange<float> { -60.0f, 0.0f, 0.1f }, -18.0f, juce::AudioParameterFloatAttributes().withLabel("dB")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::dynamics, 2, hypercolor::ParameterIDs::compressorRatio), 1 }, "Compressor Ratio #3", juce::NormalisableRange<float> { 1.0f, 20.0f, 0.1f }, 4.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::dynamics, 2, hypercolor::ParameterIDs::compressorAttack), 1 }, "Compressor Attack #3", juce::NormalisableRange<float> { 0.1f, 100.0f, 0.1f }, 10.0f, juce::AudioParameterFloatAttributes().withLabel("ms")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::dynamics, 2, hypercolor::ParameterIDs::compressorRelease), 1 }, "Compressor Release #3", juce::NormalisableRange<float> { 10.0f, 1000.0f, 0.1f }, 100.0f, juce::AudioParameterFloatAttributes().withLabel("ms")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::dynamics, 2, hypercolor::ParameterIDs::compressorKnee), 1 }, "Compressor Knee #3", juce::NormalisableRange<float> { 0.0f, 24.0f, 0.1f }, 6.0f, juce::AudioParameterFloatAttributes().withLabel("dB")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::dynamics, 2, hypercolor::ParameterIDs::compressorMakeup), 1 }, "Compressor Makeup #3", juce::NormalisableRange<float> { 0.0f, 24.0f, 0.1f }, 0.0f, juce::AudioParameterFloatAttributes().withLabel("dB")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::dynamics, 2, hypercolor::ParameterIDs::compressorMix), 1 }, "Compressor Mix #3", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 100.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::dynamics, 2, hypercolor::ParameterIDs::compressorStereoLink), 1 }, "Compressor Stereo Link #3", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 100.0f, percentAttributes));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { hypercolor::ParameterIDs::outputGain, 1 },
        "Output Gain",
        juce::NormalisableRange<float> {
            outputGainMinimumDb,
            outputGainMaximumDb,
            0.1f
        },
        outputGainDefaultDb,
        decibelAttributes
    ));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { hypercolor::ParameterIDs::grainDensity, 1 },
        "Grain Density",
        juce::NormalisableRange<float> {
            static_cast<float>(
                hypercolor::dsp::granular::GrainScheduler::minimumDensityGrainsPerSecond
            ),
            static_cast<float>(
                hypercolor::dsp::granular::GrainScheduler::maximumDensityGrainsPerSecond
            ),
            0.1f
        },
        static_cast<float>(defaultGranularParameters.densityGrainsPerSecond),
        juce::AudioParameterFloatAttributes().withLabel("grains/s")
    ));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { hypercolor::ParameterIDs::grainDensityMode, 1 },
        "Grain Density Mode",
        juce::StringArray { "Free", "BPM Sync" },
        grainDensityModeDefaultIndex
    ));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { hypercolor::ParameterIDs::grainDensityDivision, 1 },
        "Grain Density Division",
        juce::StringArray {
            "1/4", "1/4T", "1/8", "1/8T", "1/16",
            "1/16T", "1/32", "1/32T", "1/64", "1/64T"
        },
        grainDensityDivisionDefaultIndex
    ));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { hypercolor::ParameterIDs::granularSourceMode, 1 },
        "Granular Source Mode",
        juce::StringArray { "Live", "History" },
        granularSourceModeLiveIndex
    ));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { hypercolor::ParameterIDs::grainSize, 1 },
        "Grain Size",
        juce::NormalisableRange<float> {
            static_cast<float>(
                hypercolor::dsp::granular::GrainScheduler::minimumGrainSizeMilliseconds
            ),
            static_cast<float>(
                hypercolor::dsp::granular::GrainScheduler::maximumGrainSizeMilliseconds
            ),
            0.1f
        },
        static_cast<float>(defaultGranularParameters.grainSizeMilliseconds),
        juce::AudioParameterFloatAttributes().withLabel("ms")
    ));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { hypercolor::ParameterIDs::grainPosition, 1 },
        "Grain Position",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f },
        static_cast<float>(defaultGranularParameters.positionNormalized)
    ));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { hypercolor::ParameterIDs::grainPitch, 1 },
        "Grain Pitch",
        juce::NormalisableRange<float> {
            static_cast<float>(
                hypercolor::dsp::granular::GrainScheduler::minimumPitchSemitones
            ),
            static_cast<float>(
                hypercolor::dsp::granular::GrainScheduler::maximumPitchSemitones
            ),
            0.01f
        },
        static_cast<float>(defaultGranularParameters.pitchSemitones),
        juce::AudioParameterFloatAttributes().withLabel("st")
    ));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { hypercolor::ParameterIDs::grainReverse, 1 },
        "Grain Reverse",
        defaultGranularParameters.reverse
    ));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { hypercolor::ParameterIDs::grainCharacter, 1 },
        "Grain Character",
        juce::StringArray {
            "Controlled Chaos",
            "Musical",
            "Atmospheric"
        },
        granularCharacterDefaultIndex
    ));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { hypercolor::ParameterIDs::grainPitchRandom, 1 },
        "Grain Pitch Random",
        juce::NormalisableRange<float> {
            granularRandomMinimumPercent,
            granularRandomMaximumPercent,
            0.1f
        },
        granularRandomDefaultPercent,
        percentAttributes
    ));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { hypercolor::ParameterIDs::grainPositionRandom, 1 },
        "Grain Position Random",
        juce::NormalisableRange<float> {
            granularRandomMinimumPercent,
            granularRandomMaximumPercent,
            0.1f
        },
        granularRandomDefaultPercent,
        percentAttributes
    ));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { hypercolor::ParameterIDs::grainTimingJitter, 1 },
        "Grain Timing Jitter",
        juce::NormalisableRange<float> {
            granularRandomMinimumPercent,
            granularRandomMaximumPercent,
            0.1f
        },
        granularRandomDefaultPercent,
        percentAttributes
    ));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { hypercolor::ParameterIDs::grainStereoSpread, 1 },
        "Grain Stereo Spread",
        juce::NormalisableRange<float> {
            granularRandomMinimumPercent,
            granularRandomMaximumPercent,
            0.1f
        },
        granularRandomDefaultPercent,
        percentAttributes
    ));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { hypercolor::ParameterIDs::grainPanRandom, 1 },
        "Grain Pan Random",
        juce::NormalisableRange<float> {
            granularRandomMinimumPercent,
            granularRandomMaximumPercent,
            0.1f
        },
        granularRandomDefaultPercent,
        percentAttributes
    ));

    return layout;
}

void HypercolorAudioProcessor::prepareToPlay(
    double sampleRate,
    int samplesPerBlock
)
{
    const auto safeSampleRate = juce::jmax(1.0, sampleRate);
    const auto scratchBlockSize = juce::jmax(1, samplesPerBlock);
    const auto inputChannels = getTotalNumInputChannels();

    dryBuffer.setSize(2, scratchBlockSize, false, false, true);
    for (std::size_t i = 0; i < lfoOscillators.size(); ++i)
        (void) lfoOscillators[i].prepare(safeSampleRate);
    // EffectChain samples module bypass state while it is prepared. Sync the
    // APVTS targets first so a state loaded before prepareToPlay starts in the
    // correct enabled/bypassed state.
    updateEffectBypassTargets();

    const hypercolor::core::ProcessSpec processSpec {
        sampleRate,
        samplesPerBlock > 0
            ? static_cast<std::uint32_t>(samplesPerBlock)
            : 0u,
        inputChannels > 0
            ? static_cast<std::uint32_t>(inputChannels)
            : 0u
    };
    granularEnginePrepared = granularEngine.prepare(processSpec);
    const auto effectChainPrepared = effectChain.prepare(processSpec);
    granularEnginePrepared = granularEnginePrepared && effectChainPrepared;
    telemetrySamplesSincePublication = 0;
    telemetryPublishIntervalSamples = granularEnginePrepared
        ? static_cast<std::uint64_t>(std::max(
            1.0,
            std::round(sampleRate / telemetryUpdatesPerSecond)
        ))
        : 0;

    dryWetSmoother.reset(safeSampleRate, parameterSmoothingSeconds);
    outputGainSmoother.reset(safeSampleRate, parameterSmoothingSeconds);

    const auto dryWet = dryWetParameter->load(std::memory_order_relaxed);
    const auto outputGainDb = outputGainParameter->load(
        std::memory_order_relaxed
    );

    dryWetSmoother.setCurrentAndTargetValue(dryWet / 100.0f);
    outputGainSmoother.setCurrentAndTargetValue(
        juce::Decibels::decibelsToGain(outputGainDb)
    );
}

void HypercolorAudioProcessor::releaseResources()
{
    granularEngine.reset();
    effectChain.reset();
    granularEnginePrepared = false;
    telemetrySamplesSincePublication = 0;
    telemetryPublishIntervalSamples = 0;
}

bool HypercolorAudioProcessor::isBusesLayoutSupported(
    const BusesLayout& layouts
) const
{
    const auto input = layouts.getMainInputChannelSet();
    const auto output = layouts.getMainOutputChannelSet();

    if (output != juce::AudioChannelSet::mono()
        && output != juce::AudioChannelSet::stereo())
    {
        return false;
    }

    return input == output;
}

void HypercolorAudioProcessor::processBlock(
    juce::AudioBuffer<float>& buffer,
    juce::MidiBuffer& midiMessages
)
{
    juce::ScopedNoDenormals noDenormals;

    juce::ignoreUnused(midiMessages);

    const auto totalInputChannels = getTotalNumInputChannels();
    const auto totalOutputChannels = getTotalNumOutputChannels();

    for (int channel = totalInputChannels;
         channel < totalOutputChannels;
         ++channel)
    {
        buffer.clear(channel, 0, buffer.getNumSamples());
    }

    updateParameterTargets();
    updateEffectBypassTargets();
    applyFxChainConfiguration();
    applyFxMacroAssignmentSnapshot();
    applyLfoConfigurationSnapshot();
    const auto bpmForBlock = readHostBpm();
    currentBlockBpm = bpmForBlock;
    currentBlockPpqValid = false;
    if (const auto* playHead = getPlayHead())
    {
        if (const auto position = playHead->getPosition())
        {
            if (const auto ppq = position->getPpqPosition(); ppq
                && std::isfinite(*ppq))
            {
                currentBlockPpq = *ppq;
                currentBlockPpqValid = true;
            }
        }
    }
    const auto densityBpm = static_cast<double>(lastValidHostBpm.load(
        std::memory_order_acquire
    ));
    const auto granularParameters = readGranularParameters(densityBpm);
    for (auto& type : audioFxLfoOverrides)
        for (auto& slot : type)
            for (auto& parameter : slot)
                parameter = {};
    updateFxModuleParameters(bpmForBlock);

    const auto numChannels = juce::jmin(
        totalInputChannels,
        buffer.getNumChannels()
    );

    const auto scratchBlockSize = dryBuffer.getNumSamples();

    if (numChannels <= 0 || scratchBlockSize <= 0)
        return;

    jassert(numChannels <= dryBuffer.getNumChannels());

    for (int startSample = 0;
         startSample < buffer.getNumSamples();
         )
    {
        auto chunkSize = juce::jmin(
            scratchBlockSize,
            buffer.getNumSamples() - startSample
        );

        if (telemetryPublishIntervalSamples > 0)
        {
            const auto samplesUntilPublication
                = telemetryPublishIntervalSamples
                    - telemetrySamplesSincePublication;
            chunkSize = juce::jmin(
                chunkSize,
                static_cast<int>(samplesUntilPublication)
            );
        }

        processChunk(
            buffer,
            startSample,
            chunkSize,
            numChannels,
            granularParameters
        );
        publishGranularTelemetry(chunkSize);
        startSample += chunkSize;
    }
}

bool HypercolorAudioProcessor::tryPopGranularTelemetry(
    hypercolor::dsp::granular::GranularTelemetrySnapshot& destination
) noexcept
{
    return granularTelemetryQueue.tryPop(destination);
}

void HypercolorAudioProcessor::updateFxModuleParameters(double bpm) noexcept
{
    spaceReverb.setParameters(readReverbParameters());
    modulationEffect.setParameters({
        readFxParameterValue(1, 0, 0) >= 0.5f
            ? hypercolor::dsp::fx::ModulationMode::flanger
            : hypercolor::dsp::fx::ModulationMode::chorus,
        readFxParameterValue(1, 0, 1, 0.3f),
        readFxParameterValue(1, 0, 2, 50.0f) / 100.0f,
        readFxParameterValue(1, 0, 3, 7.0f),
        readFxParameterValue(1, 0, 4) / 100.0f,
        readFxParameterValue(1, 0, 5, 30.0f) / 100.0f,
        readFxParameterValue(1, 0, 6, 100.0f) / 100.0f
    });
    driveEffect.setParameters({
        readFxParameterValue(2, 0, 0) >= 1.5f
            ? hypercolor::dsp::fx::DriveMode::tube
            : (readFxParameterValue(2, 0, 0) >= 0.5f
                ? hypercolor::dsp::fx::DriveMode::hardClip
                : hypercolor::dsp::fx::DriveMode::softClip),
        readFxParameterValue(2, 0, 1),
        readFxParameterValue(2, 0, 2, 50.0f) / 100.0f,
        readFxParameterValue(2, 0, 3, 100.0f) / 100.0f,
        readFxParameterValue(2, 0, 4)
    });
    delayEffect.setParameters({
        readFxParameterValue(7, 0, 0) >= 0.5f ? hypercolor::dsp::fx::DelayMode::pingPong : hypercolor::dsp::fx::DelayMode::stereo,
        readFxParameterValue(7, 0, 1) >= 0.5f,
        readFxParameterValue(7, 0, 2, 350.0f),
        static_cast<hypercolor::dsp::fx::DelayDivision>(std::clamp(readFxParameterValue(7, 0, 3, 2.0f), 0.0f, 8.0f)),
        readFxParameterValue(7, 0, 4, 35.0f) / 100.0f,
        readFxParameterValue(7, 0, 5, 100.0f),
        readFxParameterValue(7, 0, 6, 12000.0f),
        readFxParameterValue(7, 0, 7, 30.0f) / 100.0f,
        readFxParameterValue(7, 0, 8, 100.0f) / 100.0f,
        static_cast<float>(bpm)
    });
    tapeEffect.setParameters({
        readFxParameterValue(3, 0, 0, 35.0f) / 100.0f,
        readFxParameterValue(3, 0, 1, 15.0f) / 100.0f,
        readFxParameterValue(3, 0, 2, 10.0f) / 100.0f,
        readFxParameterValue(3, 0, 3, 15.0f) / 100.0f,
        readFxParameterValue(3, 0, 4, 100.0f) / 100.0f,
        readFxParameterValue(3, 0, 5)
    });
    bitcrushEffect.setParameters({
        readFxParameterValue(4, 0, 0, 16.0f),
        readFxParameterValue(4, 0, 1, 22050.0f),
        readFxParameterValue(4, 0, 2) >= 0.5f ? hypercolor::dsp::fx::BitcrushMode::truncate : hypercolor::dsp::fx::BitcrushMode::round,
        readFxParameterValue(4, 0, 3, 100.0f) / 100.0f,
        readFxParameterValue(4, 0, 4)
    });
    filterEffect.setParameters({
        static_cast<hypercolor::dsp::fx::FilterMode>(std::clamp(readFxParameterValue(5, 0, 0), 0.0f, 3.0f)),
        readFxParameterValue(5, 0, 1, 10000.0f),
        readFxParameterValue(5, 0, 2, 20.0f) / 100.0f,
        readFxParameterValue(5, 0, 3) / 100.0f,
        readFxParameterValue(5, 0, 4, 100.0f) / 100.0f,
        readFxParameterValue(5, 0, 5)
    });
    compressorEffect.setParameters({
        readFxParameterValue(6, 0, 0, -18.0f),
        readFxParameterValue(6, 0, 1, 4.0f),
        readFxParameterValue(6, 0, 2, 10.0f),
        readFxParameterValue(6, 0, 3, 100.0f),
        readFxParameterValue(6, 0, 4, 6.0f),
        readFxParameterValue(6, 0, 5),
        readFxParameterValue(6, 0, 6, 100.0f) / 100.0f,
        readFxParameterValue(6, 0, 7, 100.0f) / 100.0f
    });

    for (std::size_t type = 0; type < hypercolor::plugin::fxTypeCount; ++type)
    {
        for (std::size_t slot = 1; slot < hypercolor::plugin::maximumFxInstancesPerType; ++slot)
        {
            const auto& raw = fxRawParameters[type][slot].values;
            const auto value = [this, type, slot, &raw](std::size_t i, float fallback = 0.0f)
            {
                return raw[i] != nullptr ? readFxParameterValue(type, slot, i, fallback) : fallback;
            };
            switch (type)
            {
                case 0:
                    extraSpaceReverbs[slot - 1].setParameters({ value(0, 30.0f) / 100.0f, value(1, 50.0f) / 100.0f, value(2, 4.0f), value(3, 40.0f) / 100.0f, value(4, 20.0f), value(5, 100.0f) / 100.0f });
                    break;
                case 1:
                    extraModulationEffects[slot - 1].setParameters({ value(0) >= 0.5f ? hypercolor::dsp::fx::ModulationMode::flanger : hypercolor::dsp::fx::ModulationMode::chorus, value(1, 0.3f), value(2, 50.0f) / 100.0f, value(3, 7.0f), value(4) / 100.0f, value(5, 30.0f) / 100.0f, value(6, 100.0f) / 100.0f });
                    break;
                case 2:
                    extraDriveEffects[slot - 1].setParameters({ value(0) >= 1.5f ? hypercolor::dsp::fx::DriveMode::tube : (value(0) >= 0.5f ? hypercolor::dsp::fx::DriveMode::hardClip : hypercolor::dsp::fx::DriveMode::softClip), value(1), value(2, 50.0f) / 100.0f, value(3, 100.0f) / 100.0f, value(4) });
                    break;
                case 3:
                    extraTapeEffects[slot - 1].setParameters({ value(0, 35.0f) / 100.0f, value(1, 15.0f) / 100.0f, value(2, 10.0f) / 100.0f, value(3, 15.0f) / 100.0f, value(4, 100.0f) / 100.0f, value(5) });
                    break;
                case 4:
                    extraBitcrushEffects[slot - 1].setParameters({ value(0, 16.0f), value(1, 22050.0f), value(2) >= 0.5f ? hypercolor::dsp::fx::BitcrushMode::truncate : hypercolor::dsp::fx::BitcrushMode::round, value(3, 100.0f) / 100.0f, value(4) });
                    break;
                case 5:
                    extraFilterEffects[slot - 1].setParameters({ static_cast<hypercolor::dsp::fx::FilterMode>(std::clamp(value(0), 0.0f, 3.0f)), value(1, 10000.0f), value(2, 20.0f) / 100.0f, value(3) / 100.0f, value(4, 100.0f) / 100.0f, value(5) });
                    break;
                case 6:
                    extraCompressorEffects[slot - 1].setParameters({ value(0, -18.0f), value(1, 4.0f), value(2, 10.0f), value(3, 100.0f), value(4, 6.0f), value(5), value(6, 100.0f) / 100.0f, value(7, 100.0f) / 100.0f });
                    break;
                case 7:
                    extraDelayEffects[slot - 1].setParameters({ value(0) >= 0.5f ? hypercolor::dsp::fx::DelayMode::pingPong : hypercolor::dsp::fx::DelayMode::stereo, value(1) >= 0.5f, value(2, 350.0f), static_cast<hypercolor::dsp::fx::DelayDivision>(std::clamp(value(3, 2.0f), 0.0f, 8.0f)), value(4, 35.0f) / 100.0f, value(5, 100.0f), value(6, 12000.0f), value(7, 30.0f) / 100.0f, value(8, 100.0f) / 100.0f, static_cast<float>(bpm) });
                    break;
                default: break;
            }
        }
    }

}

void HypercolorAudioProcessor::updateParameterTargets() noexcept
{
    const auto dryWet = dryWetParameter->load(std::memory_order_relaxed);
    const auto outputGainDb = outputGainParameter->load(
        std::memory_order_relaxed
    );

    dryWetSmoother.setTargetValue(
        juce::jlimit(dryWetMinimum, dryWetMaximum, dryWet) / 100.0f
    );
    outputGainSmoother.setTargetValue(
        juce::Decibels::decibelsToGain(
            juce::jlimit(
                outputGainMinimumDb,
                outputGainMaximumDb,
                outputGainDb
            )
        )
    );
}

void HypercolorAudioProcessor::updateEffectBypassTargets() noexcept
{
    // APVTS raw values are atomic and are therefore safe to sample on the
    // audio thread. EffectChain owns the sample-level 15 ms ramp; these calls
    // only publish the current target for each module.
    for (std::size_t type = 0; type < hypercolor::plugin::fxTypeCount; ++type)
        for (std::size_t slot = 0; slot < hypercolor::plugin::maximumFxInstancesPerType; ++slot)
            fxModuleInstances[type][slot]->setBypassed(
                fxRawParameters[type][slot].enabled->load(std::memory_order_relaxed) < 0.5f
            );
}

double HypercolorAudioProcessor::readHostBpm() noexcept
{
    // Preserve the existing FX behavior: effects use the current block BPM,
    // falling back to 120 when this block has no valid transport value.
    auto bpmForBlock = 120.0;

    if (const auto* playHead = getPlayHead())
    {
        if (const auto position = playHead->getPosition())
        {
            if (const auto hostBpm = position->getBpm())
            {
                const auto candidate = *hostBpm;
                if (std::isfinite(candidate)
                    && candidate >= hypercolor::density::minimumBpm
                    && candidate <= hypercolor::density::maximumBpm)
                {
                    bpmForBlock = candidate;
                    lastValidHostBpm.store(
                        static_cast<float>(candidate),
                        std::memory_order_release
                    );
                }
            }
        }
    }

    return bpmForBlock;
}

hypercolor::dsp::granular::GranularEngineParameters
HypercolorAudioProcessor::readGranularParameters(double hostBpm) const noexcept
{
    const auto rawMode = grainDensityModeParameter->load(
        std::memory_order_relaxed
    );
    const auto mode = std::isfinite(rawMode)
        ? std::clamp(rawMode, 0.0f, 1.0f)
        : 0.0f;
    const auto freeDensity = static_cast<double>(
        grainDensityParameter->load(std::memory_order_relaxed)
    );
    auto effectiveDensity = freeDensity;

    if (mode >= 0.5f)
    {
        const auto rawDivision = grainDensityDivisionParameter->load(
            std::memory_order_relaxed
        );
        const auto safeDivision = std::isfinite(rawDivision)
            ? rawDivision
            : static_cast<float>(grainDensityDivisionDefaultIndex);
        const auto divisionIndex = static_cast<int>(std::lround(
            std::clamp(
                safeDivision,
                0.0f,
                static_cast<float>(grainDensityDivisionMaximumIndex)
            )
        ));
        effectiveDensity = hypercolor::density::bpmToGrainsPerSecond(
            hostBpm,
            static_cast<hypercolor::density::Division>(divisionIndex)
        );
    }

    if (!std::isfinite(effectiveDensity))
        effectiveDensity = hypercolor::density::minimumGrainsPerSecond;

    effectiveDensity = std::clamp(
        effectiveDensity,
        hypercolor::density::minimumGrainsPerSecond,
        hypercolor::density::maximumGrainsPerSecond
    );

    auto result = hypercolor::dsp::granular::GranularEngineParameters {
        effectiveDensity,
        static_cast<double>(
            grainSizeParameter->load(std::memory_order_relaxed)
        ),
        static_cast<double>(
            grainPositionParameter->load(std::memory_order_relaxed)
        ),
        static_cast<double>(
            grainPitchParameter->load(std::memory_order_relaxed)
        ),
        grainReverseParameter->load(std::memory_order_relaxed) >= 0.5f,
        readGranularCharacter(
            grainCharacterParameter->load(std::memory_order_relaxed)
        ),
        static_cast<double>(
            grainPitchRandomParameter->load(std::memory_order_relaxed)
        ) / 100.0,
        static_cast<double>(
            grainPositionRandomParameter->load(std::memory_order_relaxed)
        ) / 100.0,
        static_cast<double>(
            grainTimingJitterParameter->load(std::memory_order_relaxed)
        ) / 100.0,
        static_cast<double>(
            grainStereoSpreadParameter->load(std::memory_order_relaxed)
        ) / 100.0,
        static_cast<double>(
            grainPanRandomParameter->load(std::memory_order_relaxed)
        ) / 100.0,
        readGrainEnvelope(
            grainEnvelopeParameter->load(std::memory_order_relaxed)
        )
    };
    const auto rawSourceMode = granularSourceModeParameter->load(
        std::memory_order_relaxed
    );
    const auto sourceMode = std::isfinite(rawSourceMode)
        ? std::clamp(rawSourceMode, 0.0f,
                     static_cast<float>(granularSourceModeMaximumIndex))
        : static_cast<float>(granularSourceModeLiveIndex);
    result.sourcePolicy = sourceMode >= 0.5f
        ? hypercolor::dsp::granular::CausalSourcePolicy::historyOnly
        : hypercolor::dsp::granular::CausalSourcePolicy::causalLiveOnly;
    return result;
}

hypercolor::dsp::fx::SpaceReverbParameters
HypercolorAudioProcessor::readReverbParameters() const noexcept
{
    return {
        readFxParameterValue(0, 0, 0, 30.0f) / 100.0f,
        readFxParameterValue(0, 0, 1, 50.0f) / 100.0f,
        readFxParameterValue(0, 0, 2, 4.0f),
        readFxParameterValue(0, 0, 3, 40.0f) / 100.0f,
        readFxParameterValue(0, 0, 4, 20.0f),
        readFxParameterValue(0, 0, 5, 100.0f) / 100.0f
    };
}

void HypercolorAudioProcessor::processChunk(
    juce::AudioBuffer<float>& buffer,
    int startSample,
    int numSamples,
    int numChannels,
    const hypercolor::dsp::granular::GranularEngineParameters&
        granularParameters
) noexcept
{
    for (int channel = 0; channel < numChannels; ++channel)
    {
        dryBuffer.copyFrom(
            channel,
            0,
            buffer,
            channel,
            startSample,
            numSamples
        );
    }

    hypercolor::dsp::granular::GranularAudioBlockView granularBlock;
    granularBlock.numChannels = static_cast<std::uint32_t>(numChannels);

    for (int channel = 0; channel < numChannels; ++channel)
    {
        granularBlock.inputChannels[static_cast<std::size_t>(channel)] = {
            dryBuffer.getReadPointer(channel),
            static_cast<std::size_t>(numSamples)
        };
        granularBlock.outputChannels[static_cast<std::size_t>(channel)] = {
            buffer.getWritePointer(channel, startSample),
            static_cast<std::size_t>(numSamples)
        };
    }

    const auto granularResult = granularEnginePrepared
        ? granularEngine.processBlock(
            granularBlock,
            granularParameters
        )
        : hypercolor::dsp::granular::GranularEngineProcessResult::engineNotPrepared;
    hypercolor::dsp::fx::EffectAudioBlock effectBlock;
    effectBlock.numChannels = static_cast<std::uint32_t>(numChannels);
    for (int channel = 0; channel < numChannels; ++channel)
    {
        effectBlock.channels[static_cast<std::size_t>(channel)] = {
            buffer.getWritePointer(channel, startSample),
            static_cast<std::size_t>(numSamples)
        };
    }
    auto effectResult = hypercolor::dsp::fx::EffectChainResult::success;
    bool hasLfo = false;
    if (lfoModuleCount.load(std::memory_order_relaxed) > 0
        && lfoAssignmentCount.load(std::memory_order_relaxed) > 0)
    {
        for (const auto& assignment : audioLfoAssignments)
        {
            if (!assignment.active || assignment.type >= hypercolor::plugin::fxTypeCount
                || assignment.slot >= hypercolor::plugin::maximumFxInstancesPerType
                || audioFxInstanceIdentities[assignment.type][assignment.slot] != assignment.fxIdentity)
                continue;
            for (const auto& module : audioLfoModules)
                if (module.identity == assignment.lfoIdentity && module.enabled)
                { hasLfo = true; break; }
            if (hasLfo) break;
        }
    }
    constexpr int modulationQuantum = 16;
    if (hasLfo)
    {
        for (int offset = 0; offset < numSamples; offset += modulationQuantum)
        {
            const auto segmentLength = juce::jmin(modulationQuantum, numSamples - offset);
            updateLfoOverrides(currentBlockBpm,
                currentBlockPpqValid
                    ? std::optional<double>(currentBlockPpq
                        + static_cast<double>(startSample + offset) * currentBlockBpm / 60.0
                            / juce::jmax(1.0, getSampleRate()))
                    : std::nullopt,
                segmentLength);
            updateFxModuleParameters(currentBlockBpm);
            hypercolor::dsp::fx::EffectAudioBlock segment;
            segment.numChannels = static_cast<std::uint32_t>(numChannels);
            for (int channel = 0; channel < numChannels; ++channel)
                segment.channels[static_cast<std::size_t>(channel)] = {
                    buffer.getWritePointer(channel, startSample + offset),
                    static_cast<std::size_t>(segmentLength)
                };
            if (effectChain.process(segment)
                == hypercolor::dsp::fx::EffectChainResult::invalidConfiguration)
                effectResult = hypercolor::dsp::fx::EffectChainResult::invalidConfiguration;
        }
    }
    else
    {
        effectResult = effectChain.process(effectBlock);
    }
    const auto useDryFallback = granularResult
            == hypercolor::dsp::granular::GranularEngineProcessResult::engineNotPrepared
        || granularResult
            == hypercolor::dsp::granular::GranularEngineProcessResult::invalidConfiguration;
    const auto useEffectFallback = effectResult
        == hypercolor::dsp::fx::EffectChainResult::invalidConfiguration;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        const auto wet = dryWetSmoother.getNextValue();
        const auto outputGain = outputGainSmoother.getNextValue();

        for (int channel = 0; channel < numChannels; ++channel)
        {
            const auto rawDrySample = dryBuffer.getSample(channel, sample);
            const auto drySample = std::isfinite(rawDrySample)
                ? rawDrySample
                : 0.0f;
            const auto rawWetSample = useDryFallback || useEffectFallback
                ? drySample
                : buffer.getSample(channel, startSample + sample);
            const auto wetSample = std::isfinite(rawWetSample)
                ? rawWetSample
                : 0.0f;
            const auto mixedSample = drySample
                + wet * (wetSample - drySample);
            const auto outputSample = mixedSample * outputGain;

            buffer.setSample(
                channel,
                startSample + sample,
                std::isfinite(outputSample) ? outputSample : 0.0f
            );
        }
    }
}

void HypercolorAudioProcessor::publishGranularTelemetry(
    int processedSamples
) noexcept
{
    if (!granularEnginePrepared
        || telemetryPublishIntervalSamples == 0
        || processedSamples <= 0)
    {
        return;
    }

    const auto samples = static_cast<std::uint64_t>(processedSamples);

    // processedSamples is a non-negative int and the retained accumulator is
    // always smaller than one interval, so this addition cannot approach the
    // uint64 limit on any supported host block.
    telemetrySamplesSincePublication += samples;

    if (telemetrySamplesSincePublication
        < telemetryPublishIntervalSamples)
    {
        return;
    }

    telemetrySamplesSincePublication %= telemetryPublishIntervalSamples;

    hypercolor::dsp::granular::GranularTelemetrySnapshot snapshot;

    if (!granularEngine.copyTelemetrySnapshot(snapshot))
        return;

    snapshot.sequence = nextTelemetrySequence++;
    (void) granularTelemetryQueue.tryPush(snapshot);
}

juce::AudioProcessorEditor*
HypercolorAudioProcessor::createEditor()
{
    return new HypercolorAudioProcessorEditor(*this);
}

bool HypercolorAudioProcessor::hasEditor() const
{
    return true;
}

const juce::String HypercolorAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool HypercolorAudioProcessor::acceptsMidi() const
{
    return false;
}

bool HypercolorAudioProcessor::producesMidi() const
{
    return false;
}

bool HypercolorAudioProcessor::isMidiEffect() const
{
    return false;
}

double HypercolorAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int HypercolorAudioProcessor::getNumPrograms()
{
    return 1;
}

int HypercolorAudioProcessor::getCurrentProgram()
{
    return 0;
}

void HypercolorAudioProcessor::setCurrentProgram(int index)
{
    juce::ignoreUnused(index);
}

const juce::String HypercolorAudioProcessor::getProgramName(int index)
{
    juce::ignoreUnused(index);
    return {};
}

void HypercolorAudioProcessor::changeProgramName(
    int index,
    const juce::String& newName
)
{
    juce::ignoreUnused(index, newName);
}

void HypercolorAudioProcessor::getStateInformation(
    juce::MemoryBlock& destData
)
{
    destData.reset();

    auto state = parameters.copyState();
    state.setProperty(
        stateSchemaProperty,
        currentStateSchemaVersion,
        nullptr
    );
    auto fxChainState = juce::ValueTree(fxChainStateType);
    const auto chainConfiguration = getFxChainConfiguration();
    for (std::size_t i = 0; i < chainConfiguration.count; ++i)
    {
        juce::ValueTree entry("MODULE");
        entry.setProperty("type", hypercolor::plugin::toIndex(chainConfiguration.order[i].type), nullptr);
        entry.setProperty("slot", chainConfiguration.order[i].slot, nullptr);
        entry.setProperty("identity", static_cast<juce::int64>(chainConfiguration.order[i].stableIdentity), nullptr);
        fxChainState.addChild(entry, -1, nullptr);
    }
    state.addChild(fxChainState, -1, nullptr);
    state.setProperty(nextFxIdentityProperty,
        static_cast<juce::int64>(nextFxInstanceIdentity.load(std::memory_order_relaxed)), nullptr);

    const auto assignments = getFxMacroAssignments();
    juce::ValueTree assignmentState(fxMacroAssignmentsStateType);
    for (std::size_t i = 0; i < assignments.count; ++i)
    {
        const auto& assignment = assignments.assignments[i];
        juce::ValueTree entry("ASSIGNMENT");
        entry.setProperty("axis", static_cast<int>(assignment.axis), nullptr);
        entry.setProperty("identity", static_cast<juce::int64>(assignment.instanceIdentity), nullptr);
        entry.setProperty("parameter", assignment.parameterID, nullptr);
        entry.setProperty("minimum", assignment.minimum, nullptr);
        entry.setProperty("maximum", assignment.maximum, nullptr);
        assignmentState.addChild(entry, -1, nullptr);
    }
    state.addChild(assignmentState, -1, nullptr);

    const auto lfoConfiguration = getLfoConfiguration();
    juce::ValueTree lfoState(lfoConfigurationStateType);
    for (std::size_t i = 0; i < lfoConfiguration.moduleCount; ++i)
    {
        const auto& module = lfoConfiguration.modules[i];
        juce::ValueTree entry("LFO");
        entry.setProperty("identity", static_cast<juce::int64>(module.stableIdentity), nullptr);
        entry.setProperty("enabled", module.enabled, nullptr);
        entry.setProperty("waveform", static_cast<int>(module.waveform), nullptr);
        entry.setProperty("rateMode", static_cast<int>(module.rateMode), nullptr);
        entry.setProperty("rateHz", module.rateHz, nullptr);
        entry.setProperty("division", static_cast<int>(module.division), nullptr);
        entry.setProperty("depth", module.depth, nullptr);
        entry.setProperty("phase", module.phase, nullptr);
        entry.setProperty("offset", module.offset, nullptr);
        lfoState.addChild(entry, -1, nullptr);
    }
    for (std::size_t i = 0; i < lfoConfiguration.assignmentCount; ++i)
    {
        const auto& assignment = lfoConfiguration.assignments[i];
        juce::ValueTree entry("ASSIGNMENT");
        entry.setProperty("lfoIdentity", static_cast<juce::int64>(assignment.lfoIdentity), nullptr);
        entry.setProperty("fxIdentity", static_cast<juce::int64>(assignment.fxInstanceIdentity), nullptr);
        entry.setProperty("parameter", assignment.parameterID, nullptr);
        entry.setProperty("amount", assignment.amount, nullptr);
        entry.setProperty("bipolar", assignment.bipolar, nullptr);
        entry.setProperty("inverted", assignment.inverted, nullptr);
        lfoState.addChild(entry, -1, nullptr);
    }
    state.addChild(lfoState, -1, nullptr);
    state.setProperty(nextLfoIdentityProperty,
        static_cast<juce::int64>(nextLfoIdentity.load(std::memory_order_relaxed)), nullptr);

    if (const auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void HypercolorAudioProcessor::setStateInformation(
    const void* data,
    int sizeInBytes
)
{
    if (data == nullptr
        || sizeInBytes <= 0
        || sizeInBytes > maxStateSizeBytes)
    {
        return;
    }

    const auto xml = getXmlFromBinary(data, sizeInBytes);

    if (xml == nullptr || !xml->hasTagName(stateTreeType))
        return;

    auto restoredState = juce::ValueTree::fromXml(*xml);

    if (!restoredState.isValid()
        || restoredState.getType() != parameters.state.getType())
    {
        return;
    }

    auto schemaVersion = 0;

    if (restoredState.hasProperty(stateSchemaProperty))
    {
        const auto version = restoredState.getProperty(
            stateSchemaProperty
        );

        double numericVersion = 0.0;

        if (!readFiniteNumber(version, numericVersion)
            || numericVersion != std::floor(numericVersion)
            || numericVersion < 0.0
            || numericVersion > currentStateSchemaVersion)
        {
            return;
        }

        schemaVersion = static_cast<int>(numericVersion);
    }

    hypercolor::plugin::FxChainConfiguration restoredFxChain;
    constexpr std::array<hypercolor::plugin::FxType, 8> legacyFxOrder {
        hypercolor::plugin::FxType::space,
        hypercolor::plugin::FxType::modulation,
        hypercolor::plugin::FxType::drive,
        hypercolor::plugin::FxType::tape,
        hypercolor::plugin::FxType::bitcrush,
        hypercolor::plugin::FxType::filter,
        hypercolor::plugin::FxType::dynamics,
        hypercolor::plugin::FxType::delay
    };
    restoredFxChain.count = 8;
    for (std::size_t i = 0; i < legacyFxOrder.size(); ++i)
        restoredFxChain.order[i] = { legacyFxOrder[i], 0,
            hypercolor::plugin::legacyFxIdentity(legacyFxOrder[i], 0) };
    const auto chainNode = restoredState.getChildWithName(fxChainStateType);
    if (!chainNode.isValid() && schemaVersion >= currentStateSchemaVersion)
        return;
    if (chainNode.isValid())
    {
        if (chainNode.getNumChildren() > static_cast<int>(hypercolor::plugin::maximumFxChainModules))
            return;
        restoredFxChain = {};
        restoredFxChain.count = static_cast<std::uint8_t>(chainNode.getNumChildren());
        for (int i = 0; i < chainNode.getNumChildren(); ++i)
        {
            const auto entry = chainNode.getChild(i);
            const auto type = static_cast<int>(entry.getProperty("type", -1));
            const auto slot = static_cast<int>(entry.getProperty("slot", -1));
            if (type < 0 || type >= static_cast<int>(hypercolor::plugin::fxTypeCount)
                || slot < 0 || slot >= static_cast<int>(hypercolor::plugin::maximumFxInstancesPerType))
                return;
            std::uint64_t identity = hypercolor::plugin::legacyFxIdentity(
                static_cast<hypercolor::plugin::FxType>(type),
                static_cast<std::uint8_t>(slot));
            if (schemaVersion >= 6)
            {
                double identityNumber = 0.0;
                if (!entry.hasProperty("identity")
                    || !readFiniteNumber(entry.getProperty("identity"), identityNumber)
                    || identityNumber < 1.0
                    || identityNumber > 9007199254740991.0
                    || identityNumber != std::floor(identityNumber))
                    return;
                identity = static_cast<std::uint64_t>(identityNumber);
            }
            restoredFxChain.order[static_cast<std::size_t>(i)] = {
                static_cast<hypercolor::plugin::FxType>(type),
                static_cast<std::uint8_t>(slot), identity
            };
        }
        std::array<bool, hypercolor::plugin::fxTypeCount * hypercolor::plugin::maximumFxInstancesPerType> seen {};
        for (std::size_t i = 0; i < restoredFxChain.count; ++i)
        {
            const auto id = restoredFxChain.order[i];
            const auto flat = static_cast<std::size_t>(hypercolor::plugin::toIndex(id.type))
                * hypercolor::plugin::maximumFxInstancesPerType + id.slot;
            if (seen[flat])
                return;
            seen[flat] = true;
            for (std::size_t previous = 0; previous < i; ++previous)
                if (restoredFxChain.order[previous].stableIdentity == id.stableIdentity)
                    return;
        }
    }

    auto restoredAssignments = hypercolor::plugin::FxMacroAssignmentSet {};
    const auto assignmentNode = restoredState.getChildWithName(fxMacroAssignmentsStateType);
    if (assignmentNode.isValid())
    {
        if (assignmentNode.getNumChildren() > static_cast<int>(hypercolor::plugin::maximumFxMacroAssignments))
            return;
        restoredAssignments.count = static_cast<std::uint8_t>(assignmentNode.getNumChildren());
        for (int i = 0; i < assignmentNode.getNumChildren(); ++i)
        {
            const auto entry = assignmentNode.getChild(i);
            double identity = 0.0, minimum = 0.0, maximum = 0.0;
            const auto axis = static_cast<int>(entry.getProperty("axis", -1));
            if (axis < 0 || axis > 1
                || !readFiniteNumber(entry.getProperty("identity"), identity)
                || identity < 1.0 || identity > 9007199254740991.0
                || identity != std::floor(identity)
                || !readFiniteNumber(entry.getProperty("minimum"), minimum)
                || !readFiniteNumber(entry.getProperty("maximum"), maximum))
                return;
            auto& assignment = restoredAssignments.assignments[static_cast<std::size_t>(i)];
            assignment.axis = static_cast<hypercolor::plugin::MacroAxis>(axis);
            assignment.instanceIdentity = static_cast<std::uint64_t>(identity);
            assignment.parameterID = entry.getProperty("parameter").toString();
            assignment.minimum = static_cast<float>(minimum);
            assignment.maximum = static_cast<float>(maximum);
            if (assignment.parameterID.isEmpty())
                return;
        }
    }
    else if (schemaVersion >= 6)
        return;

    hypercolor::plugin::LfoConfiguration restoredLfos;
    const auto lfoNode = restoredState.getChildWithName(lfoConfigurationStateType);
    if (!lfoNode.isValid() && schemaVersion >= 7)
        return;
    if (lfoNode.isValid())
    {
        for (int i = 0; i < lfoNode.getNumChildren(); ++i)
        {
            const auto entry = lfoNode.getChild(i);
            if (entry.hasType("LFO"))
            {
                if (restoredLfos.moduleCount >= hypercolor::plugin::maximumLfoModules)
                    return;
                double identity = 0.0, rate = 0.0, depth = 0.0, phase = 0.0, offset = 0.0;
                const auto waveform = static_cast<int>(entry.getProperty("waveform", -1));
                const auto rateMode = static_cast<int>(entry.getProperty("rateMode", -1));
                const auto division = static_cast<int>(entry.getProperty("division", -1));
                if (!readFiniteNumber(entry.getProperty("identity"), identity)
                    || identity < 1.0 || identity > 9007199254740991.0 || identity != std::floor(identity)
                    || !readFiniteNumber(entry.getProperty("rateHz"), rate)
                    || !readFiniteNumber(entry.getProperty("depth"), depth)
                    || !readFiniteNumber(entry.getProperty("phase"), phase)
                    || !readFiniteNumber(entry.getProperty("offset"), offset)
                    || waveform < 0 || waveform > 5 || rateMode < 0 || rateMode > 1
                    || division < 0 || division >= static_cast<int>(hypercolor::plugin::lfoDivisionLabels.size()))
                    return;
                auto& module = restoredLfos.modules[restoredLfos.moduleCount++];
                module.stableIdentity = static_cast<std::uint64_t>(identity);
                module.enabled = static_cast<bool>(entry.getProperty("enabled", true));
                module.waveform = static_cast<hypercolor::dsp::modulation::LfoWaveform>(waveform);
                module.rateMode = static_cast<hypercolor::plugin::LfoRateMode>(rateMode);
                module.rateHz = static_cast<float>(rate);
                module.division = static_cast<hypercolor::plugin::LfoDivision>(division);
                module.depth = static_cast<float>(depth);
                module.phase = static_cast<float>(phase);
                module.offset = static_cast<float>(offset);
            }
            else if (entry.hasType("ASSIGNMENT"))
            {
                if (restoredLfos.assignmentCount >= hypercolor::plugin::maximumLfoAssignments)
                    return;
                double lfoIdentity = 0.0, fxIdentity = 0.0, amount = 0.0;
                if (!readFiniteNumber(entry.getProperty("lfoIdentity"), lfoIdentity)
                    || !readFiniteNumber(entry.getProperty("fxIdentity"), fxIdentity)
                    || !readFiniteNumber(entry.getProperty("amount"), amount)
                    || lfoIdentity < 1.0 || fxIdentity < 1.0
                    || lfoIdentity > 9007199254740991.0 || fxIdentity > 9007199254740991.0
                    || lfoIdentity != std::floor(lfoIdentity) || fxIdentity != std::floor(fxIdentity))
                    return;
                auto& assignment = restoredLfos.assignments[restoredLfos.assignmentCount++];
                assignment.lfoIdentity = static_cast<std::uint64_t>(lfoIdentity);
                assignment.fxInstanceIdentity = static_cast<std::uint64_t>(fxIdentity);
                assignment.parameterID = entry.getProperty("parameter").toString();
                assignment.amount = static_cast<float>(amount);
                assignment.bipolar = static_cast<bool>(entry.getProperty("bipolar", true));
                assignment.inverted = static_cast<bool>(entry.getProperty("inverted", false));
                if (assignment.parameterID.isEmpty())
                    return;
            }
            else
                return;
        }
    }

    std::uint64_t restoredNextLfoIdentity = 1;
    if (restoredState.hasProperty(nextLfoIdentityProperty))
    {
        double identity = 0.0;
        if (!readFiniteNumber(restoredState.getProperty(nextLfoIdentityProperty), identity)
            || identity < 1.0 || identity > 9007199254740991.0 || identity != std::floor(identity))
            return;
        restoredNextLfoIdentity = static_cast<std::uint64_t>(identity);
    }
    for (std::size_t i = 0; i < restoredLfos.moduleCount; ++i)
        restoredNextLfoIdentity = std::max(restoredNextLfoIdentity,
            restoredLfos.modules[i].stableIdentity + 1);

    std::uint64_t restoredNextIdentity = 25;
    if (restoredState.hasProperty(nextFxIdentityProperty))
    {
        double nextIdentity = 0.0;
        if (!readFiniteNumber(restoredState.getProperty(nextFxIdentityProperty), nextIdentity)
            || nextIdentity < 25.0 || nextIdentity > 9007199254740991.0
            || nextIdentity != std::floor(nextIdentity))
            return;
        restoredNextIdentity = static_cast<std::uint64_t>(nextIdentity);
    }
    for (std::size_t i = 0; i < restoredFxChain.count; ++i)
        restoredNextIdentity = std::max(restoredNextIdentity,
            restoredFxChain.order[i].stableIdentity + 1);

    if (!migrateLegacyDensityDivisionOrder(restoredState, schemaVersion))
        return;

    const auto parameterTemplateState = parameters.copyState();

    // Additional-instance parameters are predeclared APVTS parameters. Old
    // states have no values for them, so explicitly materialise each true
    // parameter default instead of copying the processor's live value.
    for (std::size_t type = 0; type < hypercolor::plugin::fxTypeCount; ++type)
    {
        for (std::size_t slot = 1; slot < hypercolor::plugin::maximumFxInstancesPerType; ++slot)
        {
            const auto addParameterDefault = [this, &restoredState, &parameterTemplateState](const juce::String& id)
            {
                if (restoredState.getChildWithProperty("id", id).isValid())
                    return true;
                const auto templateChild = parameterTemplateState.getChildWithProperty("id", id);
                auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameters.getParameter(id));
                if (!templateChild.isValid() || ranged == nullptr)
                    return false;
                auto child = templateChild.createCopy();
                child.setProperty("value", ranged->convertFrom0to1(ranged->getDefaultValue()), nullptr);
                restoredState.addChild(child, -1, nullptr);
                return true;
            };
            const auto fxType = static_cast<hypercolor::plugin::FxType>(type);
            if (!addParameterDefault(hypercolor::plugin::fxEnabledParameterID(
                    fxType, static_cast<std::uint8_t>(slot), fxEnabledLegacyIDs[type])))
                return;
            for (const auto* legacyID : fxLegacyParameterIDs[type])
                if (legacyID != nullptr
                    && !addParameterDefault(hypercolor::plugin::fxParameterID(
                        fxType, static_cast<std::uint8_t>(slot), legacyID)))
                    return;

            const auto validateParameter = [this, &restoredState](const juce::String& id)
            {
                const auto child = restoredState.getChildWithProperty("id", id);
                auto* parameter = parameters.getParameter(id);
                auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter);
                if (!child.isValid() || ranged == nullptr || !child.hasProperty("value"))
                    return false;
                double value = 0.0;
                if (!readFiniteNumber(child.getProperty("value"), value))
                    return false;
                const auto range = ranged->getNormalisableRange();
                if (value < range.start || value > range.end)
                    return false;
                if (dynamic_cast<juce::AudioParameterBool*>(parameter) != nullptr)
                    return value == 0.0 || value == 1.0;
                if (dynamic_cast<juce::AudioParameterChoice*>(parameter) != nullptr)
                    return value == std::floor(value);
                return true;
            };
            const auto enabledID = hypercolor::plugin::fxEnabledParameterID(
                fxType, static_cast<std::uint8_t>(slot), fxEnabledLegacyIDs[type]);
            if (!validateParameter(enabledID))
                return;
            for (const auto* legacyID : fxLegacyParameterIDs[type])
                if (legacyID != nullptr
                    && !validateParameter(hypercolor::plugin::fxParameterID(
                        fxType, static_cast<std::uint8_t>(slot), legacyID)))
                    return;
        }
    }

    // FX enable parameters were introduced in schema 3.  Legacy projects did
    // not contain these children at all, so a missing value must preserve the
    // historical audible behaviour (enabled).  For schema 3 and newer, a
    // missing child is a damaged/incomplete state and receives the parameter's
    // safe default (disabled).  Existing children are never overwritten.
    const auto legacyFxEnableDefault = schemaVersion < 3 ? 1.0 : 0.0;
    for (const auto* parameterID : fxEnableParameterIDs)
    {
        if (!addDefaultParameterIfMissing(
                restoredState,
                parameterTemplateState,
                parameterID,
                legacyFxEnableDefault
            ))
        {
            return;
        }
    }

    if (!addDefaultParameterIfMissing(
            restoredState,
            parameterTemplateState,
            hypercolor::ParameterIDs::grainDensity,
            defaultGranularParameters.densityGrainsPerSecond
        )
        || !addDefaultParameterIfMissing(
            restoredState,
            parameterTemplateState,
            hypercolor::ParameterIDs::grainDensityMode,
            grainDensityModeDefaultIndex
        )
        || !addDefaultParameterIfMissing(
            restoredState,
            parameterTemplateState,
            hypercolor::ParameterIDs::granularSourceMode,
            granularSourceModeLiveIndex
        )
        || !addDefaultParameterIfMissing(
            restoredState,
            parameterTemplateState,
            hypercolor::ParameterIDs::grainDensityDivision,
            grainDensityDivisionDefaultIndex
        )
        || !addDefaultParameterIfMissing(
            restoredState,
            parameterTemplateState,
            hypercolor::ParameterIDs::grainSize,
            defaultGranularParameters.grainSizeMilliseconds
        )
        || !addDefaultParameterIfMissing(
            restoredState,
            parameterTemplateState,
            hypercolor::ParameterIDs::grainPosition,
            defaultGranularParameters.positionNormalized
        )
        || !addDefaultParameterIfMissing(
            restoredState,
            parameterTemplateState,
            hypercolor::ParameterIDs::grainPitch,
            defaultGranularParameters.pitchSemitones
        )
        || !addDefaultParameterIfMissing(
            restoredState,
            parameterTemplateState,
            hypercolor::ParameterIDs::grainReverse,
            static_cast<double>(defaultGranularParameters.reverse)
        )
        || !addDefaultParameterIfMissing(
            restoredState,
            parameterTemplateState,
            hypercolor::ParameterIDs::grainCharacter,
            granularCharacterDefaultIndex
        )
        || !addDefaultParameterIfMissing(
            restoredState,
            parameterTemplateState,
            hypercolor::ParameterIDs::grainPitchRandom,
            granularRandomDefaultPercent
        )
        || !addDefaultParameterIfMissing(
            restoredState,
            parameterTemplateState,
            hypercolor::ParameterIDs::grainPositionRandom,
            granularRandomDefaultPercent
        )
        || !addDefaultParameterIfMissing(
            restoredState,
            parameterTemplateState,
            hypercolor::ParameterIDs::grainTimingJitter,
            granularRandomDefaultPercent
        )
        || !addDefaultParameterIfMissing(
            restoredState,
            parameterTemplateState,
            hypercolor::ParameterIDs::grainStereoSpread,
            granularRandomDefaultPercent
        )
        || !addDefaultParameterIfMissing(
            restoredState,
            parameterTemplateState,
            hypercolor::ParameterIDs::grainPanRandom,
            granularRandomDefaultPercent
        )
        || !addDefaultParameterIfMissing(
            restoredState,
            parameterTemplateState,
            hypercolor::ParameterIDs::grainEnvelope,
            grainEnvelopeDefaultIndex
        )
        || !addDefaultParameterIfMissing(
            restoredState, parameterTemplateState,
            hypercolor::ParameterIDs::macroX, 0.5
        )
        || !addDefaultParameterIfMissing(
            restoredState, parameterTemplateState,
            hypercolor::ParameterIDs::macroY, 0.5
        )
        || !addDefaultParameterIfMissing(
            restoredState,
            parameterTemplateState,
            hypercolor::ParameterIDs::reverbMix,
            30.0
        )
        || !addDefaultParameterIfMissing(
            restoredState,
            parameterTemplateState,
            hypercolor::ParameterIDs::reverbSize,
            50.0
        )
        || !addDefaultParameterIfMissing(
            restoredState,
            parameterTemplateState,
            hypercolor::ParameterIDs::reverbDecay,
            4.0
        )
        || !addDefaultParameterIfMissing(
            restoredState,
            parameterTemplateState,
            hypercolor::ParameterIDs::reverbDamping,
            40.0
        )
        || !addDefaultParameterIfMissing(
            restoredState,
            parameterTemplateState,
            hypercolor::ParameterIDs::reverbPreDelay,
            20.0
        )
        || !addDefaultParameterIfMissing(
            restoredState,
            parameterTemplateState,
            hypercolor::ParameterIDs::reverbWidth,
            100.0
        )
        || !addDefaultParameterIfMissing(restoredState, parameterTemplateState, hypercolor::ParameterIDs::modulationMode, 0.0)
        || !addDefaultParameterIfMissing(restoredState, parameterTemplateState, hypercolor::ParameterIDs::modulationRate, 0.3)
        || !addDefaultParameterIfMissing(restoredState, parameterTemplateState, hypercolor::ParameterIDs::modulationDepth, 50.0)
        || !addDefaultParameterIfMissing(restoredState, parameterTemplateState, hypercolor::ParameterIDs::modulationBaseDelay, 7.0)
        || !addDefaultParameterIfMissing(restoredState, parameterTemplateState, hypercolor::ParameterIDs::modulationFeedback, 0.0)
        || !addDefaultParameterIfMissing(restoredState, parameterTemplateState, hypercolor::ParameterIDs::modulationMix, 30.0)
        || !addDefaultParameterIfMissing(restoredState, parameterTemplateState, hypercolor::ParameterIDs::modulationWidth, 100.0
        )
        || !addDefaultParameterIfMissing(restoredState, parameterTemplateState, hypercolor::ParameterIDs::driveMode, 0.0)
        || !addDefaultParameterIfMissing(restoredState, parameterTemplateState, hypercolor::ParameterIDs::driveAmount, 0.0)
        || !addDefaultParameterIfMissing(restoredState, parameterTemplateState, hypercolor::ParameterIDs::driveTone, 50.0)
        || !addDefaultParameterIfMissing(restoredState, parameterTemplateState, hypercolor::ParameterIDs::driveMix, 100.0)
        || !addDefaultParameterIfMissing(restoredState, parameterTemplateState, hypercolor::ParameterIDs::driveOutput, 0.0
        )
        || !addDefaultParameterIfMissing(restoredState, parameterTemplateState, hypercolor::ParameterIDs::delayMode, 0.0)
        || !addDefaultParameterIfMissing(restoredState, parameterTemplateState, hypercolor::ParameterIDs::delaySync, 0.0)
        || !addDefaultParameterIfMissing(restoredState, parameterTemplateState, hypercolor::ParameterIDs::delayTime, 350.0)
        || !addDefaultParameterIfMissing(restoredState, parameterTemplateState, hypercolor::ParameterIDs::delayDivision, 2.0)
        || !addDefaultParameterIfMissing(restoredState, parameterTemplateState, hypercolor::ParameterIDs::delayFeedback, 35.0)
        || !addDefaultParameterIfMissing(restoredState, parameterTemplateState, hypercolor::ParameterIDs::delayLowCut, 100.0)
        || !addDefaultParameterIfMissing(restoredState, parameterTemplateState, hypercolor::ParameterIDs::delayHighCut, 12000.0)
        || !addDefaultParameterIfMissing(restoredState, parameterTemplateState, hypercolor::ParameterIDs::delayMix, 30.0)
        || !addDefaultParameterIfMissing(restoredState, parameterTemplateState, hypercolor::ParameterIDs::delayWidth, 100.0
        )
        || !addDefaultParameterIfMissing(restoredState, parameterTemplateState, hypercolor::ParameterIDs::tapeDrive, 35.0)
        || !addDefaultParameterIfMissing(restoredState, parameterTemplateState, hypercolor::ParameterIDs::tapeWow, 15.0)
        || !addDefaultParameterIfMissing(restoredState, parameterTemplateState, hypercolor::ParameterIDs::tapeFlutter, 10.0)
        || !addDefaultParameterIfMissing(restoredState, parameterTemplateState, hypercolor::ParameterIDs::tapeWear, 15.0)
        || !addDefaultParameterIfMissing(restoredState, parameterTemplateState, hypercolor::ParameterIDs::tapeMix, 100.0)
        || !addDefaultParameterIfMissing(restoredState, parameterTemplateState, hypercolor::ParameterIDs::tapeOutput, 0.0
        )
        || !addDefaultParameterIfMissing(restoredState, parameterTemplateState, hypercolor::ParameterIDs::bitcrushBits, 16.0)
        || !addDefaultParameterIfMissing(restoredState, parameterTemplateState, hypercolor::ParameterIDs::bitcrushRate, 22050.0)
        || !addDefaultParameterIfMissing(restoredState, parameterTemplateState, hypercolor::ParameterIDs::bitcrushMode, 0.0)
        || !addDefaultParameterIfMissing(restoredState, parameterTemplateState, hypercolor::ParameterIDs::bitcrushMix, 100.0)
        || !addDefaultParameterIfMissing(restoredState, parameterTemplateState, hypercolor::ParameterIDs::bitcrushOutput, 0.0
        )
        || !addDefaultParameterIfMissing(restoredState, parameterTemplateState, hypercolor::ParameterIDs::filterMode, 0.0)
        || !addDefaultParameterIfMissing(restoredState, parameterTemplateState, hypercolor::ParameterIDs::filterCutoff, 10000.0)
        || !addDefaultParameterIfMissing(restoredState, parameterTemplateState, hypercolor::ParameterIDs::filterResonance, 20.0)
        || !addDefaultParameterIfMissing(restoredState, parameterTemplateState, hypercolor::ParameterIDs::filterDrive, 0.0)
        || !addDefaultParameterIfMissing(restoredState, parameterTemplateState, hypercolor::ParameterIDs::filterMix, 100.0)
        || !addDefaultParameterIfMissing(restoredState, parameterTemplateState, hypercolor::ParameterIDs::filterOutput, 0.0
        )
        || !addDefaultParameterIfMissing(restoredState, parameterTemplateState, hypercolor::ParameterIDs::compressorThreshold, -18.0)
        || !addDefaultParameterIfMissing(restoredState, parameterTemplateState, hypercolor::ParameterIDs::compressorRatio, 4.0)
        || !addDefaultParameterIfMissing(restoredState, parameterTemplateState, hypercolor::ParameterIDs::compressorAttack, 10.0)
        || !addDefaultParameterIfMissing(restoredState, parameterTemplateState, hypercolor::ParameterIDs::compressorRelease, 100.0)
        || !addDefaultParameterIfMissing(restoredState, parameterTemplateState, hypercolor::ParameterIDs::compressorKnee, 6.0)
        || !addDefaultParameterIfMissing(restoredState, parameterTemplateState, hypercolor::ParameterIDs::compressorMakeup, 0.0)
        || !addDefaultParameterIfMissing(restoredState, parameterTemplateState, hypercolor::ParameterIDs::compressorMix, 100.0)
        || !addDefaultParameterIfMissing(restoredState, parameterTemplateState, hypercolor::ParameterIDs::compressorStereoLink, 100.0
        ))
    {
        return;
    }

    if (schemaVersion < 0
        || schemaVersion > currentStateSchemaVersion
        || !hasValidParameterValue(
            restoredState,
            hypercolor::ParameterIDs::dryWet,
            dryWetMinimum,
            dryWetMaximum
        )
        || !hasValidParameterValue(
            restoredState,
            hypercolor::ParameterIDs::outputGain,
            outputGainMinimumDb,
            outputGainMaximumDb
        )
        || !hasValidParameterValue(restoredState, hypercolor::ParameterIDs::macroX, 0.0f, 1.0f)
        || !hasValidParameterValue(restoredState, hypercolor::ParameterIDs::macroY, 0.0f, 1.0f)
        || !hasValidParameterValue(
            restoredState,
            hypercolor::ParameterIDs::grainDensity,
            static_cast<float>(
                hypercolor::dsp::granular::GrainScheduler::minimumDensityGrainsPerSecond
            ),
            static_cast<float>(
                hypercolor::dsp::granular::GrainScheduler::maximumDensityGrainsPerSecond
            )
        )
        || !hasValidChoiceParameterValue(
            restoredState,
            hypercolor::ParameterIDs::grainDensityMode,
            grainDensityModeMinimumIndex,
            grainDensityModeMaximumIndex
        )
        || !hasValidChoiceParameterValue(
            restoredState,
            hypercolor::ParameterIDs::granularSourceMode,
            granularSourceModeMinimumIndex,
            granularSourceModeMaximumIndex
        )
        || !hasValidChoiceParameterValue(
            restoredState,
            hypercolor::ParameterIDs::grainDensityDivision,
            grainDensityDivisionMinimumIndex,
            grainDensityDivisionMaximumIndex
        )
        || !hasValidBooleanParameterValue(
            restoredState,
            hypercolor::ParameterIDs::reverbEnabled
        )
        || !hasValidBooleanParameterValue(
            restoredState,
            hypercolor::ParameterIDs::modulationEnabled
        )
        || !hasValidBooleanParameterValue(
            restoredState,
            hypercolor::ParameterIDs::driveEnabled
        )
        || !hasValidBooleanParameterValue(
            restoredState,
            hypercolor::ParameterIDs::tapeEnabled
        )
        || !hasValidBooleanParameterValue(
            restoredState,
            hypercolor::ParameterIDs::bitcrushEnabled
        )
        || !hasValidBooleanParameterValue(
            restoredState,
            hypercolor::ParameterIDs::filterEnabled
        )
        || !hasValidBooleanParameterValue(
            restoredState,
            hypercolor::ParameterIDs::compressorEnabled
        )
        || !hasValidBooleanParameterValue(
            restoredState,
            hypercolor::ParameterIDs::delayEnabled
        )
        || !hasValidParameterValue(
            restoredState,
            hypercolor::ParameterIDs::grainSize,
            static_cast<float>(
                hypercolor::dsp::granular::GrainScheduler::minimumGrainSizeMilliseconds
            ),
            static_cast<float>(
                hypercolor::dsp::granular::GrainScheduler::maximumGrainSizeMilliseconds
            )
        )
        || !hasValidParameterValue(
            restoredState,
            hypercolor::ParameterIDs::grainPosition,
            0.0f,
            1.0f
        )
        || !hasValidParameterValue(
            restoredState,
            hypercolor::ParameterIDs::grainPitch,
            static_cast<float>(
                hypercolor::dsp::granular::GrainScheduler::minimumPitchSemitones
            ),
            static_cast<float>(
                hypercolor::dsp::granular::GrainScheduler::maximumPitchSemitones
            )
        )
        || !hasValidBooleanParameterValue(
            restoredState,
            hypercolor::ParameterIDs::grainReverse
        )
        || !hasValidChoiceParameterValue(
            restoredState,
            hypercolor::ParameterIDs::grainCharacter,
            granularCharacterMinimumIndex,
            granularCharacterMaximumIndex
        )
        || !hasValidParameterValue(
            restoredState,
            hypercolor::ParameterIDs::grainPitchRandom,
            granularRandomMinimumPercent,
            granularRandomMaximumPercent
        )
        || !hasValidParameterValue(
            restoredState,
            hypercolor::ParameterIDs::grainPositionRandom,
            granularRandomMinimumPercent,
            granularRandomMaximumPercent
        )
        || !hasValidParameterValue(
            restoredState,
            hypercolor::ParameterIDs::grainTimingJitter,
            granularRandomMinimumPercent,
            granularRandomMaximumPercent
        )
        || !hasValidParameterValue(
            restoredState,
            hypercolor::ParameterIDs::grainStereoSpread,
            granularRandomMinimumPercent,
            granularRandomMaximumPercent
        )
        || !hasValidParameterValue(
            restoredState,
            hypercolor::ParameterIDs::grainPanRandom,
            granularRandomMinimumPercent,
            granularRandomMaximumPercent
        )
        || !hasValidChoiceParameterValue(
            restoredState,
            hypercolor::ParameterIDs::grainEnvelope,
            grainEnvelopeMinimumIndex,
            grainEnvelopeMaximumIndex
        )
        || !hasValidParameterValue(restoredState, hypercolor::ParameterIDs::reverbMix, 0.0f, 100.0f)
        || !hasValidParameterValue(restoredState, hypercolor::ParameterIDs::reverbSize, 0.0f, 100.0f)
        || !hasValidParameterValue(restoredState, hypercolor::ParameterIDs::reverbDecay, 0.1f, 20.0f)
        || !hasValidParameterValue(restoredState, hypercolor::ParameterIDs::reverbDamping, 0.0f, 100.0f)
        || !hasValidParameterValue(restoredState, hypercolor::ParameterIDs::reverbPreDelay, 0.0f, 200.0f)
        || !hasValidParameterValue(restoredState, hypercolor::ParameterIDs::reverbWidth, 0.0f, 200.0f)
        || !hasValidChoiceParameterValue(restoredState, hypercolor::ParameterIDs::modulationMode, 0, 1)
        || !hasValidParameterValue(restoredState, hypercolor::ParameterIDs::modulationRate, 0.01f, 10.0f)
        || !hasValidParameterValue(restoredState, hypercolor::ParameterIDs::modulationDepth, 0.0f, 100.0f)
        || !hasValidParameterValue(restoredState, hypercolor::ParameterIDs::modulationBaseDelay, 0.1f, 20.0f)
        || !hasValidParameterValue(restoredState, hypercolor::ParameterIDs::modulationFeedback, -95.0f, 95.0f)
        || !hasValidParameterValue(restoredState, hypercolor::ParameterIDs::modulationMix, 0.0f, 100.0f)
        || !hasValidParameterValue(restoredState, hypercolor::ParameterIDs::modulationWidth, 0.0f, 200.0f)
        || !hasValidChoiceParameterValue(restoredState, hypercolor::ParameterIDs::driveMode, 0, 2)
        || !hasValidParameterValue(restoredState, hypercolor::ParameterIDs::driveAmount, 0.0f, 36.0f)
        || !hasValidParameterValue(restoredState, hypercolor::ParameterIDs::driveTone, 0.0f, 100.0f)
        || !hasValidParameterValue(restoredState, hypercolor::ParameterIDs::driveMix, 0.0f, 100.0f)
        || !hasValidParameterValue(restoredState, hypercolor::ParameterIDs::driveOutput, -24.0f, 12.0f)
        || !hasValidChoiceParameterValue(restoredState, hypercolor::ParameterIDs::delayMode, 0, 1)
        || !hasValidChoiceParameterValue(restoredState, hypercolor::ParameterIDs::delaySync, 0, 1)
        || !hasValidParameterValue(restoredState, hypercolor::ParameterIDs::delayTime, 1.0f, 2000.0f)
        || !hasValidChoiceParameterValue(restoredState, hypercolor::ParameterIDs::delayDivision, 0, 8)
        || !hasValidParameterValue(restoredState, hypercolor::ParameterIDs::delayFeedback, 0.0f, 95.0f)
        || !hasValidParameterValue(restoredState, hypercolor::ParameterIDs::delayLowCut, 20.0f, 2000.0f)
        || !hasValidParameterValue(restoredState, hypercolor::ParameterIDs::delayHighCut, 1000.0f, 20000.0f)
        || !hasValidParameterValue(restoredState, hypercolor::ParameterIDs::delayMix, 0.0f, 100.0f)
        || !hasValidParameterValue(restoredState, hypercolor::ParameterIDs::delayWidth, 0.0f, 200.0f)
        || !hasValidParameterValue(restoredState, hypercolor::ParameterIDs::tapeDrive, 0.0f, 100.0f)
        || !hasValidParameterValue(restoredState, hypercolor::ParameterIDs::tapeWow, 0.0f, 100.0f)
        || !hasValidParameterValue(restoredState, hypercolor::ParameterIDs::tapeFlutter, 0.0f, 100.0f)
        || !hasValidParameterValue(restoredState, hypercolor::ParameterIDs::tapeWear, 0.0f, 100.0f)
        || !hasValidParameterValue(restoredState, hypercolor::ParameterIDs::tapeMix, 0.0f, 100.0f)
        || !hasValidParameterValue(restoredState, hypercolor::ParameterIDs::tapeOutput, -24.0f, 12.0f)
        || !hasValidParameterValue(restoredState, hypercolor::ParameterIDs::bitcrushBits, 4.0f, 24.0f)
        || !hasValidParameterValue(restoredState, hypercolor::ParameterIDs::bitcrushRate, 500.0f, 44100.0f)
        || !hasValidChoiceParameterValue(restoredState, hypercolor::ParameterIDs::bitcrushMode, 0, 1)
        || !hasValidParameterValue(restoredState, hypercolor::ParameterIDs::bitcrushMix, 0.0f, 100.0f)
        || !hasValidParameterValue(restoredState, hypercolor::ParameterIDs::bitcrushOutput, -24.0f, 12.0f)
        || !hasValidChoiceParameterValue(restoredState, hypercolor::ParameterIDs::filterMode, 0, 3)
        || !hasValidParameterValue(restoredState, hypercolor::ParameterIDs::filterCutoff, 20.0f, 20000.0f)
        || !hasValidParameterValue(restoredState, hypercolor::ParameterIDs::filterResonance, 0.0f, 100.0f)
        || !hasValidParameterValue(restoredState, hypercolor::ParameterIDs::filterDrive, 0.0f, 100.0f)
        || !hasValidParameterValue(restoredState, hypercolor::ParameterIDs::filterMix, 0.0f, 100.0f)
        || !hasValidParameterValue(restoredState, hypercolor::ParameterIDs::filterOutput, -24.0f, 12.0f)
        || !hasValidParameterValue(restoredState, hypercolor::ParameterIDs::compressorThreshold, -60.0f, 0.0f)
        || !hasValidParameterValue(restoredState, hypercolor::ParameterIDs::compressorRatio, 1.0f, 20.0f)
        || !hasValidParameterValue(restoredState, hypercolor::ParameterIDs::compressorAttack, 0.1f, 100.0f)
        || !hasValidParameterValue(restoredState, hypercolor::ParameterIDs::compressorRelease, 10.0f, 1000.0f)
        || !hasValidParameterValue(restoredState, hypercolor::ParameterIDs::compressorKnee, 0.0f, 24.0f)
        || !hasValidParameterValue(restoredState, hypercolor::ParameterIDs::compressorMakeup, 0.0f, 24.0f)
        || !hasValidParameterValue(restoredState, hypercolor::ParameterIDs::compressorMix, 0.0f, 100.0f)
        || !hasValidParameterValue(restoredState, hypercolor::ParameterIDs::compressorStereoLink, 0.0f, 100.0f)
        )
    {
        return;
    }

    // Mark the in-memory tree as current after validation.  A subsequent
    // save cannot be mistaken for the legacy ordering and migrated again.
    restoredState.setProperty(
        stateSchemaProperty,
        currentStateSchemaVersion,
        nullptr
    );
    parameters.replaceState(restoredState);
    (void) setFxChainConfiguration(restoredFxChain);
    nextFxInstanceIdentity.store(restoredNextIdentity, std::memory_order_relaxed);
    if (!setFxMacroAssignments(restoredAssignments))
        (void) setFxMacroAssignments({});
    if (!setLfoConfiguration(restoredLfos))
        (void) setLfoConfiguration({});
    nextLfoIdentity.store(restoredNextLfoIdentity, std::memory_order_relaxed);
}

hypercolor::plugin::FxChainConfiguration
HypercolorAudioProcessor::getFxChainConfiguration() const noexcept
{
    hypercolor::plugin::FxChainConfiguration result;
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        const auto before = fxConfigurationSequence.load(std::memory_order_acquire);
        if ((before & 1u) != 0u)
            continue;
        result.count = fxConfigurationCount.load(std::memory_order_relaxed);
        for (std::size_t i = 0; i < result.count; ++i)
        {
            const auto packed = fxConfigurationPacked[i].load(std::memory_order_relaxed);
            result.order[i] = {
                static_cast<hypercolor::plugin::FxType>((packed >> 2u) & 0x07u),
                static_cast<std::uint8_t>(packed & 0x03u),
                fxConfigurationIdentities[i].load(std::memory_order_relaxed)
            };
        }
        if (before == fxConfigurationSequence.load(std::memory_order_acquire))
            return result;
    }
    result.count = 0;
    return result;
}

bool HypercolorAudioProcessor::setFxChainConfiguration(
    const hypercolor::plugin::FxChainConfiguration& configuration
) noexcept
{
    if (configuration.count > hypercolor::plugin::maximumFxChainModules)
        return false;
    std::array<bool, hypercolor::plugin::fxTypeCount * hypercolor::plugin::maximumFxInstancesPerType> used {};
    std::array<std::uint64_t, hypercolor::plugin::maximumFxChainModules> identities {};
    for (std::size_t i = 0; i < configuration.count; ++i)
    {
        const auto id = configuration.order[i];
        if (hypercolor::plugin::toIndex(id.type) >= hypercolor::plugin::fxTypeCount
            || id.slot >= hypercolor::plugin::maximumFxInstancesPerType)
            return false;
        const auto flat = static_cast<std::size_t>(hypercolor::plugin::toIndex(id.type))
            * hypercolor::plugin::maximumFxInstancesPerType + id.slot;
        if (used[flat] || id.stableIdentity == 0)
            return false;
        used[flat] = true;
        for (std::size_t previous = 0; previous < i; ++previous)
            if (identities[previous] == id.stableIdentity)
                return false;
        identities[i] = id.stableIdentity;
    }
    fxConfigurationSequence.fetch_add(1, std::memory_order_acq_rel);
    for (std::size_t i = 0; i < configuration.count; ++i)
    {
        const auto id = configuration.order[i];
        fxConfigurationPacked[i].store(
            static_cast<std::uint8_t>((hypercolor::plugin::toIndex(id.type) << 2u) | id.slot),
            std::memory_order_relaxed
        );
        fxConfigurationIdentities[i].store(
            configuration.order[i].stableIdentity,
            std::memory_order_relaxed
        );
    }
    fxConfigurationCount.store(configuration.count, std::memory_order_relaxed);
    fxConfigurationSequence.fetch_add(1, std::memory_order_release);
    return true;
}

void HypercolorAudioProcessor::applyFxChainConfiguration() noexcept
{
    const auto sequenceBefore = fxConfigurationSequence.load(std::memory_order_acquire);
    if ((sequenceBefore & 1u) != 0u)
        return;
    const auto configuration = getFxChainConfiguration();
    if (sequenceBefore != fxConfigurationSequence.load(std::memory_order_acquire))
        return;
    std::array<std::uint8_t, hypercolor::plugin::maximumFxChainModules> indices {};
    for (auto& typeIdentities : audioFxInstanceIdentities)
        typeIdentities.fill(0);
    for (std::size_t i = 0; i < configuration.count; ++i)
    {
        const auto type = hypercolor::plugin::toIndex(configuration.order[i].type);
        const auto slot = configuration.order[i].slot;
        indices[i] = static_cast<std::uint8_t>(
            type
                * hypercolor::plugin::maximumFxInstancesPerType
            + slot
        );
        audioFxInstanceIdentities[type][slot] = configuration.order[i].stableIdentity;
    }
    (void) effectChain.setActiveOrder(
        std::span<const std::uint8_t>(indices.data(), configuration.count)
    );
}

std::uint64_t HypercolorAudioProcessor::allocateFxInstanceIdentity() noexcept
{
    return nextFxInstanceIdentity.fetch_add(1, std::memory_order_relaxed);
}

hypercolor::plugin::FxMacroAssignmentSet
HypercolorAudioProcessor::getFxMacroAssignments() const
{
    const juce::ScopedLock lock(macroAssignmentsLock);
    return macroAssignmentsMessageThread;
}

hypercolor::plugin::LfoConfiguration
HypercolorAudioProcessor::getLfoConfiguration() const
{
    const juce::ScopedLock lock(lfoConfigurationLock);
    return lfoConfigurationMessageThread;
}

std::uint32_t HypercolorAudioProcessor::getLfoConfigurationRevision() const noexcept
{
    return lfoConfigurationSequence.load(std::memory_order_acquire);
}

std::uint64_t HypercolorAudioProcessor::allocateLfoIdentity() noexcept
{
    return nextLfoIdentity.fetch_add(1, std::memory_order_relaxed);
}

bool HypercolorAudioProcessor::setLfoConfiguration(
    const hypercolor::plugin::LfoConfiguration& configuration)
{
    using namespace hypercolor::plugin;
    using Waveform = hypercolor::dsp::modulation::LfoWaveform;
    if (configuration.moduleCount > maximumLfoModules
        || configuration.assignmentCount > maximumLfoAssignments)
        return false;

    const juce::ScopedLock lock(lfoConfigurationLock);
    const auto chain = getFxChainConfiguration();
    std::array<LfoModuleRuntime, maximumLfoModules> modules {};
    std::array<LfoAssignmentRuntime, maximumLfoAssignments> assignments {};
    for (std::size_t i = 0; i < configuration.moduleCount; ++i)
    {
        const auto& source = configuration.modules[i];
        if (source.stableIdentity == 0 || source.stableIdentity > 9007199254740991ull
            || static_cast<std::uint8_t>(source.waveform) > static_cast<std::uint8_t>(Waveform::sampleAndHold)
            || static_cast<std::uint8_t>(source.rateMode) > 1
            || static_cast<std::uint8_t>(source.division) >= lfoDivisionLabels.size()
            || !std::isfinite(source.rateHz) || source.rateHz < 0.0f || source.rateHz > 40.0f
            || !std::isfinite(source.depth) || source.depth < 0.0f || source.depth > 1.0f
            || !std::isfinite(source.phase) || source.phase < 0.0f || source.phase > 1.0f
            || !std::isfinite(source.offset) || source.offset < -1.0f || source.offset > 1.0f)
            return false;
        for (std::size_t j = 0; j < i; ++j)
            if (configuration.modules[j].stableIdentity == source.stableIdentity)
                return false;
        modules[i] = { source.stableIdentity, source.enabled, source.waveform,
            source.rateMode, source.division, source.rateHz, source.depth,
            source.phase, source.offset };
    }

    for (std::size_t i = 0; i < configuration.assignmentCount; ++i)
    {
        const auto& source = configuration.assignments[i];
        if (source.lfoIdentity == 0 || source.fxInstanceIdentity == 0
            || source.parameterID.isEmpty() || !std::isfinite(source.amount)
            || source.amount < 0.0f || source.amount > 1.0f)
            return false;
        for (std::size_t previous = 0; previous < i; ++previous)
        {
            const auto& other = configuration.assignments[previous];
            if (other.lfoIdentity == source.lfoIdentity
                && other.fxInstanceIdentity == source.fxInstanceIdentity
                && other.parameterID == source.parameterID)
                return false;
        }
        std::size_t lfoSlot = maximumLfoModules;
        for (std::size_t j = 0; j < configuration.moduleCount; ++j)
            if (configuration.modules[j].stableIdentity == source.lfoIdentity)
            { lfoSlot = j; break; }
        if (lfoSlot >= maximumLfoModules)
            return false;
        auto* parameter = dynamic_cast<juce::RangedAudioParameter*>(
            parameters.getParameter(source.parameterID));
        if (parameter == nullptr)
            return false;
        if (dynamic_cast<juce::AudioParameterChoice*>(parameter) != nullptr
            || dynamic_cast<juce::AudioParameterBool*>(parameter) != nullptr)
            return false;
        const auto range = parameter->getNormalisableRange();
        if (!(range.end > range.start))
            return false;

        bool identityExists = false;
        bool identityMatchesParameter = false;
        for (std::size_t type = 0; type < fxTypeCount; ++type)
        {
            for (std::size_t slot = 0; slot < maximumFxInstancesPerType; ++slot)
            {
                const auto fxType = static_cast<FxType>(type);
                for (std::size_t parameterIndex = 0; parameterIndex < fxLegacyParameterIDs[type].size(); ++parameterIndex)
                {
                    const auto* legacyID = fxLegacyParameterIDs[type][parameterIndex];
                    if (legacyID == nullptr
                        || fxParameterID(fxType, static_cast<std::uint8_t>(slot), legacyID) != source.parameterID)
                        continue;
                    LfoAssignmentRuntime resolved;
                    resolved.lfoIdentity = source.lfoIdentity;
                    resolved.fxIdentity = source.fxInstanceIdentity;
                    resolved.type = static_cast<std::uint8_t>(type);
                    resolved.slot = static_cast<std::uint8_t>(slot);
                    resolved.parameterIndex = static_cast<std::uint8_t>(parameterIndex);
                    resolved.lfoSlot = static_cast<std::uint8_t>(lfoSlot);
                    resolved.amount = source.amount;
                    resolved.bipolar = source.bipolar;
                    resolved.inverted = source.inverted;
                    resolved.parameterStart = range.start;
                    resolved.parameterEnd = range.end;
                    resolved.parameterInterval = range.interval;
                    resolved.parameterSkew = range.skew;
                    for (std::size_t row = 0; row < chain.count; ++row)
                        if (chain.order[row].stableIdentity == source.fxInstanceIdentity)
                        {
                            identityExists = true;
                            if (chain.order[row].type == fxType && chain.order[row].slot == slot)
                            { resolved.active = true; identityMatchesParameter = true; }
                        }
                    assignments[i] = resolved;
                }
            }
        }
        bool foundFxParameter = false;
        for (std::size_t type = 0; type < fxTypeCount; ++type)
            for (std::size_t slot = 0; slot < maximumFxInstancesPerType; ++slot)
                for (const auto* legacyID : fxLegacyParameterIDs[type])
                    if (legacyID != nullptr
                        && fxParameterID(static_cast<FxType>(type), static_cast<std::uint8_t>(slot), legacyID) == source.parameterID)
                        foundFxParameter = true;
        if (!foundFxParameter)
            return false;
        if (identityExists && !identityMatchesParameter)
            return false;
    }

    lfoConfigurationSequence.fetch_add(1, std::memory_order_acq_rel);
    for (std::size_t i = 0; i < maximumLfoModules; ++i)
    {
        auto& destination = lfoRuntimeAtomic[i];
        const bool active = i < configuration.moduleCount;
        destination.identity.store(active ? modules[i].identity : 0, std::memory_order_relaxed);
        destination.enabled.store(active && modules[i].enabled, std::memory_order_relaxed);
        destination.waveform.store(active ? static_cast<std::uint8_t>(modules[i].waveform) : 0, std::memory_order_relaxed);
        destination.rateMode.store(active ? static_cast<std::uint8_t>(modules[i].rateMode) : 0, std::memory_order_relaxed);
        destination.division.store(active ? static_cast<std::uint8_t>(modules[i].division) : 3, std::memory_order_relaxed);
        destination.rateHz.store(active ? modules[i].rateHz : 1.0f, std::memory_order_relaxed);
        destination.depth.store(active ? modules[i].depth : 1.0f, std::memory_order_relaxed);
        destination.phase.store(active ? modules[i].phase : 0.0f, std::memory_order_relaxed);
        destination.offset.store(active ? modules[i].offset : 0.0f, std::memory_order_relaxed);
    }
    for (std::size_t i = 0; i < maximumLfoAssignments; ++i)
    {
        auto& destination = lfoAssignmentRuntimeAtomic[i];
        const bool active = i < configuration.assignmentCount && assignments[i].active;
        destination.active.store(active, std::memory_order_relaxed);
        destination.bipolar.store(i < configuration.assignmentCount && configuration.assignments[i].bipolar, std::memory_order_relaxed);
        destination.inverted.store(i < configuration.assignmentCount && configuration.assignments[i].inverted, std::memory_order_relaxed);
        destination.lfoIdentity.store(i < configuration.assignmentCount ? configuration.assignments[i].lfoIdentity : 0, std::memory_order_relaxed);
        destination.fxIdentity.store(i < configuration.assignmentCount ? configuration.assignments[i].fxInstanceIdentity : 0, std::memory_order_relaxed);
        destination.type.store(active ? assignments[i].type : 0, std::memory_order_relaxed);
        destination.slot.store(active ? assignments[i].slot : 0, std::memory_order_relaxed);
        destination.parameterIndex.store(active ? assignments[i].parameterIndex : 0, std::memory_order_relaxed);
        destination.lfoSlot.store(active ? assignments[i].lfoSlot : 0, std::memory_order_relaxed);
        destination.amount.store(i < configuration.assignmentCount ? configuration.assignments[i].amount : 1.0f, std::memory_order_relaxed);
        destination.parameterStart.store(active ? assignments[i].parameterStart : 0.0f, std::memory_order_relaxed);
        destination.parameterEnd.store(active ? assignments[i].parameterEnd : 1.0f, std::memory_order_relaxed);
        destination.parameterInterval.store(active ? assignments[i].parameterInterval : 0.0f, std::memory_order_relaxed);
        destination.parameterSkew.store(active ? assignments[i].parameterSkew : 1.0f, std::memory_order_relaxed);
    }
    lfoModuleCount.store(configuration.moduleCount, std::memory_order_relaxed);
    lfoAssignmentCount.store(configuration.assignmentCount, std::memory_order_relaxed);
    lfoConfigurationSequence.fetch_add(1, std::memory_order_release);
    lfoConfigurationMessageThread = configuration;
    std::uint64_t nextIdentity = 1;
    for (std::size_t i = 0; i < configuration.moduleCount; ++i)
        nextIdentity = std::max(nextIdentity, configuration.modules[i].stableIdentity + 1);
    nextLfoIdentity.store(std::max(nextLfoIdentity.load(std::memory_order_relaxed), nextIdentity),
                          std::memory_order_relaxed);
    return true;
}

bool HypercolorAudioProcessor::setFxMacroAssignments(
    const hypercolor::plugin::FxMacroAssignmentSet& assignments
)
{
    using namespace hypercolor::plugin;
    if (assignments.count > maximumFxMacroAssignments)
        return false;

    // Publication may come from the UI or state restore. Serialize writers
    // here; the audio reader consumes only the atomic snapshot and never takes
    // this lock.
    const juce::ScopedLock lock(macroAssignmentsLock);
    const auto chain = getFxChainConfiguration();
    std::array<MacroAssignmentRuntime, maximumFxMacroAssignments> runtime {};
    for (std::size_t i = 0; i < assignments.count; ++i)
    {
        const auto& assignment = assignments.assignments[i];
        if (static_cast<std::uint8_t>(assignment.axis) > 1
            || assignment.instanceIdentity == 0 || assignment.parameterID.isEmpty()
            || !std::isfinite(assignment.minimum) || !std::isfinite(assignment.maximum)
            || assignment.minimum > assignment.maximum)
            return false;

        auto* parameter = parameters.getParameter(assignment.parameterID);
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter);
        if (ranged == nullptr)
            return false;
        const auto range = ranged->getNormalisableRange();
        if (assignment.minimum < range.start || assignment.maximum > range.end)
            return false;

        for (std::size_t previous = 0; previous < i; ++previous)
        {
            const auto& other = assignments.assignments[previous];
            if (other.instanceIdentity == assignment.instanceIdentity
                && other.parameterID == assignment.parameterID)
                return false;
        }

        for (std::size_t row = 0; row < chain.count; ++row)
        {
            const auto instance = chain.order[row];
            if (instance.stableIdentity != assignment.instanceIdentity)
                continue;
            const auto type = toIndex(instance.type);
            for (std::size_t parameterIndex = 0;
                 parameterIndex < fxLegacyParameterIDs[type].size();
                 ++parameterIndex)
            {
                const auto* legacyID = fxLegacyParameterIDs[type][parameterIndex];
                if (legacyID == nullptr)
                    continue;
                if (fxParameterID(instance.type, instance.slot, legacyID)
                    == assignment.parameterID)
                {
                    runtime[i] = {
                        true, assignment.axis, type, instance.slot,
                        static_cast<std::uint8_t>(parameterIndex),
                        assignment.instanceIdentity,
                        assignment.minimum, assignment.maximum,
                        range.start, range.interval
                    };
                    break;
                }
            }
            if (!runtime[i].active)
                return false;
            break;
        }
        // An absent instance is retained as an orphan. It will not bind to a
        // later effect that reuses the same DSP slot because identity differs.
    }

    macroAssignmentSequence.fetch_add(1, std::memory_order_acq_rel);
    for (std::size_t i = 0; i < maximumFxMacroAssignments; ++i)
    {
        auto& destination = macroAssignmentRuntimeAtomic[i];
        const auto active = i < assignments.count && runtime[i].active;
        destination.active.store(active, std::memory_order_relaxed);
        if (active)
        {
            destination.axis.store(static_cast<std::uint8_t>(runtime[i].axis), std::memory_order_relaxed);
            destination.type.store(runtime[i].type, std::memory_order_relaxed);
            destination.slot.store(runtime[i].slot, std::memory_order_relaxed);
            destination.parameterIndex.store(runtime[i].parameterIndex, std::memory_order_relaxed);
            destination.instanceIdentity.store(runtime[i].instanceIdentity, std::memory_order_relaxed);
            destination.minimum.store(runtime[i].minimum, std::memory_order_relaxed);
            destination.maximum.store(runtime[i].maximum, std::memory_order_relaxed);
            destination.parameterStart.store(runtime[i].parameterStart, std::memory_order_relaxed);
            destination.parameterInterval.store(runtime[i].parameterInterval, std::memory_order_relaxed);
        }
    }
    macroAssignmentCount.store(assignments.count, std::memory_order_relaxed);
    macroAssignmentSequence.fetch_add(1, std::memory_order_release);
    macroAssignmentsMessageThread = assignments;
    return true;
}

void HypercolorAudioProcessor::applyFxMacroAssignmentSnapshot() noexcept
{
    auto before = macroAssignmentSequence.load(std::memory_order_acquire);
    if ((before & 1u) != 0u)
        return;
    const auto count = macroAssignmentCount.load(std::memory_order_relaxed);
    if (count > hypercolor::plugin::maximumFxMacroAssignments)
        return;
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto& source = macroAssignmentRuntimeAtomic[i];
        audioMacroAssignments[i] = {
            source.active.load(std::memory_order_relaxed),
            static_cast<hypercolor::plugin::MacroAxis>(source.axis.load(std::memory_order_relaxed)),
            source.type.load(std::memory_order_relaxed),
            source.slot.load(std::memory_order_relaxed),
            source.parameterIndex.load(std::memory_order_relaxed),
            source.instanceIdentity.load(std::memory_order_relaxed),
            source.minimum.load(std::memory_order_relaxed),
            source.maximum.load(std::memory_order_relaxed),
            source.parameterStart.load(std::memory_order_relaxed),
            source.parameterInterval.load(std::memory_order_relaxed)
        };
    }
    const auto after = macroAssignmentSequence.load(std::memory_order_acquire);
    if (before != after)
        return;

    for (auto& type : audioFxMacroOverrides)
        for (auto& slot : type)
            for (auto& parameter : slot)
                parameter = {};

    const auto x = macroXParameter->load(std::memory_order_relaxed);
    const auto y = macroYParameter->load(std::memory_order_relaxed);
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto& assignment = audioMacroAssignments[i];
        if (!assignment.active || assignment.type >= hypercolor::plugin::fxTypeCount
            || assignment.slot >= hypercolor::plugin::maximumFxInstancesPerType
            || assignment.parameterIndex >= 9
            || audioFxInstanceIdentities[assignment.type][assignment.slot]
                != assignment.instanceIdentity)
            continue;
        const auto macro = assignment.axis == hypercolor::plugin::MacroAxis::x ? x : y;
        auto mapped = hypercolor::plugin::mapMacroRange(
            macro, assignment.minimum, assignment.maximum);
        if (assignment.parameterInterval >= 1.0f)
            mapped = assignment.parameterStart
                + std::round((mapped - assignment.parameterStart)
                             / assignment.parameterInterval)
                    * assignment.parameterInterval;
        audioFxMacroOverrides[assignment.type][assignment.slot]
                             [assignment.parameterIndex] = { true, mapped };
    }
}

void HypercolorAudioProcessor::applyLfoConfigurationSnapshot() noexcept
{
    const auto before = lfoConfigurationSequence.load(std::memory_order_acquire);
    if ((before & 1u) != 0u || before == audioLfoSnapshotSequence)
        return;
    const auto previousModules = audioLfoModules;
    const auto previousOscillators = lfoOscillators;
    const auto moduleCount = lfoModuleCount.load(std::memory_order_relaxed);
    const auto assignmentCount = lfoAssignmentCount.load(std::memory_order_relaxed);
    if (moduleCount > hypercolor::plugin::maximumLfoModules
        || assignmentCount > hypercolor::plugin::maximumLfoAssignments)
        return;
    for (std::size_t i = 0; i < hypercolor::plugin::maximumLfoModules; ++i)
    {
        const auto& source = lfoRuntimeAtomic[i];
        const bool active = i < moduleCount;
        audioLfoModules[i] = {
            active ? source.identity.load(std::memory_order_relaxed) : 0,
            active && source.enabled.load(std::memory_order_relaxed),
            static_cast<hypercolor::dsp::modulation::LfoWaveform>(source.waveform.load(std::memory_order_relaxed)),
            static_cast<hypercolor::plugin::LfoRateMode>(source.rateMode.load(std::memory_order_relaxed)),
            static_cast<hypercolor::plugin::LfoDivision>(source.division.load(std::memory_order_relaxed)),
            source.rateHz.load(std::memory_order_relaxed), source.depth.load(std::memory_order_relaxed),
            source.phase.load(std::memory_order_relaxed), source.offset.load(std::memory_order_relaxed)
        };
        bool preserved = false;
        float preservedPhaseSetting = audioLfoModules[i].phase;
        for (std::size_t old = 0; old < previousModules.size(); ++old)
            if (active && previousModules[old].identity == audioLfoModules[i].identity)
            {
                lfoOscillators[i] = previousOscillators[old];
                preservedPhaseSetting = previousModules[old].phase;
                preserved = true;
                break;
            }
        if (!preserved)
        {
            const auto identity = audioLfoModules[i].identity;
            const auto seed = static_cast<std::uint32_t>(identity ^ (identity >> 32u));
            lfoOscillators[i].reset(seed);
            lfoOscillators[i].setPhase(audioLfoModules[i].phase);
        }
        else if (std::abs(preservedPhaseSetting - audioLfoModules[i].phase) > 1.0e-6f)
            lfoOscillators[i].setPhase(audioLfoModules[i].phase);
    }
    for (std::size_t i = 0; i < hypercolor::plugin::maximumLfoAssignments; ++i)
    {
        const auto& source = lfoAssignmentRuntimeAtomic[i];
        const bool active = i < assignmentCount && source.active.load(std::memory_order_relaxed);
        audioLfoAssignments[i] = {
            active,
            source.lfoIdentity.load(std::memory_order_relaxed),
            source.fxIdentity.load(std::memory_order_relaxed),
            source.type.load(std::memory_order_relaxed), source.slot.load(std::memory_order_relaxed),
            source.parameterIndex.load(std::memory_order_relaxed),
            source.lfoSlot.load(std::memory_order_relaxed),
            source.amount.load(std::memory_order_relaxed),
            source.bipolar.load(std::memory_order_relaxed), source.inverted.load(std::memory_order_relaxed),
            source.parameterStart.load(std::memory_order_relaxed), source.parameterEnd.load(std::memory_order_relaxed),
            source.parameterInterval.load(std::memory_order_relaxed), source.parameterSkew.load(std::memory_order_relaxed)
        };
    }
    if (before != lfoConfigurationSequence.load(std::memory_order_acquire))
        return;

    for (auto& type : audioFxLfoRanges)
        for (auto& slot : type)
            for (auto& parameter : slot)
                parameter = {};
    for (const auto& assignment : audioLfoAssignments)
    {
        if (!assignment.active || assignment.type >= hypercolor::plugin::fxTypeCount
            || assignment.slot >= hypercolor::plugin::maximumFxInstancesPerType
            || assignment.parameterIndex >= 9
            || audioFxInstanceIdentities[assignment.type][assignment.slot] != assignment.fxIdentity)
            continue;
        audioFxLfoRanges[assignment.type][assignment.slot][assignment.parameterIndex] = {
            assignment.parameterStart, assignment.parameterEnd,
            assignment.parameterSkew, assignment.parameterInterval
        };
    }
    audioLfoSnapshotSequence = before;
}

void HypercolorAudioProcessor::updateLfoOverrides(
    double bpm, std::optional<double> ppqPosition, int segmentSamples) noexcept
{
    if (segmentSamples <= 0)
        return;
    std::array<float, hypercolor::plugin::maximumLfoModules> average {};
    for (std::size_t i = 0; i < audioLfoModules.size(); ++i)
    {
        const auto& module = audioLfoModules[i];
        if (module.identity == 0 || !module.enabled)
            continue;
        const auto frequency = hypercolor::plugin::lfoFrequencyHz(
            module.rateMode, module.rateHz, module.division, bpm);
        lfoOscillators[i].setFrequency(frequency);
        if (ppqPosition.has_value() && module.rateMode == hypercolor::plugin::LfoRateMode::bpmSync)
        {
            const auto cycles = *ppqPosition
                * hypercolor::plugin::lfoDivisionCyclesPerQuarter[
                    static_cast<std::size_t>(module.division)];
            lfoOscillators[i].setPhase(cycles + module.phase);
        }
        double total = 0.0;
        for (int sample = 0; sample < segmentSamples; ++sample)
            total += lfoOscillators[i].nextSample(module.waveform);
        average[i] = static_cast<float>(total / static_cast<double>(segmentSamples));
    }

    for (auto& type : audioFxLfoOverrides)
        for (auto& slot : type)
            for (auto& parameter : slot)
                parameter = {};

    std::array<std::array<std::array<float, 9>, hypercolor::plugin::maximumFxInstancesPerType>,
               hypercolor::plugin::fxTypeCount> sums {};
    std::array<std::array<std::array<bool, 9>, hypercolor::plugin::maximumFxInstancesPerType>,
               hypercolor::plugin::fxTypeCount> affected {};
    for (const auto& assignment : audioLfoAssignments)
    {
        if (!assignment.active || assignment.type >= hypercolor::plugin::fxTypeCount
            || assignment.slot >= hypercolor::plugin::maximumFxInstancesPerType
            || assignment.parameterIndex >= 9
            || audioFxInstanceIdentities[assignment.type][assignment.slot] != assignment.fxIdentity)
            continue;
        const auto moduleIndex = static_cast<std::size_t>(assignment.lfoSlot);
        if (moduleIndex >= audioLfoModules.size()
            || audioLfoModules[moduleIndex].identity != assignment.lfoIdentity
            || !audioLfoModules[moduleIndex].enabled)
            continue;
        const auto& module = audioLfoModules[moduleIndex];
        auto signal = assignment.bipolar
            ? average[moduleIndex] * module.depth + module.offset
            : (average[moduleIndex] + 1.0f) * 0.5f * module.depth + module.offset;
        signal = assignment.bipolar
            ? juce::jlimit(-1.0f, 1.0f, signal)
            : juce::jlimit(0.0f, 1.0f, signal);
        if (assignment.inverted)
            signal = assignment.bipolar ? -signal : 1.0f - signal;
        sums[assignment.type][assignment.slot][assignment.parameterIndex]
            += signal * assignment.amount;
        affected[assignment.type][assignment.slot][assignment.parameterIndex] = true;
    }
    for (std::size_t type = 0; type < hypercolor::plugin::fxTypeCount; ++type)
        for (std::size_t slot = 0; slot < hypercolor::plugin::maximumFxInstancesPerType; ++slot)
            for (std::size_t parameter = 0; parameter < 9; ++parameter)
            {
                if (!affected[type][slot][parameter])
                    continue;
                const auto& range = audioFxLfoRanges[type][slot][parameter];
                const auto start = range.start;
                const auto end = range.end;
                const auto skew = range.skew;
                const auto interval = range.interval;
                const auto base = readFxBaseParameterValue(type, slot, parameter);
                const auto proportion = juce::jlimit(0.0f, 1.0f, (base - start) / (end - start));
                const auto normalized = skew > 0.0f && std::abs(skew - 1.0f) > 1.0e-6f
                    ? std::pow(proportion, skew) : proportion;
                const auto modulated = juce::jlimit(0.0f, 1.0f,
                    normalized + sums[type][slot][parameter]);
                auto raw = start + (end - start) * (skew > 0.0f
                    ? std::pow(modulated, 1.0f / skew) : modulated);
                if (interval >= 1.0f)
                    raw = start + std::round((raw - start) / interval) * interval;
                audioFxLfoOverrides[type][slot][parameter] = { true, juce::jlimit(start, end, raw) };
            }
}

float HypercolorAudioProcessor::readFxParameterValue(
    std::size_t type,
    std::size_t slot,
    std::size_t parameterIndex,
    float fallback
) const noexcept
{
    if (type >= hypercolor::plugin::fxTypeCount
        || slot >= hypercolor::plugin::maximumFxInstancesPerType
        || parameterIndex >= 9)
        return fallback;
    const auto lfoOverride = audioFxLfoOverrides[type][slot][parameterIndex];
    if (lfoOverride.active)
        return lfoOverride.value;
    return readFxBaseParameterValue(type, slot, parameterIndex, fallback);
}

float HypercolorAudioProcessor::readFxBaseParameterValue(
    std::size_t type,
    std::size_t slot,
    std::size_t parameterIndex,
    float fallback
) const noexcept
{
    if (type >= hypercolor::plugin::fxTypeCount
        || slot >= hypercolor::plugin::maximumFxInstancesPerType
        || parameterIndex >= 9)
        return fallback;
    const auto override = audioFxMacroOverrides[type][slot][parameterIndex];
    if (override.active)
        return override.value;
    const auto* parameter = fxRawParameters[type][slot].values[parameterIndex];
    return parameter != nullptr
        ? parameter->load(std::memory_order_relaxed)
        : fallback;
}

juce::AudioProcessorValueTreeState&
HypercolorAudioProcessor::getValueTreeState() noexcept
{
    return parameters;
}

const juce::AudioProcessorValueTreeState&
HypercolorAudioProcessor::getValueTreeState() const noexcept
{
    return parameters;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HypercolorAudioProcessor();
}
