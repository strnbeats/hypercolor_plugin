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
    ).reduced(8.0f);
    const auto radius = juce::jmax(
        8.0f,
        std::min(bounds.getWidth(), bounds.getHeight()) * 0.5f
    );
    const auto centre = bounds.getCentre();
    const auto angle = rotaryStartAngle
        + sliderPosition * (rotaryEndAngle - rotaryStartAngle);
    const auto baseAccent = slider.findColour(
        juce::Slider::rotarySliderFillColourId
    );
    const auto isActive = slider.isMouseButtonDown();
    const auto isHover = slider.isMouseOverOrDragging();
    const auto accent = isActive
        ? baseAccent.brighter(0.16f)
        : (isHover ? baseAccent.brighter(0.08f) : baseAccent);
    const auto compactControl = std::min(width, height) < 90;

    // The control is intentionally built in layers: a soft shadow, a dark
    // metal bezel, an inset surface and a separate illuminated scale.  This
    // keeps the knob readable at small sizes while giving the large Drive
    // controls a physical, instrument-like presence.
    const auto outerRadius = juce::jmax(10.0f, radius - 1.0f);
    const auto shadowRadius = outerRadius + 2.0f;
    graphics.setColour(juce::Colours::black.withAlpha(0.28f));
    graphics.fillEllipse(
        centre.x - shadowRadius,
        centre.y - shadowRadius + 3.0f,
        shadowRadius * 2.0f,
        shadowRadius * 2.0f
    );

    juce::ColourGradient bezelGradient(
        palette::panelRaised().brighter(0.16f),
        centre.x - outerRadius,
        centre.y - outerRadius,
        palette::background().brighter(0.03f),
        centre.x + outerRadius,
        centre.y + outerRadius,
        true
    );
    graphics.setGradientFill(bezelGradient);
    graphics.fillEllipse(
        centre.x - outerRadius,
        centre.y - outerRadius,
        outerRadius * 2.0f,
        outerRadius * 2.0f
    );
    graphics.setColour(palette::border().brighter(0.12f));
    graphics.drawEllipse(
        centre.x - outerRadius,
        centre.y - outerRadius,
        outerRadius * 2.0f,
        outerRadius * 2.0f,
        1.1f
    );

    const auto innerRadius = juce::jmax(8.0f, outerRadius - 6.0f);
    juce::ColourGradient surfaceGradient(
        palette::panelRaised().brighter(0.05f),
        centre.x,
        centre.y - innerRadius,
        palette::background().brighter(0.015f),
        centre.x,
        centre.y + innerRadius,
        false
    );
    graphics.setGradientFill(surfaceGradient);
    graphics.fillEllipse(
        centre.x - innerRadius,
        centre.y - innerRadius,
        innerRadius * 2.0f,
        innerRadius * 2.0f
    );
    graphics.setColour(palette::grid().withAlpha(0.95f));
    graphics.drawEllipse(
        centre.x - innerRadius,
        centre.y - innerRadius,
        innerRadius * 2.0f,
        innerRadius * 2.0f,
        1.0f
    );

    if (isHover || isActive)
    {
        graphics.setColour(accent.withAlpha(isActive ? 0.18f : 0.09f));
        graphics.drawEllipse(
            centre.x - outerRadius - 3.0f,
            centre.y - outerRadius - 3.0f,
            (outerRadius + 3.0f) * 2.0f,
            (outerRadius + 3.0f) * 2.0f,
            isActive ? 2.0f : 1.2f
        );
    }

    const auto tickCount = compactControl ? 12 : 16;
    for (int tick = 0; tick <= tickCount; ++tick)
    {
        const auto tickAngle = rotaryStartAngle
            + (rotaryEndAngle - rotaryStartAngle)
                * static_cast<float>(tick) / static_cast<float>(tickCount);
        const auto inner = outerRadius + 2.0f;
        const auto outer = inner + (tick % 4 == 0 ? 5.0f : 3.0f);
        const auto sinAngle = std::sin(tickAngle);
        const auto cosAngle = std::cos(tickAngle);

        graphics.setColour(
            tick <= juce::roundToInt(sliderPosition * static_cast<float>(tickCount))
                ? accent.withAlpha(0.82f)
                : palette::border().withAlpha(0.9f)
        );
        graphics.drawLine(
            centre.x + sinAngle * inner,
            centre.y - cosAngle * inner,
            centre.x + sinAngle * outer,
            centre.y - cosAngle * outer,
            tick % (compactControl ? 3 : 4) == 0 ? 1.7f : 1.0f
        );
    }

    juce::Path track;
    track.addCentredArc(
        centre.x,
        centre.y,
        outerRadius + 3.0f,
        outerRadius + 3.0f,
        0.0f,
        rotaryStartAngle,
        rotaryEndAngle,
        true
    );
    graphics.setColour(palette::grid().withAlpha(0.9f));
    graphics.strokePath(
        track,
        juce::PathStrokeType(compactControl ? 4.5f : 6.0f, juce::PathStrokeType::curved)
    );

    juce::Path valueTrack;
    valueTrack.addCentredArc(
        centre.x,
        centre.y,
        outerRadius + 3.0f,
        outerRadius + 3.0f,
        0.0f,
        rotaryStartAngle,
        angle,
        true
    );
    graphics.setColour(accent);
    graphics.strokePath(
        valueTrack,
        juce::PathStrokeType(
            compactControl ? (isActive ? 5.0f : 4.5f) : (isActive ? 7.0f : 6.0f),
            juce::PathStrokeType::curved
        )
    );

    const auto knobRadius = juce::jmax(
        8.0f,
        innerRadius - (compactControl ? 3.0f : 5.0f)
    );
    juce::ColourGradient knobGradient(
        palette::panelRaised().brighter(0.12f),
        centre.x - knobRadius * 0.7f,
        centre.y - knobRadius,
        palette::background().brighter(0.02f),
        centre.x + knobRadius * 0.7f,
        centre.y + knobRadius,
        true
    );
    graphics.setGradientFill(knobGradient);
    graphics.fillEllipse(
        centre.x - knobRadius,
        centre.y - knobRadius,
        knobRadius * 2.0f,
        knobRadius * 2.0f
    );
    graphics.setColour(palette::border().brighter(0.16f));
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
        compactControl ? (isActive ? 2.6f : 2.1f) : (isActive ? 3.4f : 2.7f)
    );
    graphics.setColour(palette::background().brighter(0.12f));
    graphics.fillEllipse(centre.x - 5.0f, centre.y - 5.0f, 10.0f, 10.0f);
    graphics.setColour(accent);
    graphics.fillEllipse(centre.x - 2.5f, centre.y - 2.5f, 5.0f, 5.0f);
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
    auto fill = active
        ? button.findColour(juce::TextButton::buttonOnColourId)
        : backgroundColour;

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
    graphics.setColour(
        active
            ? button.findColour(juce::TextButton::buttonOnColourId).brighter(0.18f)
            : palette::border()
    );
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
