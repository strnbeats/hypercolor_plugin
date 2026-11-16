#include "PluginEditor.h"

#include <array>

namespace
{
constexpr std::array<const char*, 8> effectNames {
    "SPACE", "MODULATION", "DRIVE", "TAPE",
    "BITCRUSH", "FILTER", "DYNAMICS", "DELAY"
};

constexpr std::array<std::array<const char*, 9>, 8> effectParameters {{
    {{ hypercolor::ParameterIDs::reverbMix, hypercolor::ParameterIDs::reverbSize, hypercolor::ParameterIDs::reverbDecay, hypercolor::ParameterIDs::reverbDamping, hypercolor::ParameterIDs::reverbPreDelay, hypercolor::ParameterIDs::reverbWidth, nullptr, nullptr, nullptr }},
    {{ hypercolor::ParameterIDs::modulationMode, hypercolor::ParameterIDs::modulationRate, hypercolor::ParameterIDs::modulationDepth, hypercolor::ParameterIDs::modulationBaseDelay, hypercolor::ParameterIDs::modulationFeedback, hypercolor::ParameterIDs::modulationMix, hypercolor::ParameterIDs::modulationWidth, nullptr, nullptr }},
    {{ hypercolor::ParameterIDs::driveMode, hypercolor::ParameterIDs::driveAmount, hypercolor::ParameterIDs::driveTone, hypercolor::ParameterIDs::driveMix, hypercolor::ParameterIDs::driveOutput, nullptr, nullptr, nullptr, nullptr }},
    {{ hypercolor::ParameterIDs::tapeDrive, hypercolor::ParameterIDs::tapeWow, hypercolor::ParameterIDs::tapeFlutter, hypercolor::ParameterIDs::tapeWear, hypercolor::ParameterIDs::tapeMix, hypercolor::ParameterIDs::tapeOutput, nullptr, nullptr, nullptr }},
    {{ hypercolor::ParameterIDs::bitcrushBits, hypercolor::ParameterIDs::bitcrushRate, hypercolor::ParameterIDs::bitcrushMode, hypercolor::ParameterIDs::bitcrushMix, hypercolor::ParameterIDs::bitcrushOutput, nullptr, nullptr, nullptr, nullptr }},
    {{ hypercolor::ParameterIDs::filterMode, hypercolor::ParameterIDs::filterCutoff, hypercolor::ParameterIDs::filterResonance, hypercolor::ParameterIDs::filterDrive, hypercolor::ParameterIDs::filterMix, hypercolor::ParameterIDs::filterOutput, nullptr, nullptr, nullptr }},
    {{ hypercolor::ParameterIDs::compressorThreshold, hypercolor::ParameterIDs::compressorRatio, hypercolor::ParameterIDs::compressorAttack, hypercolor::ParameterIDs::compressorRelease, hypercolor::ParameterIDs::compressorKnee, hypercolor::ParameterIDs::compressorMakeup, hypercolor::ParameterIDs::compressorMix, hypercolor::ParameterIDs::compressorStereoLink, nullptr }},
    {{ hypercolor::ParameterIDs::delayMode, hypercolor::ParameterIDs::delaySync, hypercolor::ParameterIDs::delayTime, hypercolor::ParameterIDs::delayDivision, hypercolor::ParameterIDs::delayFeedback, hypercolor::ParameterIDs::delayLowCut, hypercolor::ParameterIDs::delayHighCut, hypercolor::ParameterIDs::delayMix, hypercolor::ParameterIDs::delayWidth }}
}};

constexpr std::array<std::size_t, 8> effectParameterCounts {
    6, 7, 5, 6, 5, 6, 8, 9
};

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
    addAndMakeVisible(xyController);
    xyController.setCoordinates({ 0.5f, 0.5f }, false);

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

    inspectorSectionLabel.setText("FX INSPECTOR", juce::dontSendNotification);
    inspectorSectionLabel.setColour(juce::Label::textColourId, pink);
    inspectorSectionLabel.setFont(juce::FontOptions(12.0f).withStyle("Bold"));
    addAndMakeVisible(inspectorSectionLabel);
    inspectorHintLabel.setText("REAL PARAMETERS  //  SELECT A MODULE BELOW", juce::dontSendNotification);
    inspectorHintLabel.setColour(juce::Label::textColourId, hypercolor::ui::palette::mutedText());
    inspectorHintLabel.setFont(juce::FontOptions(9.0f).withStyle("Bold"));
    addAndMakeVisible(inspectorHintLabel);
    fxRackLabel.setText("FX RACK  /  SIGNAL ORDER", juce::dontSendNotification);
    fxRackLabel.setColour(juce::Label::textColourId, lime);
    fxRackLabel.setFont(juce::FontOptions(10.0f).withStyle("Bold"));
    addAndMakeVisible(fxRackLabel);

