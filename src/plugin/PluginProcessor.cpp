#include "PluginProcessor.h"
#include "PluginEditor.h"

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
    )
{
}

void HypercolorAudioProcessor::prepareToPlay(
    double sampleRate,
    int samplesPerBlock
)
{
    juce::ignoreUnused(sampleRate, samplesPerBlock);
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

    // Passthrough: input audio remains unchanged.
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
}

void HypercolorAudioProcessor::setStateInformation(
    const void* data,
    int sizeInBytes
)
{
    juce::ignoreUnused(data, sizeInBytes);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HypercolorAudioProcessor();
}
