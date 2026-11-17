#include "PluginEditor.h"

#include <array>
#include <cmath>
#include <cstring>

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

constexpr std::array<std::size_t, 8> effectMixIndices {
    0, 5, 3, 4, 3, 4, 6, 7
};

juce::Colour effectAccent(std::size_t effectIndex)
{
    using namespace hypercolor::ui::palette;
    constexpr std::array<juce::Colour (*)(void) noexcept, 8> accents {
        space, modulation, drive, tape, bitcrush, filter, dynamics, delay
    };
    return effectIndex < accents.size() ? accents[effectIndex]() : pink();
}

void drawTechnicalPanel(
    juce::Graphics& graphics,
    juce::Rectangle<int> integerBounds,
    juce::Colour accent,
    bool cutTopRight = true,
    bool cutBottomLeft = true
)
{
    auto bounds = integerBounds.toFloat();

    if (bounds.isEmpty())
        return;

    constexpr float cut = 13.0f;
    juce::Path panelPath;
    panelPath.startNewSubPath(bounds.getX(), bounds.getY());
    panelPath.lineTo(
        cutTopRight ? bounds.getRight() - cut : bounds.getRight(),
        bounds.getY()
    );
    if (cutTopRight)
        panelPath.lineTo(bounds.getRight(), bounds.getY() + cut);
    panelPath.lineTo(bounds.getRight(), bounds.getBottom());
    panelPath.lineTo(
        cutBottomLeft ? bounds.getX() + cut : bounds.getX(),
        bounds.getBottom()
    );
    if (cutBottomLeft)
        panelPath.lineTo(bounds.getX(), bounds.getBottom() - cut);
    panelPath.closeSubPath();

    graphics.setColour(hypercolor::ui::palette::panel().darker(0.18f));
    graphics.fillPath(panelPath);
    graphics.setColour(hypercolor::ui::palette::border());
    graphics.strokePath(panelPath, juce::PathStrokeType(1.0f));
    graphics.setColour(juce::Colours::white.withAlpha(0.025f));
    graphics.fillRect(bounds.getX() + 1.0f, bounds.getY() + 2.0f,
                      juce::jmax(0.0f, bounds.getWidth() - 2.0f), 1.0f);

    graphics.setColour(accent);
    graphics.fillRect(bounds.getX(), bounds.getY(), 54.0f, 2.0f);
    graphics.fillRect(bounds.getRight() - 18.0f, bounds.getY(), 18.0f, 2.0f);
}

void fillTechnicalSurface(
    juce::Graphics& graphics,
    juce::Rectangle<int> integerBounds
)
{
    auto bounds = integerBounds.toFloat();

    if (bounds.isEmpty())
        return;

    constexpr float cut = 13.0f;
    juce::Path surface;
    surface.startNewSubPath(bounds.getX(), bounds.getY());
    surface.lineTo(bounds.getRight() - cut, bounds.getY());
    surface.lineTo(bounds.getRight(), bounds.getY() + cut);
    surface.lineTo(bounds.getRight(), bounds.getBottom());
    surface.lineTo(bounds.getX() + cut, bounds.getBottom());
    surface.lineTo(bounds.getX(), bounds.getBottom() - cut);
    surface.closeSubPath();

    graphics.setColour(hypercolor::ui::palette::panel().darker(0.18f));
    graphics.fillPath(surface);
}

