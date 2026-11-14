#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class HypercolorAudioProcessorEditor final
    : public juce::AudioProcessorEditor
{
public:
    explicit HypercolorAudioProcessorEditor(
        HypercolorAudioProcessor& processor
    );

    void paint(juce::Graphics& graphics) override;
    void resized() override;

private:
    HypercolorAudioProcessor& audioProcessor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(
        HypercolorAudioProcessorEditor
    )
};
