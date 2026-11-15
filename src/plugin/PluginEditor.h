#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class HypercolorAudioProcessorEditor final
    : public juce::AudioProcessorEditor
{
public:
    explicit HypercolorAudioProcessorEditor(
        HypercolorAudioProcessor& processorToEdit
    );

    void paint(juce::Graphics& graphics) override;
    void resized() override;

private:
    using SliderAttachment =
        juce::AudioProcessorValueTreeState::SliderAttachment;

    HypercolorAudioProcessor& audioProcessor;

    juce::Slider dryWetSlider;
    juce::Slider outputGainSlider;
    juce::Label dryWetLabel;
    juce::Label outputGainLabel;

    SliderAttachment dryWetAttachment;
    SliderAttachment outputGainAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(
        HypercolorAudioProcessorEditor
    )
};