void drawDriveBackdrop(
    juce::Graphics& graphics,
    juce::Rectangle<int> integerBounds,
    juce::Colour accent
)
{
    auto bounds = integerBounds.toFloat().reduced(2.0f);
    if (bounds.isEmpty())
        return;

    juce::Graphics::ScopedSaveState state(graphics);
    graphics.reduceClipRegion(bounds.toNearestInt());

    // A quiet, layered surface gives the Drive page a physical depth without
    // pretending that it is an oscilloscope or a meter.  The diagonal field
    // also creates a visual axis for the four real Drive parameters.
    juce::ColourGradient wash(
        hypercolor::ui::palette::panelRaised().withAlpha(0.48f),
        bounds.getX(),
        bounds.getY(),
        hypercolor::ui::palette::background().withAlpha(0.06f),
        bounds.getRight(),
        bounds.getBottom(),
        false
    );
    graphics.setGradientFill(wash);
    graphics.fillRect(bounds);

    const auto focus = bounds.withSizeKeepingCentre(
        bounds.getWidth() * 0.78f,
        bounds.getHeight() * 0.68f
    );
    graphics.setColour(accent.withAlpha(0.035f));
    graphics.fillEllipse(focus);
    graphics.setColour(accent.withAlpha(0.10f));
    graphics.drawEllipse(focus.reduced(1.0f), 1.0f);

    const auto axisY = bounds.getCentreY();
    graphics.setColour(accent.withAlpha(0.16f));
    graphics.drawLine(bounds.getX() + 26.0f, axisY, bounds.getRight() - 26.0f, axisY, 1.0f);
    graphics.setColour(hypercolor::ui::palette::grid().withAlpha(0.45f));
    graphics.drawLine(bounds.getX() + 26.0f, axisY - 1.0f, bounds.getRight() - 26.0f, axisY - 1.0f, 1.0f);

    const auto railX = bounds.getX() + bounds.getWidth() * 0.72f;
    graphics.setColour(accent.withAlpha(0.18f));
    graphics.fillRect(railX, bounds.getY() + 18.0f, 2.0f, bounds.getHeight() - 36.0f);
    graphics.setColour(accent.withAlpha(0.42f));
    graphics.fillRect(railX - 5.0f, bounds.getCentreY() - 1.0f, 12.0f, 2.0f);

    const auto corner = juce::Rectangle<float>(
        bounds.getX() + 18.0f,
        bounds.getBottom() - 34.0f,
        92.0f,
        1.0f
    );
    graphics.setColour(accent.withAlpha(0.55f));
    graphics.fillRect(corner);
    graphics.setColour(hypercolor::ui::palette::text().withAlpha(0.18f));
    graphics.fillRect(corner.withX(corner.getX() + 102.0f).withWidth(32.0f));
}

