#include "PluginEditor.h"

#include <array>

namespace
{
void drawTechnicalPanel(
    juce::Graphics& graphics,
    juce::Rectangle<int> integerBounds,
    juce::Colour accent
)
{
    auto bounds = integerBounds.toFloat();

    if (bounds.isEmpty())
        return;

    constexpr float cut = 13.0f;
    juce::Path panelPath;
    panelPath.startNewSubPath(bounds.getX(), bounds.getY());
    panelPath.lineTo(bounds.getRight() - cut, bounds.getY());
    panelPath.lineTo(bounds.getRight(), bounds.getY() + cut);
    panelPath.lineTo(bounds.getRight(), bounds.getBottom());
    panelPath.lineTo(bounds.getX() + cut, bounds.getBottom());
    panelPath.lineTo(bounds.getX(), bounds.getBottom() - cut);
    panelPath.closeSubPath();

    graphics.setColour(hypercolor::ui::palette::panel());
    graphics.fillPath(panelPath);
    graphics.setColour(hypercolor::ui::palette::border());
    graphics.strokePath(panelPath, juce::PathStrokeType(1.0f));

    graphics.setColour(accent);
    graphics.fillRect(bounds.getX(), bounds.getY(), 54.0f, 2.0f);
    graphics.fillRect(bounds.getRight() - 18.0f, bounds.getY(), 18.0f, 2.0f);
}
}

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
    setLookAndFeel(&lookAndFeel);

    const auto cyan = hypercolor::ui::palette::cyan();
    const auto pink = hypercolor::ui::palette::pink();
    const auto lime = hypercolor::ui::palette::lime();
    const auto panelRaised = hypercolor::ui::palette::panelRaised();
    const auto text = hypercolor::ui::palette::text();

    addAndMakeVisible(granularVisualizer);

    const auto configureSlider = [
        this,
        text
    ](
        juce::Slider& slider,
        juce::String suffix,
        juce::Colour accent
    )
    {
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 96, 25);
        slider.setMouseDragSensitivity(180);
        slider.setTextValueSuffix(std::move(suffix));
        slider.setColour(juce::Slider::rotarySliderFillColourId, accent);
        slider.setColour(
            juce::Slider::rotarySliderOutlineColourId,
            hypercolor::ui::palette::grid()
        );
        slider.setColour(juce::Slider::thumbColourId, accent);
        slider.setColour(
            juce::Slider::textBoxBackgroundColourId,
            hypercolor::ui::palette::background()
        );
        slider.setColour(juce::Slider::textBoxTextColourId, text);
        slider.setColour(
            juce::Slider::textBoxOutlineColourId,
            hypercolor::ui::palette::border()
        );
        addAndMakeVisible(slider);
    };

    configureSlider(dryWetSlider, " %", lime);
    configureSlider(outputGainSlider, " dB", cyan);
    configureSlider(densitySlider, " gr/s", cyan);
    configureSlider(sizeSlider, " ms", pink);
    configureSlider(positionSlider, {}, lime);
    configureSlider(pitchSlider, {}, cyan);

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
        label->setFont(juce::FontOptions(11.0f).withStyle("Bold"));
        addAndMakeVisible(*label);
    }

    positionHintLabel.setText(
        "0% FRESH  •  100% OLD",
        juce::dontSendNotification
    );
    positionHintLabel.setJustificationType(juce::Justification::centred);
    positionHintLabel.setColour(
        juce::Label::textColourId,
        hypercolor::ui::palette::mutedText()
    );
    positionHintLabel.setFont(juce::FontOptions(9.0f).withStyle("Bold"));
    addAndMakeVisible(positionHintLabel);

    granularSectionLabel.setText(
        "01 / GRAIN CONTROL",
        juce::dontSendNotification
    );
    masterSectionLabel.setText("02 / MASTER", juce::dontSendNotification);

    granularSectionLabel.setJustificationType(juce::Justification::centredLeft);
    granularSectionLabel.setColour(juce::Label::textColourId, cyan);
    granularSectionLabel.setFont(juce::FontOptions(12.0f).withStyle("Bold"));
    masterSectionLabel.setJustificationType(juce::Justification::centredLeft);
    masterSectionLabel.setColour(juce::Label::textColourId, pink);
    masterSectionLabel.setFont(juce::FontOptions(12.0f).withStyle("Bold"));
    addAndMakeVisible(granularSectionLabel);
    addAndMakeVisible(masterSectionLabel);

    reverseButton.setClickingTogglesState(true);
    reverseButton.setColour(
        juce::TextButton::buttonColourId,
        panelRaised
    );
    reverseButton.setColour(
        juce::TextButton::buttonOnColourId,
        pink
    );
    reverseButton.setColour(juce::TextButton::textColourOffId, text);
    reverseButton.setColour(
        juce::TextButton::textColourOnId,
        hypercolor::ui::palette::background()
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
    setResizeLimits(760, 560, 1600, 1000);
    setSize(1100, 700);
}

HypercolorAudioProcessorEditor::~HypercolorAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

