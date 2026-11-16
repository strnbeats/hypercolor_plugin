#include "HypercolorLookAndFeel.h"

#include <algorithm>
#include <cmath>

namespace hypercolor::ui
{
HypercolorLookAndFeel::HypercolorLookAndFeel()
{
    setColour(juce::Slider::textBoxBackgroundColourId, palette::background());
    setColour(juce::Slider::textBoxTextColourId, palette::text());
    setColour(juce::Slider::textBoxOutlineColourId, palette::border());
    setColour(juce::Label::textColourId, palette::text());
    setColour(juce::TextButton::buttonColourId, palette::panelRaised());
    setColour(juce::TextButton::buttonOnColourId, palette::pink());
    setColour(juce::TextButton::textColourOffId, palette::text());
    setColour(juce::TextButton::textColourOnId, palette::background());
}

void HypercolorLookAndFeel::drawRotarySlider(
    juce::Graphics& graphics,
    int x,
    int y,
    int width,
    int height,
    float sliderPosition,
    float rotaryStartAngle,
    float rotaryEndAngle,
    juce::Slider& slider
)
{
    const auto bounds = juce::Rectangle<float>(
        static_cast<float>(x),
        static_cast<float>(y),
        static_cast<float>(width),
        static_cast<float>(height)
    ).reduced(7.0f);
    const auto radius = juce::jmax(
        8.0f,
        std::min(bounds.getWidth(), bounds.getHeight()) * 0.5f
    );
    const auto centre = bounds.getCentre();
    const auto angle = rotaryStartAngle
        + sliderPosition * (rotaryEndAngle - rotaryStartAngle);
    const auto accent = slider.findColour(
        juce::Slider::rotarySliderFillColourId
    );

    for (int tick = 0; tick <= 10; ++tick)
    {
        const auto tickAngle = rotaryStartAngle
            + (rotaryEndAngle - rotaryStartAngle)
                * static_cast<float>(tick) / 10.0f;
        const auto inner = radius - 1.0f;
        const auto outer = radius + (tick % 5 == 0 ? 4.0f : 2.0f);
        const auto sinAngle = std::sin(tickAngle);
        const auto cosAngle = std::cos(tickAngle);

        graphics.setColour(
            tick <= juce::roundToInt(sliderPosition * 10.0f)
                ? accent.withAlpha(0.72f)
                : palette::border()
        );
        graphics.drawLine(
            centre.x + sinAngle * inner,
            centre.y - cosAngle * inner,
            centre.x + sinAngle * outer,
            centre.y - cosAngle * outer,
            tick % 5 == 0 ? 1.5f : 1.0f
        );
    }

    juce::Path track;
    track.addCentredArc(
        centre.x,
        centre.y,
        radius - 7.0f,
        radius - 7.0f,
        0.0f,
        rotaryStartAngle,
        rotaryEndAngle,
        true
    );
    graphics.setColour(palette::grid());
    graphics.strokePath(
        track,
        juce::PathStrokeType(5.0f, juce::PathStrokeType::curved)
    );

    juce::Path valueTrack;
    valueTrack.addCentredArc(
        centre.x,
        centre.y,
        radius - 7.0f,
        radius - 7.0f,
        0.0f,
        rotaryStartAngle,
        angle,
        true
    );
    graphics.setColour(accent);
    graphics.strokePath(
        valueTrack,
        juce::PathStrokeType(5.0f, juce::PathStrokeType::curved)
    );

    const auto knobRadius = juce::jmax(7.0f, radius - 17.0f);
    graphics.setColour(palette::panelRaised());
    graphics.fillEllipse(
        centre.x - knobRadius,
        centre.y - knobRadius,
        knobRadius * 2.0f,
        knobRadius * 2.0f
    );
    graphics.setColour(palette::border());
    graphics.drawEllipse(
        centre.x - knobRadius,
        centre.y - knobRadius,
        knobRadius * 2.0f,
        knobRadius * 2.0f,
        1.0f
    );

    const auto pointerLength = knobRadius - 4.0f;
    graphics.setColour(accent);
    graphics.drawLine(
        centre.x,
        centre.y,
        centre.x + std::sin(angle) * pointerLength,
        centre.y - std::cos(angle) * pointerLength,
        2.5f
    );
    graphics.fillEllipse(centre.x - 2.0f, centre.y - 2.0f, 4.0f, 4.0f);
}

void HypercolorLookAndFeel::drawButtonBackground(
    juce::Graphics& graphics,
    juce::Button& button,
    const juce::Colour& backgroundColour,
    bool shouldDrawButtonAsHighlighted,
    bool shouldDrawButtonAsDown
)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    const auto active = button.getToggleState();
    auto fill = active ? palette::pink() : backgroundColour;

    if (shouldDrawButtonAsDown)
        fill = fill.darker(0.18f);
    else if (shouldDrawButtonAsHighlighted)
        fill = fill.brighter(0.10f);

    juce::Path shape;
    constexpr float cut = 8.0f;
    shape.startNewSubPath(bounds.getX(), bounds.getY());
    shape.lineTo(bounds.getRight() - cut, bounds.getY());
    shape.lineTo(bounds.getRight(), bounds.getY() + cut);
    shape.lineTo(bounds.getRight(), bounds.getBottom());
    shape.lineTo(bounds.getX() + cut, bounds.getBottom());
    shape.lineTo(bounds.getX(), bounds.getBottom() - cut);
    shape.closeSubPath();

    graphics.setColour(fill);
    graphics.fillPath(shape);
    graphics.setColour(active ? palette::lime() : palette::border());
    graphics.strokePath(shape, juce::PathStrokeType(active ? 1.5f : 1.0f));
}

void HypercolorLookAndFeel::drawButtonText(
    juce::Graphics& graphics,
    juce::TextButton& button,
    bool,
    bool
)
{
    graphics.setFont(getTextButtonFont(button, button.getHeight()));
    graphics.setColour(
        button.findColour(
            button.getToggleState()
                ? juce::TextButton::textColourOnId
                : juce::TextButton::textColourOffId
        )
    );
    graphics.drawFittedText(
        button.getButtonText(),
        button.getLocalBounds().reduced(8, 2),
        juce::Justification::centred,
        1
    );
}

juce::Font HypercolorLookAndFeel::getTextButtonFont(
    juce::TextButton&,
    int buttonHeight
)
{
    return juce::Font {
        juce::FontOptions(
            juce::jlimit(10.0f, 13.0f, static_cast<float>(buttonHeight) * 0.30f)
        ).withStyle("Bold")
    };
}

juce::Font HypercolorLookAndFeel::getLabelFont(juce::Label& label)
{
    return label.getFont();
}
}