void drawEffectBackdrop(
    juce::Graphics& graphics,
    juce::Rectangle<int> integerBounds,
    std::size_t effectIndex,
    juce::Colour accent
)
{
    if (effectIndex == 2)
    {
        drawDriveBackdrop(graphics, integerBounds, accent);
        return;
    }

    auto bounds = integerBounds.toFloat().reduced(2.0f);
    if (bounds.isEmpty())
        return;

    juce::Graphics::ScopedSaveState state(graphics);
    graphics.reduceClipRegion(bounds.toNearestInt());

    juce::ColourGradient wash(
        hypercolor::ui::palette::panelRaised().withAlpha(0.32f),
        bounds.getX(),
        bounds.getY(),
        hypercolor::ui::palette::background().withAlpha(0.05f),
        bounds.getRight(),
        bounds.getBottom(),
        false
    );
    graphics.setGradientFill(wash);
    graphics.fillRect(bounds);

    const auto centre = bounds.getCentre();
    const auto pi = juce::MathConstants<float>::pi;
    const auto midY = bounds.getCentreY();

    switch (effectIndex)
    {
        case 0: // SPACE: concentric depth fields and orbital reflections.
        {
            const auto spaceCentre = centre.translated(bounds.getWidth() * 0.08f, 0.0f);
            for (int ring = 0; ring < 5; ++ring)
            {
                const auto scale = 0.18f + static_cast<float>(ring) * 0.14f;
                const auto ellipse = juce::Rectangle<float>(
                    bounds.getWidth() * scale,
                    bounds.getHeight() * scale * 0.62f
                ).withCentre(spaceCentre);
                graphics.setColour(accent.withAlpha(
                    0.035f + static_cast<float>(ring) * 0.012f
                ));
                graphics.drawEllipse(ellipse, ring == 4 ? 1.4f : 0.8f);
            }

            for (int arc = 0; arc < 4; ++arc)
            {
                juce::Path path;
                const auto radius = bounds.getHeight()
                    * (0.24f + static_cast<float>(arc) * 0.075f);
                path.addCentredArc(
                    spaceCentre.x,
                    spaceCentre.y,
                    radius * 1.35f,
                    radius * 0.54f,
                    0.0f,
                    -1.2f + static_cast<float>(arc) * 0.08f,
                    1.0f + static_cast<float>(arc) * 0.12f,
                    true
                );
                graphics.setColour(accent.withAlpha(
                    0.10f - static_cast<float>(arc) * 0.012f
                ));
                graphics.strokePath(path, juce::PathStrokeType(1.2f));
            }
            break;
        }

        case 1: // MODULATION: phase-shifted interference fields.
        {
            for (int wave = 0; wave < 4; ++wave)
            {
                juce::Path path;
                const auto phase = static_cast<float>(wave) * 0.72f;
                const auto amplitude = bounds.getHeight()
                    * (0.045f + static_cast<float>(wave) * 0.012f);
                for (int sample = 0; sample <= 72; ++sample)
                {
                    const auto x = bounds.getX()
                        + bounds.getWidth() * static_cast<float>(sample) / 72.0f;
                    const auto t = static_cast<float>(sample) / 72.0f;
                    const auto y = midY
                        + std::sin(t * pi * 3.2f + phase) * amplitude
                        + std::sin(t * pi * 7.0f - phase) * amplitude * 0.24f;
                    if (sample == 0)
                        path.startNewSubPath(x, y);
                    else
                        path.lineTo(x, y);
                }
                graphics.setColour(accent.withAlpha(
                    0.08f - static_cast<float>(wave) * 0.012f
                ));
                graphics.strokePath(path, juce::PathStrokeType(wave == 0 ? 1.5f : 0.8f));
            }
            graphics.setColour(accent.withAlpha(0.18f));
            graphics.drawLine(bounds.getX() + 26.0f, midY, bounds.getRight() - 26.0f, midY, 1.0f);
            break;
        }

        case 3: // TAPE: slow, non-periodic magnetic drift lines.
        {
            for (int line = 0; line < 5; ++line)
            {
                juce::Path path;
                const auto yBase = bounds.getY() + bounds.getHeight()
                    * (0.25f + static_cast<float>(line) * 0.12f);
                for (int sample = 0; sample <= 70; ++sample)
                {
                    const auto t = static_cast<float>(sample) / 70.0f;
                    const auto x = bounds.getX() + t * bounds.getWidth();
                    const auto y = yBase
                        + std::sin(t * pi * (
                            1.35f + static_cast<float>(line) * 0.08f
                        ) + static_cast<float>(line) * 0.55f)
                            * bounds.getHeight() * 0.025f
                        + std::sin(t * pi * 4.0f + static_cast<float>(line))
                            * bounds.getHeight() * 0.008f;
                    if (sample == 0)
                        path.startNewSubPath(x, y);
                    else
                        path.lineTo(x, y);
                }
                graphics.setColour(accent.withAlpha(0.07f + (line == 2 ? 0.05f : 0.0f)));
                graphics.strokePath(path, juce::PathStrokeType(line == 2 ? 1.5f : 0.8f));
            }
            graphics.setColour(accent.withAlpha(0.16f));
            graphics.fillRect(bounds.getX() + 22.0f, bounds.getBottom() - 32.0f, bounds.getWidth() * 0.34f, 2.0f);
            break;
        }

        case 4: // BITCRUSH: stepped quantisation lattice.
        {
            const auto steps = 12;
            const auto stepWidth = bounds.getWidth() / static_cast<float>(steps);
            for (int step = 0; step < steps; ++step)
            {
                const auto x = bounds.getX()
                    + static_cast<float>(step) * stepWidth;
                const auto h = bounds.getHeight() * (0.12f + (step % 4) * 0.045f);
                graphics.setColour(accent.withAlpha(0.035f));
                graphics.fillRect(x, midY - h, stepWidth - 2.0f, h * 2.0f);
            }

            juce::Path staircase;
            staircase.startNewSubPath(bounds.getX() + 20.0f, midY + bounds.getHeight() * 0.16f);
            for (int step = 0; step < steps; ++step)
            {
                const auto x = bounds.getX() + 20.0f
                    + static_cast<float>(step) * stepWidth;
                const auto level = (step % 5) * bounds.getHeight() * 0.045f;
                staircase.lineTo(x, midY - level);
                staircase.lineTo(x + stepWidth * 0.72f, midY - level);
            }
            graphics.setColour(accent.withAlpha(0.18f));
            graphics.strokePath(staircase, juce::PathStrokeType(1.4f));
            graphics.setColour(accent.withAlpha(0.23f));
            graphics.drawHorizontalLine(juce::roundToInt(midY), bounds.getX() + 20.0f, bounds.getRight() - 20.0f);
            break;
        }

        case 5: // FILTER: restrained frequency curve with a resonant peak.
        {
            juce::Path curve;
            for (int sample = 0; sample <= 80; ++sample)
            {
                const auto t = static_cast<float>(sample) / 80.0f;
                const auto x = bounds.getX() + 22.0f + t * (bounds.getWidth() - 44.0f);
                const auto peak = std::exp(-std::pow((t - 0.62f) / 0.12f, 2.0f));
                const auto slope = 0.26f - t * 0.20f;
                const auto y = bounds.getBottom() - 42.0f
                    - (slope + peak * 0.23f) * bounds.getHeight();
                if (sample == 0)
                    curve.startNewSubPath(x, y);
                else
                    curve.lineTo(x, y);
            }
            graphics.setColour(accent.withAlpha(0.19f));
            graphics.strokePath(curve, juce::PathStrokeType(1.7f));
            graphics.setColour(accent.withAlpha(0.09f));
            graphics.drawVerticalLine(
                juce::roundToInt(bounds.getX() + bounds.getWidth() * 0.62f),
                bounds.getY() + 22.0f,
                bounds.getBottom() - 30.0f
            );
            break;
        }

        case 6: // DYNAMICS: envelope, threshold and recovery curve.
        {
            juce::Path envelope;
            envelope.startNewSubPath(bounds.getX() + 22.0f, bounds.getBottom() - 38.0f);
            envelope.cubicTo(
                bounds.getX() + bounds.getWidth() * 0.24f,
                bounds.getBottom() - bounds.getHeight() * 0.60f,
                bounds.getX() + bounds.getWidth() * 0.34f,
                bounds.getY() + bounds.getHeight() * 0.23f,
                bounds.getX() + bounds.getWidth() * 0.52f,
                bounds.getY() + bounds.getHeight() * 0.28f
            );
            envelope.cubicTo(
                bounds.getX() + bounds.getWidth() * 0.70f,
                bounds.getY() + bounds.getHeight() * 0.30f,
                bounds.getX() + bounds.getWidth() * 0.77f,
                bounds.getBottom() - bounds.getHeight() * 0.36f,
                bounds.getRight() - 22.0f,
                bounds.getBottom() - 38.0f
            );
            graphics.setColour(accent.withAlpha(0.18f));
            graphics.strokePath(envelope, juce::PathStrokeType(1.8f));
            const auto thresholdY = bounds.getY() + bounds.getHeight() * 0.46f;
            constexpr float thresholdDashes[] { 5.0f, 4.0f };
            graphics.setColour(accent.withAlpha(0.13f));
            graphics.drawDashedLine(
                juce::Line<float>(bounds.getX() + 22.0f, thresholdY, bounds.getRight() - 22.0f, thresholdY),
                thresholdDashes,
                2,
                1.0f
            );
            break;
        }

        case 7: // DELAY: repeated echoes on a measured time rail.
        {
            const auto railY = bounds.getBottom() - 48.0f;
            graphics.setColour(accent.withAlpha(0.14f));
            graphics.drawLine(bounds.getX() + 22.0f, railY, bounds.getRight() - 22.0f, railY, 1.0f);
            for (int echo = 0; echo < 8; ++echo)
            {
                const auto t = static_cast<float>(echo + 1) / 9.0f;
                const auto x = bounds.getX() + 32.0f + t * (bounds.getWidth() - 64.0f);
                const auto h = bounds.getHeight()
                    * (0.32f - static_cast<float>(echo) * 0.028f);
                graphics.setColour(accent.withAlpha(
                    0.17f - static_cast<float>(echo) * 0.016f
                ));
                graphics.fillRect(x, railY - h, echo == 0 ? 3.0f : 2.0f, h);
                graphics.setColour(accent.withAlpha(0.08f));
                graphics.drawLine(x + 5.0f, railY - h * 0.55f, x + 30.0f, railY - h * 0.55f, 1.0f);
            }
            break;
        }

        default:
            break;
    }
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
        "0% FRESH  /  100% OLD",
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
        button.setColour(
            juce::TextButton::buttonOnColourId,
            effectAccent(index)
        );
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

    const auto headerControlWidth = 244.0f;
    const auto brandWidth = juce::jmin(208.0f, header.getWidth() * 0.32f);
    auto brandArea = header.withTrimmedRight(headerControlWidth + 14.0f)
        .removeFromLeft(brandWidth)
        .reduced(0.0f, 7.0f);

    // The large split wordmark no longer consumes the entire top band.  A
    // compact instrument stamp leaves the header quiet and moves the
    // expressive HYPER/COLOR treatment into the XY field itself.
    graphics.setFont(juce::FontOptions(17.0f).withStyle("Bold"));
    graphics.setColour(hypercolor::ui::palette::text());
    graphics.drawText("HYPERCOLOR", brandArea.removeFromTop(25.0f),
                      juce::Justification::centredLeft, false);
    graphics.setFont(juce::FontOptions(8.0f).withStyle("Bold"));
    graphics.setColour(hypercolor::ui::palette::mutedText());
    graphics.drawText("PREMIUM DIGITAL INSTRUMENT", brandArea.removeFromTop(16.0f),
                      juce::Justification::centredLeft, false);

    graphics.setColour(hypercolor::ui::palette::cyan());
    graphics.fillRect(header.getX(), header.getBottom() - 3.0f, 62.0f, 2.0f);
    graphics.setColour(hypercolor::ui::palette::pink());
    graphics.fillRect(header.getX() + 67.0f, header.getBottom() - 3.0f, 24.0f, 2.0f);

    auto statusArea = header.withTrimmedLeft(brandWidth + 18.0f)
        .withTrimmedRight(headerControlWidth + 14.0f)
        .reduced(0.0f, 9.0f);
    graphics.setFont(juce::FontOptions(9.0f).withStyle("Bold"));
    graphics.setColour(hypercolor::ui::palette::mutedText());
    graphics.drawText(
        "EXPERIMENTAL GRANULAR PROCESSOR  //  64 VOICE FIELD",
        statusArea.removeFromTop(22.0f),
        juce::Justification::centredRight,
        false
    );
    graphics.setColour(hypercolor::ui::palette::lime());
    graphics.drawText(
        "PRESET  /  CURRENT STATE",
        statusArea.removeFromTop(18.0f),
        juce::Justification::centredRight,
        false
    );
    graphics.setColour(hypercolor::ui::palette::cyan());
    graphics.drawText(
        "DIGITAL CHAOS LAB",
        statusArea,
        juce::Justification::bottomRight,
        false
    );

    const auto headerControls = header.withTrimmedLeft(header.getWidth() - headerControlWidth)
        .reduced(0.0f, 6.0f);
    auto controlsSurface = headerControls.reduced(1.0f);
    juce::ColourGradient controlsGradient(
        hypercolor::ui::palette::panelRaised().brighter(0.08f).withAlpha(0.98f),
        controlsSurface.getX(),
        controlsSurface.getY(),
        hypercolor::ui::palette::background().withAlpha(0.98f),
        controlsSurface.getRight(),
        controlsSurface.getBottom(),
        false
    );
    graphics.setGradientFill(controlsGradient);
    graphics.fillRoundedRectangle(controlsSurface, 6.0f);
    graphics.setColour(hypercolor::ui::palette::border().withAlpha(0.9f));
    graphics.drawRoundedRectangle(controlsSurface, 6.0f, 1.0f);
    graphics.setColour(hypercolor::ui::palette::grid().withAlpha(0.85f));
    graphics.drawVerticalLine(
        juce::roundToInt(controlsSurface.getCentreX()),
        controlsSurface.getY() + 8.0f,
        controlsSurface.getBottom() - 8.0f
    );
    graphics.setColour(hypercolor::ui::palette::lime().withAlpha(0.75f));
    graphics.fillRect(controlsSurface.getX() + 10.0f, controlsSurface.getY(), 34.0f, 2.0f);
    graphics.setColour(hypercolor::ui::palette::cyan().withAlpha(0.75f));
    graphics.fillRect(controlsSurface.getCentreX() + 10.0f, controlsSurface.getY(), 34.0f, 2.0f);

    drawTechnicalPanel(
        graphics,
        grainControlPanel,
        hypercolor::ui::palette::cyan()
    );
    if (selectedModule >= 0)
    {
        // The two FX panels share one visual surface.  The backdrop fills the
        // internal corner cuts at their seam, while the individual panels
        // below still provide their own borders and accent colours.
        fillTechnicalSurface(
            graphics,
            inspectorPanel.getUnion(masterPanel)
        );
    }
    const auto workspaceAccent = selectedModule >= 0
        ? effectAccent(selectedEffect)
        : hypercolor::ui::palette::cyan();
    drawTechnicalPanel(
        graphics,
        inspectorPanel,
        workspaceAccent,
        selectedModule < 0,
        true
    );
    drawTechnicalPanel(
        graphics,
        rackPanel,
        hypercolor::ui::palette::lime()
    );
    drawTechnicalPanel(
        graphics,
        masterPanel,
        workspaceAccent,
        true,
        selectedModule < 0
    );
    if (selectedModule >= 0)
    {
        drawEffectBackdrop(
            graphics,
            inspectorPanel,
            selectedEffect,
            effectAccent(selectedEffect)
        );
    }
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
    inspectorHintLabel.setText(
        selectedModule == 2
            ? "SATURATION CHARACTER  //  OUTPUT STAGE"
            : "REAL PARAMETERS  //  SELECT A MODULE BELOW",
        juce::dontSendNotification
    );
    inspectorSectionLabel.setColour(
        juce::Label::textColourId,
        selectedModule >= 0 ? effectAccent(selectedEffect) : hypercolor::ui::palette::pink()
    );
    masterSectionLabel.setColour(
        juce::Label::textColourId,
        selectedModule >= 0 ? effectAccent(selectedEffect) : hypercolor::ui::palette::pink()
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
        const auto isEffect = selectedModule >= 0;
        const auto isEffectMix = isEffect && i == effectMixIndices[selectedEffect];
        const auto accent = isEffect
            ? effectAccent(selectedEffect)
            : (i == 1 || i == 2 ? hypercolor::ui::palette::pink()
                                : hypercolor::ui::palette::cyan());
        slider.setSliderStyle(
            isEffect ? juce::Slider::RotaryHorizontalVerticalDrag
                     : juce::Slider::LinearHorizontal
        );
        slider.setTextBoxStyle(
            isEffect ? juce::Slider::TextBoxBelow : juce::Slider::TextBoxRight,
            false,
            isEffect ? 86 : 74,
            isEffect ? 24 : 22
        );
        slider.setMouseDragSensitivity(isEffect ? 180 : 140);
        slider.setRange(range.start, range.end, range.interval);
        slider.setColour(juce::Slider::trackColourId, hypercolor::ui::palette::grid());
        slider.setColour(juce::Slider::backgroundColourId, hypercolor::ui::palette::background());
        slider.setColour(juce::Slider::thumbColourId, accent);
        slider.setColour(juce::Slider::rotarySliderFillColourId, accent);
        slider.setColour(
            juce::Slider::rotarySliderOutlineColourId,
            hypercolor::ui::palette::grid()
        );
        slider.setColour(juce::Slider::textBoxTextColourId, hypercolor::ui::palette::text());
        slider.setColour(juce::Slider::textBoxBackgroundColourId, hypercolor::ui::palette::background());
        slider.setComponentID(parameterID);
        auto displayName = isEffectMix
            ? juce::String("MIX")
            : juce::String(parameterID).toUpperCase();
        if (selectedModule == 2)
        {
            if (std::strcmp(parameterID, hypercolor::ParameterIDs::driveMode) == 0)
                displayName = "MODE";
            else if (std::strcmp(parameterID, hypercolor::ParameterIDs::driveAmount) == 0)
                displayName = "DRIVE";
            else if (std::strcmp(parameterID, hypercolor::ParameterIDs::driveTone) == 0)
                displayName = "TONE";
            else if (std::strcmp(parameterID, hypercolor::ParameterIDs::driveOutput) == 0)
                displayName = "OUTPUT";
        }
        label.setText(displayName, juce::dontSendNotification);
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
        62,
        82,
        juce::roundToInt(static_cast<float>(getHeight()) * 0.115f)
    );
    headerArea = content.removeFromTop(headerHeight);
    content.removeFromTop(10);
    const auto navigationWidth = juce::jlimit(118, 152, getWidth() / 7);
    rackPanel = content.removeFromLeft(navigationWidth);
    content.removeFromLeft(12);
    auto workspace = content;
    masterPanel = {};

    // Global controls stay in the header on every workspace.
    auto headerControls = headerArea.reduced(8, 7).removeFromRight(244);
    auto dryWetBounds = headerControls.removeFromLeft(116).reduced(6, 0);
    auto outputGainBounds = headerControls.removeFromLeft(116).reduced(6, 0);
    dryWetLabel.setBounds(dryWetBounds.removeFromTop(16));
    dryWetSlider.setBounds(dryWetBounds);
    outputGainLabel.setBounds(outputGainBounds.removeFromTop(16));
    outputGainSlider.setBounds(outputGainBounds);
    dryWetLabel.setVisible(true);
    dryWetSlider.setVisible(true);
    outputGainLabel.setVisible(true);
    outputGainSlider.setVisible(true);

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
    masterSectionLabel.setVisible(!granularMode);
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
        const auto gap = juce::jlimit(12, 20, workspace.getWidth() / 65);
        const auto leftWidth = juce::roundToInt(static_cast<float>(workspace.getWidth() - gap) * 0.64f);
        grainControlPanel = workspace.removeFromLeft(leftWidth);
        workspace.removeFromLeft(gap);
        inspectorPanel = workspace;
        auto grainContent = grainControlPanel.reduced(10, 8);
        // The Grain Control row is a primary instrument surface, not a
        // footer.  Reserve enough vertical space for the layered rotary
        // controls to retain the same physical presence as the FX knobs.
        constexpr int minimumControlsHeight = 148;
        constexpr int minimumVisualizerHeight = 82;
        const auto availableHeight = grainContent.getHeight();
        const auto desiredXYHeight = juce::jlimit(
            180,
            430,
            juce::roundToInt(static_cast<float>(availableHeight) * 0.43f)
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
                    170,
                juce::jmin(
                    juce::roundToInt(static_cast<float>(grainContent.getHeight()) * 0.45f),
                    juce::jmax(minimumVisualizerHeight, grainContent.getHeight() - minimumControlsHeight)
                )
            )
        );
        granularVisualizer.setBounds(grainContent.removeFromTop(visualizerHeight));
        grainContent.removeFromTop(7);
        auto granularArea = grainContent;
        granularSectionLabel.setBounds(granularArea.removeFromTop(20));
        granularArea.removeFromTop(2);

        // All five primary controls share one common row.  Position used to
        // steal the bottom of its cell for the fresh/old hint, which made its
        // rotary and value box visibly lower than Density, Size and Pitch.
        // Reserve that hint as a separate strip instead, leaving identical
        // bounds for every control in the row.
        const auto positionHintStrip = granularArea.getHeight() > 22
            ? granularArea.removeFromBottom(16)
            : juce::Rectangle<int> {};
        const auto controlRow = granularArea;

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

        auto controlArea = cell.reduced(2, 0);

        if (granularControls[index] == &reverseButton)
        {
            granularControls[index]->setBounds(
                controlArea.withSizeKeepingCentre(
                    juce::jmin(84, juce::jmax(48, controlArea.getWidth() - 8)),
                    juce::jmin(54, controlArea.getHeight())
                )
            );
        }
        else
        {
            granularControls[index]->setBounds(controlArea);
        }
    }

        if (!positionHintStrip.isEmpty())
        {
            const auto positionX = controlRow.getX() + granularControlWidth * 2;
            const auto positionWidth = juce::jmin(
                granularControlWidth,
                controlRow.getRight() - positionX
            );
            positionHintLabel.setBounds(
                positionX + 5,
                positionHintStrip.getY(),
                juce::jmax(1, positionWidth - 10),
                positionHintStrip.getHeight()
            );
        }
        else
        {
            positionHintLabel.setBounds({});
        }
    }
    else
    {
        grainControlPanel = {};
        const auto mixWidth = juce::jlimit(116, 156, workspace.getWidth() / 6);
        // FX workspaces use one continuous surface.  Leaving a separate
        // spacer here exposed the editor background between the inspector
        // and the Mix panel, producing an unintended black vertical notch.
        masterPanel = workspace.removeFromRight(mixWidth);
        inspectorPanel = workspace;

        masterSectionLabel.setText("FX MIX", juce::dontSendNotification);
        auto mixArea = masterPanel.reduced(14, 14);
        masterSectionLabel.setBounds(mixArea.removeFromTop(24));
        mixArea.removeFromTop(10);
        const auto mixIndex = effectMixIndices[selectedEffect];
        inspectorLabels[mixIndex].setBounds(mixArea.removeFromTop(22));
        inspectorSliders[mixIndex].setBounds(mixArea.reduced(4, 0));
    }

    auto inspectorArea = inspectorPanel.reduced(16, 12);
    inspectorSectionLabel.setBounds(inspectorArea.removeFromTop(26));
    inspectorHintLabel.setBounds(inspectorArea.removeFromTop(18));
    inspectorArea.removeFromTop(8);
    if (granularMode)
    {
        const auto rowHeight = juce::jmax(34, inspectorArea.getHeight() / 8);
        const auto labelWidth = juce::jlimit(96, 132, inspectorPanel.getWidth() / 3);
        for (std::size_t i = 0; i < inspectorSliders.size(); ++i)
        {
            if (!inspectorSliders[i].isVisible())
                continue;
            auto row = inspectorArea.removeFromTop(rowHeight);
            inspectorLabels[i].setBounds(row.removeFromLeft(labelWidth));
            inspectorSliders[i].setBounds(row.reduced(3, 1));
        }
    }
    else
    {
        const auto mixIndex = effectMixIndices[selectedEffect];
        const auto parameterCount = effectParameterCounts[selectedEffect];
        const auto visibleCount = parameterCount > 0 ? parameterCount - 1 : 0;
        const auto columns = visibleCount <= 4 ? std::size_t { 2 }
            : (visibleCount <= 6 ? std::size_t { 3 } : std::size_t { 4 });
        const auto rows = static_cast<std::size_t>(juce::jmax(
            1,
            (static_cast<int>(visibleCount) + static_cast<int>(columns) - 1)
                / static_cast<int>(columns)
        ));
        const auto cellWidth = inspectorArea.getWidth() / static_cast<int>(columns);
        const auto cellHeight = inspectorArea.getHeight() / static_cast<int>(rows);
        std::size_t cellIndex = 0;
        for (std::size_t i = 0; i < parameterCount; ++i)
        {
            if (i == mixIndex || !inspectorSliders[i].isVisible())
                continue;
            const auto column = static_cast<int>(cellIndex % columns);
            const auto row = static_cast<int>(cellIndex / columns);
            auto cell = juce::Rectangle<int>(
                inspectorArea.getX() + column * cellWidth,
                inspectorArea.getY() + row * cellHeight,
                column + 1 == static_cast<int>(columns)
                    ? inspectorArea.getRight() - (inspectorArea.getX() + column * cellWidth)
                    : cellWidth,
                row + 1 == static_cast<int>(rows)
                    ? inspectorArea.getBottom() - (inspectorArea.getY() + row * cellHeight)
                    : cellHeight
            ).reduced(7, 4);
            inspectorLabels[i].setBounds(cell.removeFromTop(20));
            inspectorSliders[i].setBounds(cell);
            ++cellIndex;
        }
    }

    // Panel surfaces are painted by this parent component while the controls
    // themselves are child components.  A module switch can otherwise leave
    // parts of the previous surface in the dirty-region cache until the
    // window receives focus again.
    repaint();
}
