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
    using ButtonAttachment =
        juce::AudioProcessorValueTreeState::ButtonAttachment;

    HypercolorAudioProcessor& audioProcessor;

    juce::Slider dryWetSlider;
    juce::Slider outputGainSlider;
    juce::Slider densitySlider;
    juce::Slider sizeSlider;
    juce::Slider positionSlider;
    juce::Slider pitchSlider;
    juce::TextButton reverseButton;

    juce::Label dryWetLabel;
    juce::Label outputGainLabel;
    juce::Label densityLabel;
    juce::Label sizeLabel;
    juce::Label positionLabel;
    juce::Label pitchLabel;
    juce::Label reverseLabel;
    juce::Label positionHintLabel;
    juce::Label granularSectionLabel;
    juce::Label masterSectionLabel;

    SliderAttachment dryWetAttachment;
    SliderAttachment outputGainAttachment;
    SliderAttachment densityAttachment;
    SliderAttachment sizeAttachment;
    SliderAttachment positionAttachment;
    SliderAttachment pitchAttachment;
    ButtonAttachment reverseAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(
        HypercolorAudioProcessorEditor
    )
};