void HypercolorAudioProcessorEditor::paint(juce::Graphics& graphics)
{
    juce::ignoreUnused(audioProcessor);

    graphics.fillAll(hypercolor::ui::palette::background());

    const auto header = headerArea.toFloat();
    graphics.setColour(hypercolor::ui::palette::grid());
    graphics.drawHorizontalLine(
        headerArea.getBottom() - 1,
        header.getX(),
        header.getRight()
    );

    auto wordmark = header.reduced(0.0f, 5.0f);
    const auto titleWidth = juce::jmin(460.0f, wordmark.getWidth() * 0.58f);
    auto titleArea = wordmark.removeFromLeft(titleWidth);

    graphics.setFont(juce::FontOptions(43.0f).withStyle("Bold"));
    graphics.setColour(hypercolor::ui::palette::text());
    graphics.drawFittedText(
        "HYPER",
        titleArea.removeFromLeft(titleWidth * 0.54f).toNearestInt(),
        juce::Justification::centredLeft,
        1
    );
    graphics.setColour(hypercolor::ui::palette::pink());
    graphics.drawFittedText(
        "COLOR",
        titleArea.toNearestInt(),
        juce::Justification::centredLeft,
        1
    );

    graphics.setColour(hypercolor::ui::palette::cyan());
    graphics.fillRect(header.getX(), header.getBottom() - 4.0f, 96.0f, 2.0f);
    graphics.setColour(hypercolor::ui::palette::lime());
    graphics.fillRect(header.getX() + 100.0f, header.getBottom() - 4.0f, 31.0f, 2.0f);

    graphics.setFont(juce::FontOptions(10.0f).withStyle("Bold"));
    graphics.setColour(hypercolor::ui::palette::mutedText());
    graphics.drawText(
        "EXPERIMENTAL GRANULAR PROCESSOR  //  64 VOICE FIELD",
        wordmark.removeFromBottom(23.0f),
        juce::Justification::centredRight,
        false
    );
    graphics.setColour(hypercolor::ui::palette::lime());
    graphics.drawText(
        "HYPERCOLOR  V0.1.0",
        wordmark,
        juce::Justification::centredRight,
        false
    );

    drawTechnicalPanel(
        graphics,
        grainControlPanel,
        hypercolor::ui::palette::cyan()
    );
    drawTechnicalPanel(
        graphics,
        masterPanel,
        hypercolor::ui::palette::pink()
    );
}

void HypercolorAudioProcessorEditor::resized()
{
    const auto margin = juce::jlimit(18, 36, getWidth() / 30);
    auto content = getLocalBounds().reduced(margin);
    const auto headerHeight = juce::jlimit(
        68,
        96,
        juce::roundToInt(static_cast<float>(getHeight()) * 0.13f)
    );
    headerArea = content.removeFromTop(headerHeight);
    content.removeFromTop(12);

    const auto visualizerHeight = juce::jlimit(
        210,
        440,
        juce::roundToInt(static_cast<float>(content.getHeight()) * 0.57f)
    );
    granularVisualizer.setBounds(content.removeFromTop(visualizerHeight));
    content.removeFromTop(14);

    const auto panelGap = juce::jlimit(10, 18, getWidth() / 70);
    const auto grainPanelWidth = juce::roundToInt(
        static_cast<float>(content.getWidth() - panelGap) * 0.70f
    );
    grainControlPanel = content.removeFromLeft(grainPanelWidth);
    content.removeFromLeft(panelGap);
    masterPanel = content;

    auto granularArea = grainControlPanel.reduced(14, 10);
    auto masterArea = masterPanel.reduced(14, 10);

    granularSectionLabel.setBounds(granularArea.removeFromTop(24));
    masterSectionLabel.setBounds(masterArea.removeFromTop(24));
    granularArea.removeFromTop(3);
    masterArea.removeFromTop(3);

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
        ).reduced(5, 0);
        granularLabels[index]->setBounds(cell.removeFromTop(21));

        if (granularControls[index] == &positionSlider)
            positionHintLabel.setBounds(cell.removeFromBottom(17));

        if (granularControls[index] == &reverseButton)
        {
            granularControls[index]->setBounds(
                cell.withSizeKeepingCentre(
                    juce::jmax(76, cell.getWidth() - 10),
                    juce::jmin(48, cell.getHeight())
                )
            );
        }
        else
        {
            granularControls[index]->setBounds(cell);
        }
    }

    auto masterControls = masterArea;
    const auto masterControlWidth = masterControls.getWidth() / 2;
    auto dryWetBounds = masterControls.removeFromLeft(masterControlWidth)
        .reduced(6, 0);
    auto outputGainBounds = masterControls.reduced(6, 0);

    dryWetLabel.setBounds(dryWetBounds.removeFromTop(20));
    dryWetSlider.setBounds(dryWetBounds);
    outputGainLabel.setBounds(outputGainBounds.removeFromTop(20));
    outputGainSlider.setBounds(outputGainBounds);
}
