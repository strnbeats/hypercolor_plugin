#include "PluginEditor.h"

#include <array>

HypercolorAudioProcessorEditor::HypercolorAudioProcessorEditor(
    HypercolorAudioProcessor& processorToEdit
)
    : AudioProcessorEditor(&processorToEdit),
      audioProcessor(processorToEdit),
      granularVisualizer(processorToEdit),
      dryWetAttachment(
          processorToEdit.getValueTreeState(),
          hypercolor::ParameterIDs::dryWet,
          dryWetSlider
      ),
      outputGainAttachment(
          processorToEdit.getValueTreeState(),
          hypercolor::ParameterIDs::outputGain,
          outputGainSlider
      ),
      densityAttachment(
          processorToEdit.getValueTreeState(),
          hypercolor::ParameterIDs::grainDensity,
          densitySlider
      ),
      sizeAttachment(
          processorToEdit.getValueTreeState(),
          hypercolor::ParameterIDs::grainSize,
          sizeSlider
      ),
      positionAttachment(
          processorToEdit.getValueTreeState(),
          hypercolor::ParameterIDs::grainPosition,
          positionSlider
      ),
      pitchAttachment(
          processorToEdit.getValueTreeState(),
          hypercolor::ParameterIDs::grainPitch,
          pitchSlider
      ),
      reverseAttachment(
          processorToEdit.getValueTreeState(),
          hypercolor::ParameterIDs::grainReverse,
          reverseButton
      )
{
    const auto accent = juce::Colour::fromRGB(115, 230, 210);
    const auto accentSecondary = juce::Colour::fromRGB(230, 115, 198);
    const auto controlBackground = juce::Colour::fromRGB(27, 31, 46);
    const auto controlOutline = juce::Colour::fromRGB(63, 70, 94);
    const auto text = juce::Colour::fromRGB(205, 210, 226);

    addAndMakeVisible(granularVisualizer);

    const auto configureSlider = [
        this,
        accent,
        accentSecondary,
        controlBackground,
        controlOutline,
        text
    ](
        juce::Slider& slider,
        juce::String suffix
    )
    {
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 90, 24);
        slider.setTextValueSuffix(std::move(suffix));
        slider.setColour(juce::Slider::rotarySliderFillColourId, accent);
        slider.setColour(
            juce::Slider::rotarySliderOutlineColourId,
            controlOutline
        );
        slider.setColour(juce::Slider::thumbColourId, accentSecondary);
        slider.setColour(
            juce::Slider::textBoxBackgroundColourId,
            controlBackground
        );
        slider.setColour(juce::Slider::textBoxTextColourId, text);
        slider.setColour(
            juce::Slider::textBoxOutlineColourId,
            controlOutline
        );
        addAndMakeVisible(slider);
    };

    configureSlider(dryWetSlider, " %");
    configureSlider(outputGainSlider, " dB");
    configureSlider(densitySlider, " gr/s");
    configureSlider(sizeSlider, " ms");
    configureSlider(positionSlider, {});
    configureSlider(pitchSlider, {});

    positionSlider.textFromValueFunction = [](double value)
    {
        return juce::String(value * 100.0, 1) + " %";
    };
    positionSlider.valueFromTextFunction = [](const juce::String& textValue)
    {
        return textValue.getDoubleValue() / 100.0;
    };

    pitchSlider.textFromValueFunction = [](double value)
    {
        const auto prefix = value > 0.0 ? "+" : "";
        return juce::String(prefix) + juce::String(value, 2) + " st";
    };
    pitchSlider.valueFromTextFunction = [](const juce::String& textValue)
    {
        return textValue.getDoubleValue();
    };

    dryWetSlider.setComponentID(hypercolor::ParameterIDs::dryWet);
    outputGainSlider.setComponentID(hypercolor::ParameterIDs::outputGain);
    densitySlider.setComponentID(hypercolor::ParameterIDs::grainDensity);
    sizeSlider.setComponentID(hypercolor::ParameterIDs::grainSize);
    positionSlider.setComponentID(hypercolor::ParameterIDs::grainPosition);
    pitchSlider.setComponentID(hypercolor::ParameterIDs::grainPitch);
    reverseButton.setComponentID(hypercolor::ParameterIDs::grainReverse);

    dryWetLabel.setText("DRY / WET", juce::dontSendNotification);
    outputGainLabel.setText("OUTPUT GAIN", juce::dontSendNotification);
    densityLabel.setText("DENSITY", juce::dontSendNotification);
    sizeLabel.setText("SIZE", juce::dontSendNotification);
    positionLabel.setText("POSITION", juce::dontSendNotification);
    pitchLabel.setText("PITCH", juce::dontSendNotification);
    reverseLabel.setText("DIRECTION", juce::dontSendNotification);

    for (auto* label : {
             &dryWetLabel,
             &outputGainLabel,
             &densityLabel,
             &sizeLabel,
             &positionLabel,
             &pitchLabel,
             &reverseLabel })
    {
        label->setJustificationType(juce::Justification::centred);
        label->setColour(juce::Label::textColourId, text);
        addAndMakeVisible(*label);
    }

    positionHintLabel.setText(
        "0% FRESH  •  100% OLD",
        juce::dontSendNotification
    );
    positionHintLabel.setJustificationType(juce::Justification::centred);
    positionHintLabel.setColour(
        juce::Label::textColourId,
        juce::Colour::fromRGB(132, 140, 164)
    );
    positionHintLabel.setFont(juce::FontOptions(10.0f));
    addAndMakeVisible(positionHintLabel);

    granularSectionLabel.setText("GRANULAR", juce::dontSendNotification);
    masterSectionLabel.setText("MASTER", juce::dontSendNotification);

    for (auto* label : { &granularSectionLabel, &masterSectionLabel })
    {
        label->setJustificationType(juce::Justification::centredLeft);
        label->setColour(juce::Label::textColourId, accent);
        label->setFont(juce::FontOptions(12.0f).withStyle("Bold"));
        addAndMakeVisible(*label);
    }

    reverseButton.setClickingTogglesState(true);
    reverseButton.setColour(
        juce::TextButton::buttonColourId,
        controlBackground
    );
    reverseButton.setColour(
        juce::TextButton::buttonOnColourId,
        accent
    );
    reverseButton.setColour(juce::TextButton::textColourOffId, text);
    reverseButton.setColour(
        juce::TextButton::textColourOnId,
        juce::Colour::fromRGB(13, 15, 24)
    );
    reverseButton.onStateChange = [this]
    {
        reverseButton.setButtonText(
            reverseButton.getToggleState() ? "REVERSE: ON" : "REVERSE: OFF"
        );
    };
    reverseButton.onStateChange();
    addAndMakeVisible(reverseButton);

    setResizable(true, true);
    setResizeLimits(600, 440, 960, 630);
    setSize(760, 500);
}

