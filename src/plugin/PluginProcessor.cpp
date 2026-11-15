#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cerrno>
#include <cmath>
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

constexpr float dryWetMinimum = 0.0f;
constexpr float dryWetMaximum = 100.0f;
constexpr float dryWetDefault = 100.0f;

constexpr float outputGainMinimumDb = -24.0f;
constexpr float outputGainMaximumDb = 12.0f;
constexpr float outputGainDefaultDb = 0.0f;

bool readFiniteNumber(const juce::var& value, double& result)
{
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

    jassert(dryWetParameter != nullptr);
    jassert(outputGainParameter != nullptr);

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

    return layout;
}

void HypercolorAudioProcessor::prepareToPlay(
    double sampleRate,
    int samplesPerBlock
)
{
    const auto safeSampleRate = juce::jmax(1.0, sampleRate);
    const auto scratchBlockSize = juce::jmax(1, samplesPerBlock);

    dryBuffer.setSize(2, scratchBlockSize, false, false, true);

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
         startSample += scratchBlockSize)
    {
        processChunk(
            buffer,
            startSample,
            juce::jmin(
                scratchBlockSize,
                buffer.getNumSamples() - startSample
            ),
            numChannels
        );
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

void HypercolorAudioProcessor::processChunk(
    juce::AudioBuffer<float>& buffer,
    int startSample,
    int numSamples,
    int numChannels
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

    // Future wet DSP will process this region of `buffer` here. Keeping the
    // dry copy separate now makes the global mix path stable from Phase 1A.

    for (int sample = 0; sample < numSamples; ++sample)
    {
        const auto wet = dryWetSmoother.getNextValue();
        const auto outputGain = outputGainSmoother.getNextValue();

        for (int channel = 0; channel < numChannels; ++channel)
        {
            const auto drySample = dryBuffer.getSample(channel, sample);
            const auto wetSample = buffer.getSample(
                channel,
                startSample + sample
            );
            const auto mixedSample = drySample
                + wet * (wetSample - drySample);

            buffer.setSample(
                channel,
                startSample + sample,
                mixedSample * outputGain
            );
        }
    }
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

    const auto restoredState = juce::ValueTree::fromXml(*xml);

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
        ))
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