    moduleTitleLabel.setColour(juce::Label::textColourId, text);
    moduleTitleLabel.setFont(juce::FontOptions(19.0f).withStyle("Bold"));
    moduleTitleLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(moduleTitleLabel);
    moduleSubtitleLabel.setColour(juce::Label::textColourId, hypercolor::ui::palette::mutedText());
    moduleSubtitleLabel.setFont(juce::FontOptions(9.0f).withStyle("Bold"));
    moduleSubtitleLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(moduleSubtitleLabel);

    granularNavButton.setButtonText("GRANULAR");
    granularNavButton.setClickingTogglesState(true);
    granularNavButton.setRadioGroupId(1701);
    granularNavButton.setColour(juce::TextButton::buttonColourId, panelRaised);
    granularNavButton.setColour(juce::TextButton::buttonOnColourId, cyan);
    granularNavButton.setColour(juce::TextButton::textColourOffId, text);
    granularNavButton.setColour(juce::TextButton::textColourOnId, hypercolor::ui::palette::background());
    granularNavButton.setComponentID("module.granular");
    granularNavButton.onClick = [this] { selectGranular(); };
    addAndMakeVisible(granularNavButton);

    for (std::size_t index = 0; index < effectButtons.size(); ++index)
    {
        auto& button = effectButtons[index];
        button.setButtonText(effectNames[index]);
        button.setClickingTogglesState(true);
        button.setRadioGroupId(1701);
        button.setColour(juce::TextButton::buttonColourId, panelRaised);
        button.setColour(juce::TextButton::buttonOnColourId, cyan);
        button.setColour(juce::TextButton::textColourOffId, text);
        button.setColour(juce::TextButton::textColourOnId, hypercolor::ui::palette::background());
        button.onClick = [this, index] { selectEffect(index); };
        button.setComponentID("module." + juce::String(static_cast<int>(index)));
        addAndMakeVisible(button);
    }
    granularNavButton.setToggleState(true, juce::dontSendNotification);
    moduleTitleLabel.setText("GRANULAR CORE", juce::dontSendNotification);
    moduleSubtitleLabel.setText("XY CHARACTER FIELD  //  REAL-TIME HISTORY  //  64 VOICES", juce::dontSendNotification);
    rebuildInspector();

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
        "PRESET / CURRENT STATE",
        wordmark,
        juce::Justification::centredRight,
        false
    );

    graphics.setFont(juce::FontOptions(9.0f).withStyle("Bold"));
    graphics.setColour(hypercolor::ui::palette::cyan());
    graphics.drawText(
        "DIGITAL CHAOS LAB",
        header.reduced(0.0f, 5.0f).withTrimmedLeft(header.getWidth() * 0.58f),
        juce::Justification::bottomRight,
        false
    );

    drawTechnicalPanel(
        graphics,
        grainControlPanel,
        hypercolor::ui::palette::cyan()
    );
    drawTechnicalPanel(
        graphics,
        inspectorPanel,
        hypercolor::ui::palette::pink()
    );
    drawTechnicalPanel(
        graphics,
        rackPanel,
        hypercolor::ui::palette::lime()
    );
    drawTechnicalPanel(
        graphics,
        masterPanel,
        hypercolor::ui::palette::pink()
    );
}

void HypercolorAudioProcessorEditor::selectGranular()
{
    selectedModule = -1;
    granularNavButton.setToggleState(true, juce::dontSendNotification);
    for (auto& button : effectButtons)
        button.setToggleState(false, juce::dontSendNotification);
    moduleTitleLabel.setText("GRANULAR CORE", juce::dontSendNotification);
    moduleSubtitleLabel.setText("XY CHARACTER FIELD  //  REAL-TIME HISTORY  //  64 VOICES", juce::dontSendNotification);
    rebuildInspector();
}

void HypercolorAudioProcessorEditor::selectEffect(std::size_t index)
{
    if (index >= effectButtons.size())
        return;

    selectedModule = static_cast<int>(index);
    selectedEffect = index;
    granularNavButton.setToggleState(false, juce::dontSendNotification);
    for (std::size_t i = 0; i < effectButtons.size(); ++i)
        effectButtons[i].setToggleState(i == selectedEffect, juce::dontSendNotification);
    rebuildInspector();
}

