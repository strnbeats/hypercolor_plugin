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
        ) / 100.0
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
    const auto useDryFallback = granularResult
            == hypercolor::dsp::granular::GranularEngineProcessResult::engineNotPrepared
        || granularResult
            == hypercolor::dsp::granular::GranularEngineProcessResult::invalidConfiguration;

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
            const auto rawWetSample = useDryFallback
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
