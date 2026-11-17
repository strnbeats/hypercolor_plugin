#include "PremiumRotaryKnob.h"

#include "HypercolorLookAndFeel.h"

#include <algorithm>
#include <cmath>

namespace hypercolor::ui
{
PremiumRotaryKnob::PremiumRotaryKnob()
    : accentColour(palette::cyan()),
      arcStartColour(palette::cyan().darker(0.34f)),
      arcMiddleColour(palette::cyan()),
      arcEndColour(palette::cyan().brighter(0.44f)),
      arcContrastColour(palette::modulation()),
      secondaryAccentColour(palette::cyan().brighter(0.18f)),
      targetAccentColour(accentColour),
      targetArcStartColour(arcStartColour),
      targetArcMiddleColour(arcMiddleColour),
      targetArcEndColour(arcEndColour),
      targetArcContrastColour(arcContrastColour),
      targetSecondaryAccentColour(secondaryAccentColour)
{
    setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    setTextBoxStyle(juce::Slider::TextBoxBelow, false, 96, 25);
    setMouseDragSensitivity(180);
    setColour(juce::Slider::rotarySliderFillColourId, accentColour);
    setColour(juce::Slider::rotarySliderOutlineColourId, palette::grid());
    setColour(juce::Slider::thumbColourId, accentColour);
    setColour(juce::Slider::textBoxBackgroundColourId, palette::background());
    setColour(juce::Slider::textBoxTextColourId, palette::text());
    setColour(juce::Slider::textBoxOutlineColourId, palette::border());

    // Keep one continuous UI clock per control. The phase is derived from
    // elapsed wall-clock time, so parameter changes and mouse interaction do
    // not restart or quantise the two-second colour cycle.
    lastAnimationTimeMs = juce::Time::getMillisecondCounterHiRes();
    startTimerHz(30);
}

PremiumRotaryKnob::~PremiumRotaryKnob()
{
    stopTimer();
}

void PremiumRotaryKnob::setAccentColour(juce::Colour colour)
{
    accentColour = colour;
    arcStartColour = colour.darker(0.34f);
    arcMiddleColour = colour;
    arcEndColour = colour.brighter(0.44f);
    arcContrastColour = colour.interpolatedWith(juce::Colours::white, 0.42f);
    secondaryAccentColour = colour.brighter(0.18f);
    targetAccentColour = accentColour;
    targetArcStartColour = arcStartColour;
    targetArcMiddleColour = arcMiddleColour;
    targetArcEndColour = arcEndColour;
    targetArcContrastColour = arcContrastColour;
    targetSecondaryAccentColour = secondaryAccentColour;
    paletteTransition = 1.0f;
    setColour(juce::Slider::rotarySliderFillColourId, accentColour);
    setColour(juce::Slider::thumbColourId, accentColour);
    repaint();
}

void PremiumRotaryKnob::setSecondaryAccentColour(juce::Colour colour)
{
    secondaryAccentColour = colour;
    arcEndColour = colour;
    targetSecondaryAccentColour = colour;
    targetArcEndColour = colour;
    paletteTransition = 1.0f;
    repaint();
}

void PremiumRotaryKnob::setArcGradientColours(juce::Colour start,
                                              juce::Colour middle,
                                              juce::Colour end)
{
    arcStartColour = start;
    arcMiddleColour = middle;
    arcEndColour = end;
    secondaryAccentColour = end;
    targetArcStartColour = start;
    targetArcMiddleColour = middle;
    targetArcEndColour = end;
    targetSecondaryAccentColour = end;
    paletteTransition = 1.0f;
    repaint();
}

void PremiumRotaryKnob::setArcContrastColour(juce::Colour colour)
{
    arcContrastColour = colour;
    targetArcContrastColour = colour;
    paletteTransition = 1.0f;
    repaint();
}

void PremiumRotaryKnob::setTargetPalette(juce::Colour accent,
                                         juce::Colour start,
                                         juce::Colour middle,
                                         juce::Colour end,
                                         juce::Colour contrast)
{
    targetAccentColour = accent;
    targetArcStartColour = start;
    targetArcMiddleColour = middle;
    targetArcEndColour = end;
    targetSecondaryAccentColour = end;
    targetArcContrastColour = contrast;
    paletteTransition = 0.0f;
    repaint();
}

void PremiumRotaryKnob::setVisualScale(float scale)
{
    visualScale = juce::jlimit(0.95f, 1.15f, scale);
    repaint();
}

void PremiumRotaryKnob::mouseEnter(const juce::MouseEvent& event)
{
    juce::Slider::mouseEnter(event);
    interactionEnergy = juce::jmax(interactionEnergy, 0.28f);
    startTimerHz(30);
    repaint();
}

void PremiumRotaryKnob::mouseExit(const juce::MouseEvent& event)
{
    juce::Slider::mouseExit(event);
    startTimerHz(30);
    repaint();
}

void PremiumRotaryKnob::mouseDown(const juce::MouseEvent& event)
{
    interactionEnergy = 1.0f;
    startTimerHz(30);
    juce::Slider::mouseDown(event);
    repaint();
}

void PremiumRotaryKnob::mouseDrag(const juce::MouseEvent& event)
{
    interactionEnergy = juce::jmax(interactionEnergy, 0.86f);
    juce::Slider::mouseDrag(event);
    repaint();
}

void PremiumRotaryKnob::mouseUp(const juce::MouseEvent& event)
{
    juce::Slider::mouseUp(event);
    interactionEnergy = juce::jmax(interactionEnergy, 0.72f);
    startTimerHz(30);
    if (onInteractionFinished != nullptr)
        onInteractionFinished();
    repaint();
}

void PremiumRotaryKnob::timerCallback()
{
    const auto nowMs = juce::Time::getMillisecondCounterHiRes();
    const auto elapsedMs = juce::jmax(0.0, nowMs - lastAnimationTimeMs);
    lastAnimationTimeMs = nowMs;

    // Exactly one complete phase rotation per 2000 ms. Wrapping the phase
    // instead of resetting a frame counter keeps the loop seamless even when
    // a timer callback is delayed by the host or window server.
    constexpr double animationPeriodMs = 2000.0;
    arcPhase += static_cast<float>(elapsedMs / animationPeriodMs);
    arcPhase -= std::floor(arcPhase);

    const auto targetEnergy = isMouseButtonDown()
        ? 1.0f
        : (isMouseOverOrDragging() ? 0.28f : 0.0f);
    const auto smoothing = targetEnergy > interactionEnergy ? 0.34f : 0.18f;
    interactionEnergy += (targetEnergy - interactionEnergy) * smoothing;

    if (interactionEnergy < 0.008f && targetEnergy <= 0.0f)
        interactionEnergy = 0.0f;

    if (paletteTransition < 1.0f)
    {
        // The existing UI clock also owns theme transitions.  The phase of
        // the two-second arc animation is intentionally untouched here.
        paletteTransition = juce::jmin(1.0f, paletteTransition + 0.16f);
        const auto t = paletteTransition * paletteTransition
            * (3.0f - 2.0f * paletteTransition);
        const auto blend = [t](juce::Colour current, juce::Colour target)
        {
            return current.interpolatedWith(target, t);
        };
        accentColour = blend(accentColour, targetAccentColour);
        arcStartColour = blend(arcStartColour, targetArcStartColour);
        arcMiddleColour = blend(arcMiddleColour, targetArcMiddleColour);
        arcEndColour = blend(arcEndColour, targetArcEndColour);
        arcContrastColour = blend(arcContrastColour, targetArcContrastColour);
        secondaryAccentColour = blend(secondaryAccentColour,
                                      targetSecondaryAccentColour);
        setColour(juce::Slider::rotarySliderFillColourId, accentColour);
        setColour(juce::Slider::thumbColourId, accentColour);
    }

    // The timer intentionally remains active in Idle: the base colour
    // animation is a continuous part of the instrument surface, while the
    // interaction energy still controls the extra hover/drag emphasis.
    repaint();
}

void PremiumRotaryKnob::paint(juce::Graphics& graphics)
{
    const auto bounds = getLocalBounds().toFloat().reduced(3.0f);
    const auto compactControl = std::min(bounds.getWidth(), bounds.getHeight()) < 90.0f;
    const auto baseTextReserve = juce::jmin(28.0f, bounds.getHeight() * 0.34f);
    // Main Granular knobs use the spare space above their value box to grow
    // as a complete unit. The minimum keeps the JUCE text editor clear even
    // at the compact Master-control size.
    const auto textReserve = juce::jmax(22.5f, baseTextReserve / visualScale);
    // Keep a hard top safety inset inside the Slider itself. Thick arc
    // strokes must never enter the label's component bounds, even when the
    // parent layout is resized to a tight height.
    const auto topSafety = compactControl ? 8.0f : 10.0f;
    const auto knobArea = bounds.withTrimmedTop(topSafety)
                               .withTrimmedBottom(textReserve);
    const auto radius = juce::jmax(
        10.0f,
        std::min(knobArea.getWidth(), knobArea.getHeight()) * 0.5f - 2.0f
    );
    const auto centre = knobArea.getCentre();
    const auto range = getNormalisableRange();
    const auto valuePosition = juce::jlimit(
        0.0,
        1.0,
        range.end > range.start ? range.convertTo0to1(getValue()) : 0.0
    );
    constexpr auto startAngle = juce::MathConstants<float>::pi * 1.25f;
    constexpr auto endAngle = juce::MathConstants<float>::pi * 2.75f;
    const auto valueAngle = startAngle
        + static_cast<float>(valuePosition) * (endAngle - startAngle);
    const auto active = isMouseButtonDown();
    const auto hovered = isMouseOverOrDragging();
    const auto interaction = juce::jlimit(
        0.0f,
        1.0f,
        juce::jmax(interactionEnergy, active ? 1.0f : (hovered ? 0.28f : 0.0f))
    );
    const auto accent = accentColour.brighter(0.04f + interaction * 0.16f);
    const auto highlight = secondaryAccentColour.brighter(interaction * 0.12f);
    const auto shadowAccent = accentColour.darker(0.52f);

    // A compact shadow and bevel establish depth without making the control
    // look like a flat painted circle.
    const auto shadowRadius = radius + 3.0f + interaction * 1.5f;
    graphics.setColour(juce::Colours::black.withAlpha(0.28f + interaction * 0.12f));
    graphics.fillEllipse(centre.x - shadowRadius,
                         centre.y - shadowRadius + 3.0f,
                         shadowRadius * 2.0f,
                         shadowRadius * 2.0f);

    juce::ColourGradient bezel(
        highlight.interpolatedWith(palette::panelRaised().brighter(0.20f), 0.46f),
        centre.x - radius,
        centre.y - radius,
        shadowAccent.interpolatedWith(palette::background().brighter(0.02f), 0.34f),
        centre.x + radius,
        centre.y + radius,
        true
    );
    graphics.setGradientFill(bezel);
    graphics.fillEllipse(centre.x - radius,
                         centre.y - radius,
                         radius * 2.0f,
                         radius * 2.0f);
    graphics.setColour(highlight.withAlpha(0.34f + interaction * 0.18f));
    graphics.drawEllipse(centre.x - radius,
                         centre.y - radius,
                         radius * 2.0f,
                         radius * 2.0f,
                         1.0f + interaction * 0.45f);
    graphics.setColour(shadowAccent.withAlpha(0.5f));
    juce::Path shadowBevel;
    shadowBevel.addCentredArc(centre.x,
                              centre.y,
                              radius - 1.0f,
                              radius - 1.0f,
                              0.0f,
                              0.28f,
                              2.48f,
                              true);
    graphics.strokePath(shadowBevel, juce::PathStrokeType(1.15f));

    const auto trackRadius = radius + 2.0f;
    juce::Path track;
    track.addCentredArc(centre.x, centre.y, trackRadius, trackRadius, 0.0f,
                        startAngle, endAngle, true);
    graphics.setColour(palette::grid().withAlpha(0.95f));
    graphics.strokePath(track,
                         juce::PathStrokeType((compactControl ? 6.0f : 6.5f)
                                                  + interaction * 0.9f,
                                              juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));

    // JUCE's Graphics API does not provide a gradient stroke along an arc.
    // Subdivide the bounded active path into small curved segments instead.
    // This keeps the endpoint exactly locked to the Slider value while making
    // the colour transition travel along the arc rather than recolouring the
    // whole stroke at once.
    const auto arcStart = arcStartColour.brighter(interaction * 0.10f);
    const auto arcMid = arcMiddleColour.brighter(interaction * 0.10f);
    const auto arcEnd = arcEndColour.brighter(interaction * 0.08f);
    const auto contrast = arcContrastColour.brighter(interaction * 0.12f);
    // A sinusoidal travel phase creates a seamless 2 s colour sweep. The
    // accent is constrained to the active part of the arc, so low values do
    // not produce a bright colour outside the Slider's actual fill.
    const auto phaseWave = 0.5f
        + 0.5f * std::sin(arcPhase * juce::MathConstants<float>::twoPi);
    const auto activeSpan = static_cast<float>(valuePosition);
    const auto accentTravel = juce::jlimit(
        0.04f,
        juce::jmax(0.04f, activeSpan - 0.025f),
        0.04f + activeSpan * (0.16f + phaseWave * 0.72f)
    );
    const auto accentWidth = 0.075f + interaction * 0.035f;
    const auto arcColourAt = [
        &arcStart,
        &arcMid,
        &arcEnd,
        &contrast,
        accentTravel,
        accentWidth,
        interaction
    ](float position)
    {
        const auto t = juce::jlimit(0.0f, 1.0f, position);
        auto base = t < 0.5f
            ? arcStart.interpolatedWith(arcMid, t * 2.0f)
            : arcMid.interpolatedWith(arcEnd, (t - 0.5f) * 2.0f);

        const auto distance = std::abs(t - accentTravel);
        const auto accentWeight = std::exp(
            -(distance * distance)
                / (2.0f * accentWidth * accentWidth)
        ) * (0.20f + interaction * 0.42f);
        base = base.interpolatedWith(
            contrast,
            juce::jlimit(0.0f, 0.64f, accentWeight)
        );

        return base;
    };

    constexpr int arcSegments = 72;
    const auto activeStroke = (compactControl ? 7.4f : 9.0f)
        + interaction * (compactControl ? 0.85f : 1.55f);
    const auto glowStroke = (compactControl ? 9.8f : 11.8f)
        + interaction * 3.0f;
    if (valuePosition > 0.00001)
    {
        for (int segmentIndex = 0; segmentIndex < arcSegments; ++segmentIndex)
        {
            const auto segmentStart = static_cast<float>(segmentIndex)
                / static_cast<float>(arcSegments);
            const auto segmentEnd = juce::jmin(
                static_cast<float>(valuePosition),
                static_cast<float>(segmentIndex + 1)
                    / static_cast<float>(arcSegments)
            );
            if (segmentStart >= static_cast<float>(valuePosition)
                || segmentEnd <= segmentStart)
                break;

            juce::Path segment;
            segment.addCentredArc(
                centre.x,
                centre.y,
                trackRadius,
                trackRadius,
                0.0f,
                startAngle + (endAngle - startAngle) * segmentStart,
                startAngle + (endAngle - startAngle) * segmentEnd,
                true
            );

            const auto segmentColour = arcColourAt(
                (segmentStart + segmentEnd) * 0.5f
            );
            if (interaction > 0.01f)
            {
                graphics.setColour(segmentColour.withAlpha(
                    0.07f + interaction * 0.14f
                ));
                graphics.strokePath(
                    segment,
                    juce::PathStrokeType(glowStroke,
                                         juce::PathStrokeType::curved,
                                         juce::PathStrokeType::rounded)
                );
            }

            graphics.setColour(segmentColour.withAlpha(
                0.99f + interaction * 0.01f
            ));
            graphics.strokePath(
                segment,
                juce::PathStrokeType(activeStroke,
                                     juce::PathStrokeType::curved,
                                     juce::PathStrokeType::rounded)
            );

            // A restrained inner highlight keeps the three colour stops
            // legible at compact sizes without turning the arc into a neon
            // outline.
            graphics.setColour(segmentColour.brighter(0.10f).withAlpha(
                0.30f + interaction * 0.14f
            ));
            graphics.strokePath(
                segment,
                juce::PathStrokeType(1.15f + interaction * 0.2f,
                                     juce::PathStrokeType::curved,
                                     juce::PathStrokeType::rounded)
            );
        }
    }

    // A compact light bead marks the live end of the value arc, making the
    // current position readable even when the knob is viewed at a small size.
    const auto bead = centre + juce::Point<float>(std::sin(valueAngle),
                                                   -std::cos(valueAngle))
        * trackRadius;
    graphics.setColour(highlight.withAlpha(0.32f + interaction * 0.34f));
    graphics.fillEllipse(bead.x - (1.4f + interaction),
                         bead.y - (1.4f + interaction),
                         (2.8f + interaction * 2.0f),
                         (2.8f + interaction * 2.0f));

    constexpr int tickCount = 14;
    for (int tick = 0; tick <= tickCount; ++tick)
    {
        const auto tickAngle = startAngle
            + (endAngle - startAngle) * static_cast<float>(tick) / static_cast<float>(tickCount);
        const auto direction = juce::Point<float>(std::sin(tickAngle), -std::cos(tickAngle));
        const auto inner = radius + 4.0f;
        const auto outer = inner + (tick % 4 == 0 ? 4.0f : 2.2f);
        const auto lit = static_cast<float>(tick) <= static_cast<float>(valuePosition) * tickCount;
        graphics.setColour((lit ? highlight : palette::border()).withAlpha(lit ? 0.82f : 0.72f));
        graphics.drawLine(centre.x + direction.x * inner,
                          centre.y + direction.y * inner,
                          centre.x + direction.x * outer,
                          centre.y + direction.y * outer,
                          tick % 4 == 0 ? 1.35f : 0.8f);
    }

    const auto innerRadius = juce::jmax(8.0f, radius - 7.0f);
    juce::ColourGradient surface(
        palette::panelRaised().brighter(0.10f + interaction * 0.12f)
            .interpolatedWith(accentColour.darker(0.42f), 0.18f + interaction * 0.12f),
        centre.x - innerRadius * 0.6f,
        centre.y - innerRadius,
        shadowAccent.interpolatedWith(palette::background().brighter(0.015f), 0.28f),
        centre.x + innerRadius * 0.6f,
        centre.y + innerRadius,
        true
    );
    graphics.setGradientFill(surface);
    graphics.fillEllipse(centre.x - innerRadius,
                         centre.y - innerRadius,
                         innerRadius * 2.0f,
                         innerRadius * 2.0f);
    graphics.setColour(highlight.withAlpha(0.26f + interaction * 0.24f));
    graphics.drawEllipse(centre.x - innerRadius,
                         centre.y - innerRadius,
                         innerRadius * 2.0f,
                         innerRadius * 2.0f,
                         1.0f + interaction * 0.35f);
    graphics.setColour(palette::grid().withAlpha(0.84f));
    graphics.drawEllipse(centre.x - innerRadius + 2.0f,
                         centre.y - innerRadius + 2.0f,
                         innerRadius * 2.0f - 4.0f,
                         innerRadius * 2.0f - 4.0f,
                         0.65f);

    juce::ColourGradient wellGlow(
        accent.withAlpha(0.12f + interaction * 0.12f),
        centre.x - innerRadius * 0.25f,
        centre.y - innerRadius * 0.58f,
        accentColour.darker(0.5f).withAlpha(0.0f),
        centre.x + innerRadius * 0.8f,
        centre.y + innerRadius * 0.75f,
        true
    );
    graphics.setGradientFill(wellGlow);
    graphics.fillEllipse(centre.x - innerRadius * 0.82f,
                         centre.y - innerRadius * 0.82f,
                         innerRadius * 1.64f,
                         innerRadius * 1.64f);

    const auto pointerLength = innerRadius - 5.0f;
    graphics.setColour(accent.withAlpha(0.88f + interaction * 0.12f));
    graphics.drawLine(centre.x,
                      centre.y,
                      centre.x + std::sin(valueAngle) * pointerLength,
                      centre.y - std::cos(valueAngle) * pointerLength,
                      2.35f + interaction * 0.85f);
    graphics.setColour(palette::background().brighter(0.14f));
    graphics.fillEllipse(centre.x - 4.5f, centre.y - 4.5f, 9.0f, 9.0f);
    graphics.setColour(highlight);
    graphics.fillEllipse(centre.x - 2.3f, centre.y - 2.3f, 4.6f, 4.6f);
    graphics.setColour(palette::text().withAlpha(0.5f + interaction * 0.32f));
    graphics.fillEllipse(centre.x - innerRadius * 0.28f,
                         centre.y - innerRadius * 0.42f,
                         2.0f,
                         2.0f);
}
}
