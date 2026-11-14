#include "PluginEditor.h"

HypercolorAudioProcessorEditor::HypercolorAudioProcessorEditor(
    HypercolorAudioProcessor& processor
)
    : AudioProcessorEditor(&processor),
      audioProcessor(processor)
{
    setSize(640, 420);
}

void HypercolorAudioProcessorEditor::paint(juce::Graphics& graphics)
{
    juce::ignoreUnused(audioProcessor);

    graphics.fillAll(juce::Colour::fromRGB(13, 15, 24));

    graphics.setColour(juce::Colour::fromRGB(115, 230, 210));

    graphics.setFont(juce::FontOptions(32.0f));

    graphics.drawFittedText(
        "HYPERCOLOR",
        getLocalBounds().reduced(24),
        juce::Justification::centred,
        1
    );

    graphics.setColour(juce::Colour::fromRGB(145, 150, 170));

    graphics.setFont(juce::FontOptions(12.0f));

    graphics.drawText(
        "GRANULAR CREATIVE EFFECT",
        getLocalBounds().removeFromBottom(70),
        juce::Justification::centred
    );
}

void HypercolorAudioProcessorEditor::resized()
{
}