void HypercolorAudioProcessorEditor::paint(juce::Graphics& graphics)
{
    juce::ignoreUnused(audioProcessor);

    graphics.fillAll(juce::Colour::fromRGB(13, 15, 24));

    graphics.setColour(juce::Colour::fromRGB(115, 230, 210));

    graphics.setFont(juce::FontOptions(32.0f));

    graphics.drawFittedText(
        "HYPERCOLOR",
        getLocalBounds().removeFromTop(76).reduced(24),
        juce::Justification::centred,
        1
    );

    graphics.setColour(juce::Colour::fromRGB(145, 150, 170));

    graphics.setFont(juce::FontOptions(12.0f));

    graphics.drawText(
        "GRANULAR CREATIVE EFFECT",
        getLocalBounds().removeFromBottom(30),
        juce::Justification::centred
    );
}

void HypercolorAudioProcessorEditor::resized()
{
    const auto margin = juce::jlimit(14, 32, getWidth() / 24);
    auto content = getLocalBounds().reduced(margin);
    content.removeFromTop(66);
    content.removeFromBottom(22);

    const auto visualizerHeight = juce::jlimit(
        70,
        160,
        juce::roundToInt(
            static_cast<float>(content.getHeight()) * 0.32f
        )
    );
    granularVisualizer.setBounds(
        content.removeFromTop(visualizerHeight).reduced(0, 3)
    );
    content.removeFromTop(4);

    auto granularArea = content.removeFromTop(
        juce::roundToInt(static_cast<float>(content.getHeight()) * 0.57f)
    );
    auto masterArea = content;

    granularSectionLabel.setBounds(granularArea.removeFromTop(20));
    masterSectionLabel.setBounds(masterArea.removeFromTop(20));

    const auto granularControlWidth = granularArea.getWidth() / 5;
    std::array<juce::Component*, 5> granularControls {
        &densitySlider,
        &sizeSlider,
        &positionSlider,
        &pitchSlider,
        &reverseButton
    };
    std::array<juce::Label*, 5> granularLabels {
        &densityLabel,
        &sizeLabel,
        &positionLabel,
        &pitchLabel,
        &reverseLabel
    };

    for (std::size_t index = 0; index < granularControls.size(); ++index)
    {
        auto cell = granularArea.removeFromLeft(
            index + 1 == granularControls.size()
                ? granularArea.getWidth()
                : granularControlWidth
        ).reduced(4);
        granularLabels[index]->setBounds(cell.removeFromTop(20));

        if (granularControls[index] == &positionSlider)
            positionHintLabel.setBounds(cell.removeFromBottom(16));

        if (granularControls[index] == &reverseButton)
        {
            granularControls[index]->setBounds(
                cell.withSizeKeepingCentre(
                    juce::jmax(70, cell.getWidth() - 12),
                    juce::jmin(42, cell.getHeight())
                )
            );
        }
        else
        {
            granularControls[index]->setBounds(cell);
        }
    }

    auto masterControls = masterArea.withSizeKeepingCentre(
        juce::jmin(380, masterArea.getWidth()),
        masterArea.getHeight()
    );
    const auto masterControlWidth = masterControls.getWidth() / 2;
    auto dryWetBounds = masterControls.removeFromLeft(masterControlWidth)
        .reduced(6, 0);
    auto outputGainBounds = masterControls.reduced(6, 0);

    dryWetLabel.setBounds(dryWetBounds.removeFromTop(20));
    dryWetSlider.setBounds(dryWetBounds);
    outputGainLabel.setBounds(outputGainBounds.removeFromTop(20));
    outputGainSlider.setBounds(outputGainBounds);
}
