#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace
{
constexpr auto stateTreeType = "HYPERCOLOR_STATE";
constexpr auto stateSchemaProperty = "schemaVersion";
constexpr int currentStateSchemaVersion = 1;
// Large enough to leave room for future bounded preset/state data while still
// rejecting clearly unreasonable input before parsing it as XML.
constexpr int maxStateSizeBytes = 64 * 1024 * 1024;
constexpr double parameterSmoothingSeconds = 0.02;
constexpr double telemetryUpdatesPerSecond = 30.0;

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
    (void) effectChain.addModule(&spaceReverb);
    (void) effectChain.addModule(&modulationEffect);
    (void) effectChain.addModule(&driveEffect);
    (void) effectChain.addModule(&tapeEffect);
    (void) effectChain.addModule(&bitcrushEffect);
    (void) effectChain.addModule(&filterEffect);
    (void) effectChain.addModule(&delayEffect);
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
    driveModeParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::driveMode);
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
    tapeDriveParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::tapeDrive);
    tapeWowParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::tapeWow);
    tapeFlutterParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::tapeFlutter);
    tapeWearParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::tapeWear);
    tapeMixParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::tapeMix);
    tapeOutputParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::tapeOutput);
    bitcrushBitsParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::bitcrushBits);
    bitcrushRateParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::bitcrushRate);
    bitcrushModeParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::bitcrushMode);
    bitcrushMixParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::bitcrushMix);
    bitcrushOutputParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::bitcrushOutput);
    filterModeParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::filterMode);
    filterCutoffParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::filterCutoff);
    filterResonanceParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::filterResonance);
    filterDriveParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::filterDrive);
    filterMixParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::filterMix);
    filterOutputParameter = parameters.getRawParameterValue(hypercolor::ParameterIDs::filterOutput);

    jassert(dryWetParameter != nullptr);
    jassert(outputGainParameter != nullptr);
    jassert(grainDensityParameter != nullptr);
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
    jassert(driveModeParameter != nullptr);
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
    jassert(tapeDriveParameter != nullptr);
    jassert(tapeWowParameter != nullptr);
    jassert(tapeFlutterParameter != nullptr);
    jassert(tapeWearParameter != nullptr);
    jassert(tapeMixParameter != nullptr);
    jassert(tapeOutputParameter != nullptr);
    jassert(bitcrushBitsParameter != nullptr);
    jassert(bitcrushRateParameter != nullptr);
    jassert(bitcrushModeParameter != nullptr);
    jassert(bitcrushMixParameter != nullptr);
    jassert(bitcrushOutputParameter != nullptr);
    jassert(filterModeParameter != nullptr);
    jassert(filterCutoffParameter != nullptr);
    jassert(filterResonanceParameter != nullptr);
    jassert(filterDriveParameter != nullptr);
    jassert(filterMixParameter != nullptr);
    jassert(filterOutputParameter != nullptr);

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

    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::reverbMix, 1 }, "Reverb Mix", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 30.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::reverbSize, 1 }, "Reverb Size", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 50.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::reverbDecay, 1 }, "Reverb Decay", juce::NormalisableRange<float> { 0.1f, 20.0f, 0.01f }, 4.0f, juce::AudioParameterFloatAttributes().withLabel("s")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::reverbDamping, 1 }, "Reverb Damping", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 40.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::reverbPreDelay, 1 }, "Reverb Pre-Delay", juce::NormalisableRange<float> { 0.0f, 200.0f, 0.1f }, 20.0f, juce::AudioParameterFloatAttributes().withLabel("ms")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::reverbWidth, 1 }, "Reverb Width", juce::NormalisableRange<float> { 0.0f, 200.0f, 0.1f }, 100.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { hypercolor::ParameterIDs::modulationMode, 1 }, "Modulation Mode", juce::StringArray { "Chorus", "Flanger" }, 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::modulationRate, 1 }, "Modulation Rate", juce::NormalisableRange<float> { 0.01f, 10.0f, 0.01f }, 0.3f, juce::AudioParameterFloatAttributes().withLabel("Hz")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::modulationDepth, 1 }, "Modulation Depth", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 50.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::modulationBaseDelay, 1 }, "Modulation Base Delay", juce::NormalisableRange<float> { 0.1f, 20.0f, 0.01f }, 7.0f, juce::AudioParameterFloatAttributes().withLabel("ms")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::modulationFeedback, 1 }, "Modulation Feedback", juce::NormalisableRange<float> { -95.0f, 95.0f, 0.1f }, 0.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::modulationMix, 1 }, "Modulation Mix", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 30.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::modulationWidth, 1 }, "Modulation Width", juce::NormalisableRange<float> { 0.0f, 200.0f, 0.1f }, 100.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { hypercolor::ParameterIDs::driveMode, 1 }, "Drive Mode", juce::StringArray { "Soft Clip", "Hard Clip", "Tube" }, 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::driveAmount, 1 }, "Drive", juce::NormalisableRange<float> { 0.0f, 36.0f, 0.1f }, 0.0f, juce::AudioParameterFloatAttributes().withLabel("dB")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::driveTone, 1 }, "Drive Tone", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 50.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::driveMix, 1 }, "Drive Mix", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 100.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::driveOutput, 1 }, "Drive Output", juce::NormalisableRange<float> { -24.0f, 12.0f, 0.1f }, 0.0f, juce::AudioParameterFloatAttributes().withLabel("dB")));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { hypercolor::ParameterIDs::delayMode, 1 }, "Delay Mode", juce::StringArray { "Stereo", "Ping-Pong" }, 0));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { hypercolor::ParameterIDs::delaySync, 1 }, "Delay Sync", false));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::delayTime, 1 }, "Delay Time", juce::NormalisableRange<float> { 1.0f, 2000.0f, 0.1f }, 350.0f, juce::AudioParameterFloatAttributes().withLabel("ms")));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { hypercolor::ParameterIDs::delayDivision, 1 }, "Delay Division", juce::StringArray { "1/1", "1/2", "1/4", "1/8", "1/16", "1/4 dotted", "1/8 dotted", "1/4 triplet", "1/8 triplet" }, 2));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::delayFeedback, 1 }, "Delay Feedback", juce::NormalisableRange<float> { 0.0f, 95.0f, 0.1f }, 35.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::delayLowCut, 1 }, "Delay Low Cut", juce::NormalisableRange<float> { 20.0f, 2000.0f, 1.0f }, 100.0f, juce::AudioParameterFloatAttributes().withLabel("Hz")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::delayHighCut, 1 }, "Delay High Cut", juce::NormalisableRange<float> { 1000.0f, 20000.0f, 1.0f }, 12000.0f, juce::AudioParameterFloatAttributes().withLabel("Hz")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::delayMix, 1 }, "Delay Mix", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 30.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::delayWidth, 1 }, "Delay Width", juce::NormalisableRange<float> { 0.0f, 200.0f, 0.1f }, 100.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::tapeDrive, 1 }, "Tape Drive", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 35.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::tapeWow, 1 }, "Tape Wow", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 15.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::tapeFlutter, 1 }, "Tape Flutter", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 10.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::tapeWear, 1 }, "Tape Wear", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 15.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::tapeMix, 1 }, "Tape Mix", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 100.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::tapeOutput, 1 }, "Tape Output", juce::NormalisableRange<float> { -24.0f, 12.0f, 0.1f }, 0.0f, juce::AudioParameterFloatAttributes().withLabel("dB")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::bitcrushBits, 1 }, "Bitcrush Bits", juce::NormalisableRange<float> { 4.0f, 24.0f, 1.0f }, 16.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::bitcrushRate, 1 }, "Bitcrush Rate", juce::NormalisableRange<float> { 500.0f, 44100.0f, 1.0f }, 22050.0f, juce::AudioParameterFloatAttributes().withLabel("Hz")));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { hypercolor::ParameterIDs::bitcrushMode, 1 }, "Bitcrush Mode", juce::StringArray { "Round", "Truncate" }, 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::bitcrushMix, 1 }, "Bitcrush Mix", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 100.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::bitcrushOutput, 1 }, "Bitcrush Output", juce::NormalisableRange<float> { -24.0f, 12.0f, 0.1f }, 0.0f, juce::AudioParameterFloatAttributes().withLabel("dB")));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { hypercolor::ParameterIDs::filterMode, 1 }, "Filter Mode", juce::StringArray { "Low-Pass", "High-Pass", "Band-Pass", "Notch" }, 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::filterCutoff, 1 }, "Filter Cutoff", juce::NormalisableRange<float> { 20.0f, 20000.0f, 1.0f, 0.35f }, 10000.0f, juce::AudioParameterFloatAttributes().withLabel("Hz")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::filterResonance, 1 }, "Filter Resonance", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 20.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::filterDrive, 1 }, "Filter Drive", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 0.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::filterMix, 1 }, "Filter Mix", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 100.0f, percentAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { hypercolor::ParameterIDs::filterOutput, 1 }, "Filter Output", juce::NormalisableRange<float> { -24.0f, 12.0f, 0.1f }, 0.0f, juce::AudioParameterFloatAttributes().withLabel("dB")));

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
    const auto granularParameters = readGranularParameters();
    spaceReverb.setParameters(readReverbParameters());
    modulationEffect.setParameters({
        modulationModeParameter->load(std::memory_order_relaxed) >= 0.5f
            ? hypercolor::dsp::fx::ModulationMode::flanger
            : hypercolor::dsp::fx::ModulationMode::chorus,
        modulationRateParameter->load(std::memory_order_relaxed),
        modulationDepthParameter->load(std::memory_order_relaxed) / 100.0f,
        modulationBaseDelayParameter->load(std::memory_order_relaxed),
        modulationFeedbackParameter->load(std::memory_order_relaxed) / 100.0f,
        modulationMixParameter->load(std::memory_order_relaxed) / 100.0f,
        modulationWidthParameter->load(std::memory_order_relaxed) / 100.0f
    });
    driveEffect.setParameters({
        driveModeParameter->load(std::memory_order_relaxed) >= 1.5f
            ? hypercolor::dsp::fx::DriveMode::tube
            : (driveModeParameter->load(std::memory_order_relaxed) >= 0.5f
                ? hypercolor::dsp::fx::DriveMode::hardClip
                : hypercolor::dsp::fx::DriveMode::softClip),
        driveAmountParameter->load(std::memory_order_relaxed),
        driveToneParameter->load(std::memory_order_relaxed) / 100.0f,
        driveMixParameter->load(std::memory_order_relaxed) / 100.0f,
        driveOutputParameter->load(std::memory_order_relaxed)
    });
    double bpm = 120.0;
    if (const auto* playHead = getPlayHead())
    {
        if (const auto position = playHead->getPosition())
            if (position->getBpm().hasValue())
                bpm = *position->getBpm();
    }
    delayEffect.setParameters({
        delayModeParameter->load(std::memory_order_relaxed) >= 0.5f ? hypercolor::dsp::fx::DelayMode::pingPong : hypercolor::dsp::fx::DelayMode::stereo,
        delaySyncParameter->load(std::memory_order_relaxed) >= 0.5f,
        delayTimeParameter->load(std::memory_order_relaxed),
        static_cast<hypercolor::dsp::fx::DelayDivision>(std::clamp(delayDivisionParameter->load(std::memory_order_relaxed), 0.0f, 8.0f)),
        delayFeedbackParameter->load(std::memory_order_relaxed) / 100.0f,
        delayLowCutParameter->load(std::memory_order_relaxed),
        delayHighCutParameter->load(std::memory_order_relaxed),
        delayMixParameter->load(std::memory_order_relaxed) / 100.0f,
        delayWidthParameter->load(std::memory_order_relaxed) / 100.0f,
        static_cast<float>(bpm)
    });
    tapeEffect.setParameters({
        tapeDriveParameter->load(std::memory_order_relaxed) / 100.0f,
        tapeWowParameter->load(std::memory_order_relaxed) / 100.0f,
        tapeFlutterParameter->load(std::memory_order_relaxed) / 100.0f,
        tapeWearParameter->load(std::memory_order_relaxed) / 100.0f,
        tapeMixParameter->load(std::memory_order_relaxed) / 100.0f,
        tapeOutputParameter->load(std::memory_order_relaxed)
    });
    bitcrushEffect.setParameters({
        bitcrushBitsParameter->load(std::memory_order_relaxed),
        bitcrushRateParameter->load(std::memory_order_relaxed),
        bitcrushModeParameter->load(std::memory_order_relaxed) >= 0.5f ? hypercolor::dsp::fx::BitcrushMode::truncate : hypercolor::dsp::fx::BitcrushMode::round,
        bitcrushMixParameter->load(std::memory_order_relaxed) / 100.0f,
        bitcrushOutputParameter->load(std::memory_order_relaxed)
    });
    filterEffect.setParameters({
        static_cast<hypercolor::dsp::fx::FilterMode>(std::clamp(filterModeParameter->load(std::memory_order_relaxed), 0.0f, 3.0f)),
        filterCutoffParameter->load(std::memory_order_relaxed),
        filterResonanceParameter->load(std::memory_order_relaxed) / 100.0f,
        filterDriveParameter->load(std::memory_order_relaxed) / 100.0f,
        filterMixParameter->load(std::memory_order_relaxed) / 100.0f,
        filterOutputParameter->load(std::memory_order_relaxed)
    });

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

hypercolor::dsp::granular::GranularEngineParameters
HypercolorAudioProcessor::readGranularParameters() const noexcept
{
    return {
        static_cast<double>(
            grainDensityParameter->load(std::memory_order_relaxed)
        ),
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
}

hypercolor::dsp::fx::SpaceReverbParameters
HypercolorAudioProcessor::readReverbParameters() const noexcept
{
    return {
        reverbMixParameter->load(std::memory_order_relaxed) / 100.0f,
        reverbSizeParameter->load(std::memory_order_relaxed) / 100.0f,
        reverbDecayParameter->load(std::memory_order_relaxed),
        reverbDampingParameter->load(std::memory_order_relaxed) / 100.0f,
        reverbPreDelayParameter->load(std::memory_order_relaxed),
        reverbWidthParameter->load(std::memory_order_relaxed) / 100.0f
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
    const auto effectResult = effectChain.process(effectBlock);
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

    const auto parameterTemplateState = parameters.copyState();

    if (!addDefaultParameterIfMissing(
            restoredState,
            parameterTemplateState,
            hypercolor::ParameterIDs::grainDensity,
            defaultGranularParameters.densityGrainsPerSecond
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
        )
    {
        return;
    }

    parameters.replaceState(restoredState);
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
