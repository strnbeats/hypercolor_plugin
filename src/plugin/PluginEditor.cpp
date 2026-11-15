#include "PluginEditor.h"

HypercolorAudioProcessorEditor::HypercolorAudioProcessorEditor(
    HypercolorAudioProcessor& processorToEdit
)
    : AudioProcessorEditor(&processorToEdit),
      audioProcessor(processorToEdit),
      dryWetAttachment(
          processorToEdit.getValueTreeState(),
          hypercolor::ParameterIDs::dryWet,
          dryWetSlider
      ),
      outputGainAttachment(
          processorToEdit.getValueTreeState(),
          hypercolor::ParameterIDs::outputGain,
          outputGainSlider
      )
{
    const auto configureSlider = [this](
        juce::Slider& slider,
        juce::String suffix
    )
    {
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 90, 24);
        slider.setTextValueSuffix(std::move(suffix));
        addAndMakeVisible(slider);
    };

    configureSlider(dryWetSlider, " %");
    configureSlider(outputGainSlider, " dB");

    dryWetLabel.setText("DRY / WET", juce::dontSendNotification);
    outputGainLabel.setText("OUTPUT GAIN", juce::dontSendNotification);

    for (auto* label : { &dryWetLabel, &outputGainLabel })
    {
        label->setJustificationType(juce::Justification::centred);
        label->setColour(
            juce::Label::textColourId,
            juce::Colour::fromRGB(180, 186, 205)
        );
        addAndMakeVisible(*label);
    }

    setResizable(true, true);
    setResizeLimits(480, 320, 960, 630);
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
        getLocalBounds().removeFromTop(100).reduced(24),
        juce::Justification::centred,
        1
    );

    graphics.setColour(juce::Colour::fromRGB(145, 150, 170));

    graphics.setFont(juce::FontOptions(12.0f));

    graphics.drawText(
        "GRANULAR CREATIVE EFFECT",
        getLocalBounds().removeFromBottom(50),
        juce::Justification::centred
    );
}

void HypercolorAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced(40);
    bounds.removeFromTop(100);
    bounds.removeFromBottom(40);

    auto controls = bounds.withSizeKeepingCentre(
        juce::jmin(360, bounds.getWidth()),
        juce::jmin(210, bounds.getHeight())
    );
    const auto controlWidth = controls.getWidth() / 2;

    auto dryWetBounds = controls.removeFromLeft(controlWidth).reduced(12);
    auto outputGainBounds = controls.reduced(12);

    dryWetLabel.setBounds(dryWetBounds.removeFromTop(28));
    dryWetSlider.setBounds(dryWetBounds);

    outputGainLabel.setBounds(outputGainBounds.removeFromTop(28));
    outputGainSlider.setBounds(outputGainBounds);
}