void HypercolorAudioProcessorEditor::rebuildInspector()
{
    for (auto& attachment : inspectorAttachments)
        attachment.reset();
    for (auto& slider : inspectorSliders)
    {
        slider.setVisible(false);
        removeChildComponent(&slider);
    }
    for (auto& label : inspectorLabels)
    {
        label.setVisible(false);
        removeChildComponent(&label);
    }

    const std::array<const char*, 9> granularParameters {
        hypercolor::ParameterIDs::grainCharacter,
        hypercolor::ParameterIDs::grainPitchRandom,
        hypercolor::ParameterIDs::grainPositionRandom,
        hypercolor::ParameterIDs::grainTimingJitter,
        hypercolor::ParameterIDs::grainStereoSpread,
        hypercolor::ParameterIDs::grainPanRandom,
        hypercolor::ParameterIDs::grainEnvelope,
        nullptr,
        nullptr
    };
    const auto* parameterList = selectedModule < 0
        ? granularParameters.data()
        : effectParameters[selectedEffect].data();
    const auto parameterCount = selectedModule < 0
        ? std::size_t { 7 }
        : effectParameterCounts[selectedEffect];

    inspectorSectionLabel.setText(
        selectedModule < 0
            ? "GRANULAR DETAIL  /  VARIATION"
            : juce::String("MODULE CONTROL  /  ") + effectNames[selectedEffect],
        juce::dontSendNotification
    );
    moduleTitleLabel.setText(
        selectedModule < 0 ? "GRANULAR CORE" : effectNames[selectedEffect],
        juce::dontSendNotification
    );
    moduleSubtitleLabel.setText(
        selectedModule < 0
            ? "XY CHARACTER FIELD  //  REAL-TIME HISTORY  //  64 VOICES"
            : "LIVE APVTS CONTROL  //  DSP MODULE  //  PARAMETERS RETAINED",
        juce::dontSendNotification
    );

    for (std::size_t i = 0; i < parameterCount; ++i)
    {
        const auto* parameterID = parameterList[i];
        auto* parameter = audioProcessor.getValueTreeState().getParameter(parameterID);
        if (parameter == nullptr)
            continue;

        auto& slider = inspectorSliders[i];
        auto& label = inspectorLabels[i];
        const auto range = parameter->getNormalisableRange();
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 74, 22);
        slider.setRange(range.start, range.end, range.interval);
        slider.setColour(juce::Slider::trackColourId, hypercolor::ui::palette::grid());
        slider.setColour(juce::Slider::backgroundColourId, hypercolor::ui::palette::background());
        slider.setColour(juce::Slider::thumbColourId, hypercolor::ui::palette::pink());
        slider.setColour(juce::Slider::textBoxTextColourId, hypercolor::ui::palette::text());
        slider.setColour(juce::Slider::textBoxBackgroundColourId, hypercolor::ui::palette::background());
        slider.setComponentID(parameterID);
        label.setText(juce::String(parameterID).toUpperCase(), juce::dontSendNotification);
        label.setColour(juce::Label::textColourId, hypercolor::ui::palette::mutedText());
        label.setFont(juce::FontOptions(9.0f).withStyle("Bold"));
        addAndMakeVisible(label);
        addAndMakeVisible(slider);
        inspectorAttachments[i] = std::make_unique<SliderAttachment>(
            audioProcessor.getValueTreeState(), parameterID, slider
        );
    }
    resized();
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
    content.removeFromTop(10);

    const auto footerHeight = juce::jlimit(86, 108, getHeight() / 7);
    auto footer = content.removeFromBottom(footerHeight);
    content.removeFromBottom(10);
    const auto navigationWidth = juce::jlimit(118, 152, getWidth() / 7);
    rackPanel = content.removeFromLeft(navigationWidth);
    content.removeFromLeft(12);
    auto workspace = content;
    masterPanel = footer;

    fxRackLabel.setBounds(rackPanel.reduced(10, 10).removeFromTop(20));
    auto navigationArea = rackPanel.reduced(10, 10);
    navigationArea.removeFromTop(28);
    granularNavButton.setBounds(navigationArea.removeFromTop(34));
    navigationArea.removeFromTop(6);
    for (auto& button : effectButtons)
    {
        button.setBounds(navigationArea.removeFromTop(30));
        navigationArea.removeFromTop(4);
    }

    moduleTitleLabel.setBounds(workspace.removeFromTop(27));
    moduleSubtitleLabel.setBounds(workspace.removeFromTop(18));
    workspace.removeFromTop(7);

    const bool granularMode = selectedModule < 0;
    for (auto* component : {
             static_cast<juce::Component*>(&xyController),
             static_cast<juce::Component*>(&granularVisualizer),
             static_cast<juce::Component*>(&granularSectionLabel),
             static_cast<juce::Component*>(&densitySlider),
             static_cast<juce::Component*>(&sizeSlider),
             static_cast<juce::Component*>(&positionSlider),
             static_cast<juce::Component*>(&pitchSlider),
             static_cast<juce::Component*>(&reverseButton),
             static_cast<juce::Component*>(&densityLabel),
             static_cast<juce::Component*>(&sizeLabel),
             static_cast<juce::Component*>(&positionLabel),
             static_cast<juce::Component*>(&pitchLabel),
             static_cast<juce::Component*>(&reverseLabel),
             static_cast<juce::Component*>(&positionHintLabel) })
        component->setVisible(granularMode);

    if (granularMode)
    {
        const auto gap = juce::jlimit(10, 18, workspace.getWidth() / 70);
        const auto leftWidth = juce::roundToInt(static_cast<float>(workspace.getWidth() - gap) * 0.58f);
        grainControlPanel = workspace.removeFromLeft(leftWidth);
        workspace.removeFromLeft(gap);
        inspectorPanel = workspace;
        auto grainContent = grainControlPanel.reduced(10, 8);
        constexpr int minimumControlsHeight = 86;
        constexpr int minimumVisualizerHeight = 58;
        const auto availableHeight = grainContent.getHeight();
        const auto desiredXYHeight = juce::jlimit(
            120,
            240,
            juce::roundToInt(static_cast<float>(availableHeight) * 0.48f)
        );
        const auto xyHeight = juce::jmax(
            80,
            juce::jmin(
                desiredXYHeight,
                juce::jmax(80, availableHeight - minimumVisualizerHeight - minimumControlsHeight)
            )
        );
        xyController.setBounds(grainContent.removeFromTop(xyHeight));
        grainContent.removeFromTop(8);
        const auto visualizerHeight = juce::jmax(
            minimumVisualizerHeight,
            juce::jmin(
                160,
                juce::jmin(
                    juce::roundToInt(static_cast<float>(grainContent.getHeight()) * 0.58f),
                    juce::jmax(minimumVisualizerHeight, grainContent.getHeight() - minimumControlsHeight)
                )
            )
        );
        granularVisualizer.setBounds(grainContent.removeFromTop(visualizerHeight));
        grainContent.removeFromTop(7);
        auto granularArea = grainContent;
        granularSectionLabel.setBounds(granularArea.removeFromTop(20));
        granularArea.removeFromTop(2);

        const auto granularControlWidth = juce::jmax(1, granularArea.getWidth() / 5);
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
        {
            if (cell.getHeight() >= 38)
                positionHintLabel.setBounds(cell.removeFromBottom(17));
            else
                positionHintLabel.setBounds({});
        }

        if (granularControls[index] == &reverseButton)
        {
            granularControls[index]->setBounds(
                cell.withSizeKeepingCentre(
                    juce::jmin(76, juce::jmax(40, cell.getWidth() - 10)),
                    juce::jmin(48, cell.getHeight())
                )
            );
        }
        else
        {
            granularControls[index]->setBounds(cell);
        }
    }
    }
    else
    {
        grainControlPanel = {};
        inspectorPanel = workspace;
        inspectorSliders.front().setVisible(true);
    }

    auto inspectorArea = inspectorPanel.reduced(16, 12);
    inspectorSectionLabel.setBounds(inspectorArea.removeFromTop(26));
    inspectorHintLabel.setBounds(inspectorArea.removeFromTop(18));
    inspectorArea.removeFromTop(8);
    const auto rowHeight = juce::jmax(31, inspectorArea.getHeight() / 9);
    for (std::size_t i = 0; i < inspectorSliders.size(); ++i)
    {
        if (!inspectorSliders[i].isVisible())
            continue;
        auto row = inspectorArea.removeFromTop(rowHeight);
        inspectorLabels[i].setBounds(row.removeFromLeft(118));
        inspectorSliders[i].setBounds(row.reduced(3, 1));
    }

    auto masterArea = masterPanel.reduced(12, 8);
    masterSectionLabel.setBounds(masterArea.removeFromTop(20));
    masterArea.removeFromTop(2);
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
