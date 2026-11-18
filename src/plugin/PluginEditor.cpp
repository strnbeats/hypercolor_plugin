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

constexpr std::array<const char*, 8> effectEnableParameterIDs {
    hypercolor::ParameterIDs::reverbEnabled,
    hypercolor::ParameterIDs::modulationEnabled,
    hypercolor::ParameterIDs::driveEnabled,
    hypercolor::ParameterIDs::tapeEnabled,
    hypercolor::ParameterIDs::bitcrushEnabled,
    hypercolor::ParameterIDs::filterEnabled,
    hypercolor::ParameterIDs::compressorEnabled,
    hypercolor::ParameterIDs::delayEnabled
};

constexpr float fxLineOpacityScale = 1.10f;

float fxLineAlpha(float alpha) noexcept
{
    return juce::jlimit(0.0f, 1.0f, alpha * fxLineOpacityScale);
}

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
    juce::Colour accent,
    const std::array<float, 9>& visualDeltas
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

    const auto amountDelta = visualDeltas[1];
    const auto toneDelta = visualDeltas[2];
    const auto mixDelta = visualDeltas[3];
    const auto outputDelta = visualDeltas[4];
    const auto modeDelta = visualDeltas[0];
    const auto glowIntensity = juce::jlimit(0.012f, 0.10f,
        0.035f + amountDelta * 0.032f + mixDelta * 0.018f
            + outputDelta * 0.010f);
    const auto focus = bounds.withSizeKeepingCentre(
        bounds.getWidth() * 0.78f * (1.0f + toneDelta * 0.14f
                                     + modeDelta * 0.035f),
        bounds.getHeight() * 0.68f
    );
    graphics.setColour(accent.withAlpha(fxLineAlpha(glowIntensity)));
    graphics.fillEllipse(focus);
    graphics.setColour(accent.withAlpha(fxLineAlpha(juce::jlimit(
        0.04f, 0.24f, 0.10f + amountDelta * 0.12f
    ))));
    graphics.drawEllipse(focus.reduced(1.0f), 1.0f);

    const auto axisY = bounds.getCentreY();
    graphics.setColour(accent.withAlpha(fxLineAlpha(juce::jlimit(
        0.08f, 0.32f, 0.16f + amountDelta * 0.12f
    ))));
    graphics.drawLine(bounds.getX() + 26.0f, axisY, bounds.getRight() - 26.0f, axisY, 1.0f);
    graphics.setColour(hypercolor::ui::palette::grid().withAlpha(fxLineAlpha(0.45f)));
    graphics.drawLine(bounds.getX() + 26.0f, axisY - 1.0f, bounds.getRight() - 26.0f, axisY - 1.0f, 1.0f);

    const auto railX = bounds.getX() + bounds.getWidth() * 0.72f;
    graphics.setColour(accent.withAlpha(fxLineAlpha(juce::jlimit(
        0.08f, 0.34f, 0.18f + amountDelta * 0.14f
    ))));
    graphics.fillRect(railX, bounds.getY() + 18.0f, 2.0f, bounds.getHeight() - 36.0f);
    graphics.setColour(accent.withAlpha(fxLineAlpha(juce::jlimit(
        0.18f, 0.70f, 0.42f + amountDelta * 0.24f
    ))));
    graphics.fillRect(railX - 5.0f, bounds.getCentreY() - 1.0f, 12.0f, 2.0f);

    const auto corner = juce::Rectangle<float>(
        bounds.getX() + 18.0f,
        bounds.getBottom() - 34.0f,
        92.0f,
        1.0f
    );
    graphics.setColour(accent.withAlpha(fxLineAlpha(0.55f)));
    graphics.fillRect(corner);
    graphics.setColour(hypercolor::ui::palette::text().withAlpha(fxLineAlpha(0.18f)));
    graphics.fillRect(corner.withX(corner.getX() + 102.0f).withWidth(32.0f));
}

void drawEffectBackdrop(
    juce::Graphics& graphics,
    juce::Rectangle<int> integerBounds,
    std::size_t effectIndex,
    juce::Colour accent,
    const std::array<float, 9>& visualDeltas
)
{
    if (effectIndex == 2)
    {
        drawDriveBackdrop(graphics, integerBounds, accent, visualDeltas);
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
    const auto delta = [&visualDeltas](std::size_t index)
    {
        return juce::jlimit(-1.0f, 1.0f, visualDeltas[index]);
    };

    switch (effectIndex)
    {
        case 0: // SPACE: concentric depth fields and orbital reflections.
        {
            const auto spaceCentre = centre.translated(bounds.getWidth() * 0.08f, 0.0f);
            const auto mixDelta = delta(0);
            const auto sizeDelta = delta(1);
            const auto decayDelta = delta(2);
            const auto dampingDelta = delta(3);
            const auto preDelayDelta = delta(4);
            const auto widthDelta = delta(5);
            for (int ring = 0; ring < 5; ++ring)
            {
                const auto scale = 0.18f + static_cast<float>(ring) * 0.14f;
                const auto ellipse = juce::Rectangle<float>(
                    bounds.getWidth() * scale
                        * (1.0f + sizeDelta * 0.24f + widthDelta * 0.14f),
                    bounds.getHeight() * scale * 0.62f
                        * (1.0f + sizeDelta * 0.14f)
                ).withCentre(spaceCentre.translated(
                    0.0f,
                    preDelayDelta * bounds.getHeight() * 0.035f
                ));
                const auto ringAlpha = (0.035f + static_cast<float>(ring) * 0.012f)
                    * (1.0f + decayDelta * 0.38f + dampingDelta * 0.20f
                       + mixDelta * 0.16f);
                graphics.setColour(accent.withAlpha(fxLineAlpha(
                    juce::jlimit(0.0f, 0.22f, ringAlpha)
                )));
                graphics.drawEllipse(ellipse, ring == 4 ? 1.4f : 0.8f);
            }

            for (int arc = 0; arc < 4; ++arc)
            {
                juce::Path path;
                const auto radius = bounds.getHeight()
                    * (0.24f + static_cast<float>(arc) * 0.075f)
                    * (1.0f + sizeDelta * 0.16f);
                path.addCentredArc(
                    spaceCentre.x,
                    spaceCentre.y,
                    radius * 1.35f,
                    radius * 0.54f * (1.0f + widthDelta * 0.18f),
                    0.0f,
                    -1.2f + static_cast<float>(arc) * 0.08f,
                    1.0f + static_cast<float>(arc) * 0.12f,
                    true
                );
                const auto arcAlpha = (0.10f - static_cast<float>(arc) * 0.012f)
                    * (1.0f + decayDelta * 0.45f + dampingDelta * 0.18f
                       + mixDelta * 0.22f);
                graphics.setColour(accent.withAlpha(fxLineAlpha(juce::jlimit(
                    0.0f, 0.32f, arcAlpha
                ))));
                graphics.strokePath(path, juce::PathStrokeType(1.2f));
            }
            break;
        }

        case 1: // MODULATION: phase-shifted interference fields.
        {
            const auto modeDelta = delta(0);
            const auto rateDelta = delta(1);
            const auto depthDelta = delta(2);
            const auto delayDelta = delta(3);
            const auto feedbackDelta = delta(4);
            const auto mixDelta = delta(5);
            const auto widthDelta = delta(6);
            for (int wave = 0; wave < 4; ++wave)
            {
                juce::Path path;
                const auto phase = static_cast<float>(wave) * 0.72f
                    + modeDelta * static_cast<float>(wave) * 0.12f;
                const auto amplitude = bounds.getHeight()
                    * (0.045f + static_cast<float>(wave) * 0.012f)
                    * (1.0f + depthDelta * 0.52f);
                for (int sample = 0; sample <= 72; ++sample)
                {
                    const auto x = bounds.getX() + bounds.getWidth()
                        * (0.5f + (static_cast<float>(sample) / 72.0f - 0.5f)
                           * (1.0f + widthDelta * 0.12f));
                    const auto t = static_cast<float>(sample) / 72.0f;
                    const auto y = midY + delayDelta * bounds.getHeight() * 0.025f
                        + std::sin(t * pi * 3.2f * (1.0f + rateDelta * 0.34f)
                                   + phase) * amplitude
                        + std::sin(t * pi * 7.0f * (1.0f + rateDelta * 0.24f)
                                   - phase) * amplitude * 0.24f;
                    if (sample == 0)
                        path.startNewSubPath(x, y);
                    else
                        path.lineTo(x, y);
                }
                const auto waveAlpha = (0.08f - static_cast<float>(wave) * 0.012f)
                    * (1.0f + feedbackDelta * 0.34f + mixDelta * 0.30f);
                graphics.setColour(accent.withAlpha(fxLineAlpha(juce::jlimit(
                    0.0f, 0.24f, waveAlpha
                ))));
                graphics.strokePath(path, juce::PathStrokeType(wave == 0 ? 1.5f : 0.8f));
            }
            graphics.setColour(accent.withAlpha(fxLineAlpha(0.18f)));
            graphics.drawLine(bounds.getX() + 26.0f, midY, bounds.getRight() - 26.0f, midY, 1.0f);
            break;
        }

        case 3: // TAPE: slow, non-periodic magnetic drift lines.
        {
            const auto wowDelta = delta(1);
            const auto flutterDelta = delta(2);
            const auto wearDelta = delta(3);
            const auto driveDelta = delta(0);
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
                            * (1.0f + wowDelta * 0.55f)
                        + std::sin(t * pi * 4.0f + static_cast<float>(line))
                            * bounds.getHeight() * 0.008f
                            * (1.0f + flutterDelta * 0.9f);
                    if (sample == 0)
                        path.startNewSubPath(x, y);
                    else
                        path.lineTo(x, y);
                }
                const auto lineAlpha = (0.07f + (line == 2 ? 0.05f : 0.0f))
                    * (1.0f + wearDelta * 0.40f + driveDelta * 0.18f
                       + delta(4) * 0.20f + delta(5) * 0.12f);
                graphics.setColour(accent.withAlpha(fxLineAlpha(juce::jlimit(
                    0.0f, 0.24f, lineAlpha
                ))));
                graphics.strokePath(path, juce::PathStrokeType(line == 2 ? 1.5f : 0.8f));
            }
            graphics.setColour(accent.withAlpha(fxLineAlpha(juce::jlimit(
                0.06f, 0.34f, 0.16f + wearDelta * 0.14f
            ))));
            graphics.fillRect(bounds.getX() + 22.0f, bounds.getBottom() - 32.0f,
                              bounds.getWidth() * 0.34f * (1.0f + driveDelta * 0.12f),
                              2.0f);
            break;
        }

        case 4: // BITCRUSH: stepped quantisation lattice.
        {
            const auto steps = 12;
            const auto degradation = juce::jlimit(
                -0.7f, 0.9f, -delta(0) * 0.54f - delta(1) * 0.28f
                    + delta(2) * 0.14f - delta(3) * 0.18f
                    + delta(4) * 0.10f
            );
            const auto stepWidth = bounds.getWidth() / static_cast<float>(steps);
            for (int step = 0; step < steps; ++step)
            {
                const auto x = bounds.getX()
                    + static_cast<float>(step) * stepWidth;
                const auto h = bounds.getHeight()
                    * (0.12f + (step % 4) * 0.045f)
                    * (1.0f + degradation * 0.42f);
                graphics.setColour(accent.withAlpha(fxLineAlpha(juce::jlimit(
                    0.0f, 0.12f, 0.035f * (1.0f + degradation * 0.72f)
                ))));
                graphics.fillRect(x, midY - h, stepWidth - 2.0f, h * 2.0f);
            }

            juce::Path staircase;
            staircase.startNewSubPath(bounds.getX() + 20.0f, midY + bounds.getHeight() * 0.16f);
            for (int step = 0; step < steps; ++step)
            {
                const auto x = bounds.getX() + 20.0f
                    + static_cast<float>(step) * stepWidth;
                const auto level = (step % 5) * bounds.getHeight() * 0.045f
                    * (1.0f + degradation * 0.55f);
                staircase.lineTo(x, midY - level);
                staircase.lineTo(x + stepWidth * 0.72f, midY - level);
            }
            graphics.setColour(accent.withAlpha(fxLineAlpha(juce::jlimit(
                0.06f, 0.34f, 0.18f + degradation * 0.13f
            ))));
            graphics.strokePath(staircase, juce::PathStrokeType(1.4f));
            graphics.setColour(accent.withAlpha(fxLineAlpha(juce::jlimit(
                0.08f, 0.38f, 0.23f + degradation * 0.12f
            ))));
            graphics.drawHorizontalLine(juce::roundToInt(midY), bounds.getX() + 20.0f, bounds.getRight() - 20.0f);
            break;
        }

        case 5: // FILTER: restrained frequency curve with a resonant peak.
        {
            const auto cutoffDelta = delta(1);
            const auto resonanceDelta = delta(2);
            const auto peakCentre = juce::jlimit(
                0.18f, 0.84f, 0.62f + cutoffDelta * 0.20f
                    + delta(0) * 0.07f + delta(3) * 0.04f
            );
            const auto peakWidth = 0.12f * (1.0f + resonanceDelta * 0.48f
                                            + delta(5) * 0.16f);
            const auto peakHeight = 0.23f * (1.0f + resonanceDelta * 0.58f
                + delta(3) * 0.32f + delta(4) * 0.22f);
            juce::Path curve;
            for (int sample = 0; sample <= 80; ++sample)
            {
                const auto t = static_cast<float>(sample) / 80.0f;
                const auto x = bounds.getX() + 22.0f + t * (bounds.getWidth() - 44.0f);
                const auto peak = std::exp(-std::pow(
                    (t - peakCentre) / peakWidth, 2.0f
                ));
                const auto slope = 0.26f - t * 0.20f;
                const auto y = bounds.getBottom() - 42.0f
                    - (slope + peak * peakHeight) * bounds.getHeight();
                if (sample == 0)
                    curve.startNewSubPath(x, y);
                else
                    curve.lineTo(x, y);
            }
            graphics.setColour(accent.withAlpha(fxLineAlpha(juce::jlimit(
                0.08f, 0.34f, 0.19f + resonanceDelta * 0.08f
                    + delta(4) * 0.08f + delta(5) * 0.05f
            ))));
            graphics.strokePath(curve, juce::PathStrokeType(1.7f));
            graphics.setColour(accent.withAlpha(fxLineAlpha(juce::jlimit(
                0.04f, 0.24f, 0.09f + resonanceDelta * 0.08f
            ))));
            graphics.drawVerticalLine(
                juce::roundToInt(bounds.getX() + bounds.getWidth() * peakCentre),
                bounds.getY() + 22.0f,
                bounds.getBottom() - 30.0f
            );
            break;
        }

        case 6: // DYNAMICS: envelope, threshold and recovery curve.
        {
            const auto thresholdDelta = delta(0);
            const auto ratioDelta = delta(1);
            const auto attackDelta = delta(2);
            const auto releaseDelta = delta(3);
            const auto compression = juce::jlimit(
                -0.7f, 0.7f, ratioDelta * 0.48f - thresholdDelta * 0.28f
                    + delta(4) * 0.18f + delta(5) * 0.14f
            );
            const auto attackX = 0.24f + attackDelta * 0.055f;
            const auto recoveryX = 0.77f - releaseDelta * 0.055f;
            const auto envelopeTop = 0.28f + compression * 0.08f;
            juce::Path envelope;
            envelope.startNewSubPath(bounds.getX() + 22.0f, bounds.getBottom() - 38.0f);
            envelope.cubicTo(
                bounds.getX() + bounds.getWidth() * attackX,
                bounds.getBottom() - bounds.getHeight() * 0.60f,
                bounds.getX() + bounds.getWidth() * (0.34f + attackDelta * 0.04f),
                bounds.getY() + bounds.getHeight() * 0.23f,
                bounds.getX() + bounds.getWidth() * 0.52f,
                bounds.getY() + bounds.getHeight() * envelopeTop
            );
            envelope.cubicTo(
                bounds.getX() + bounds.getWidth() * (0.70f + releaseDelta * 0.035f),
                bounds.getY() + bounds.getHeight() * (envelopeTop + 0.02f),
                bounds.getX() + bounds.getWidth() * recoveryX,
                bounds.getBottom() - bounds.getHeight() * 0.36f,
                bounds.getRight() - 22.0f,
                bounds.getBottom() - 38.0f
            );
            graphics.setColour(accent.withAlpha(fxLineAlpha(juce::jlimit(
                0.08f, 0.32f, 0.18f + std::abs(compression) * 0.06f
                    + delta(6) * 0.05f + delta(7) * 0.03f
            ))));
            graphics.strokePath(envelope, juce::PathStrokeType(1.8f));
            const auto thresholdY = bounds.getY() + bounds.getHeight()
                * (0.46f - thresholdDelta * 0.12f);
            constexpr float thresholdDashes[] { 5.0f, 4.0f };
            graphics.setColour(accent.withAlpha(fxLineAlpha(juce::jlimit(
                0.05f, 0.26f, 0.13f + std::abs(thresholdDelta) * 0.09f
            ))));
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
            const auto timeDelta = delta(2);
            const auto divisionDelta = delta(3);
            const auto feedbackDelta = delta(4);
            const auto lowCutDelta = delta(5);
            const auto highCutDelta = delta(6);
            const auto mixDelta = delta(7);
            const auto widthDelta = delta(8);
            const auto modeDelta = delta(0);
            const auto syncDelta = delta(1);
            const auto echoSpread = juce::jlimit(0.72f, 1.22f,
                1.0f + timeDelta * 0.18f + divisionDelta * 0.08f
                    + widthDelta * 0.06f + modeDelta * 0.035f
                    + syncDelta * 0.025f);
            const auto railY = bounds.getBottom() - 48.0f;
            graphics.setColour(accent.withAlpha(fxLineAlpha(0.14f)));
            graphics.drawLine(bounds.getX() + 22.0f, railY, bounds.getRight() - 22.0f, railY, 1.0f);
            for (int echo = 0; echo < 8; ++echo)
            {
                const auto t = static_cast<float>(echo + 1) / 9.0f;
                const auto baseX = bounds.getX() + 32.0f
                    + t * (bounds.getWidth() - 64.0f);
                const auto x = juce::jlimit(
                    bounds.getX() + 20.0f,
                    bounds.getRight() - 34.0f,
                    centre.x + (baseX - centre.x) * echoSpread
                );
                const auto h = bounds.getHeight()
                    * (0.32f - static_cast<float>(echo) * 0.028f)
                    * (1.0f + feedbackDelta * 0.38f
                       + (lowCutDelta + highCutDelta) * 0.08f);
                graphics.setColour(accent.withAlpha(fxLineAlpha(
                    juce::jlimit(0.0f, 0.36f,
                        (0.17f - static_cast<float>(echo) * 0.016f)
                            * (1.0f + feedbackDelta * 0.44f + mixDelta * 0.38f))
                )));
                graphics.fillRect(x, railY - h, echo == 0 ? 3.0f : 2.0f, h);
                graphics.setColour(accent.withAlpha(fxLineAlpha(juce::jlimit(
                    0.0f, 0.22f, 0.08f * (1.0f + feedbackDelta * 0.46f
                                             + mixDelta * 0.32f)
                ))));
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
      ),
      tooltipWindow(this, 500)
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

    const auto configurePremiumKnob = [
        this,
        text
    ](
        hypercolor::ui::PremiumRotaryKnob& knob,
        juce::String suffix,
        juce::Colour accent
    )
    {
        knob.setAccentColour(accent);
        knob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 96, 25);
        knob.setMouseDragSensitivity(180);
        knob.setTextValueSuffix(std::move(suffix));
        knob.setColour(juce::Slider::textBoxBackgroundColourId,
                       hypercolor::ui::palette::background());
        knob.setColour(juce::Slider::textBoxTextColourId, text);
        knob.setColour(juce::Slider::textBoxOutlineColourId,
                       hypercolor::ui::palette::border());
        addAndMakeVisible(knob);
    };

    // The header controls share the Premium rotary language, with a compact
    // footprint and their own global accent colours.
    configurePremiumKnob(dryWetSlider, " %", lime);
    configurePremiumKnob(outputGainSlider, " dB", cyan);
    configurePremiumKnob(densitySlider, " gr/s", cyan);
    configurePremiumKnob(sizeSlider, " ms", pink);
    configurePremiumKnob(positionSlider, {}, lime);
    configurePremiumKnob(pitchSlider, {}, hypercolor::ui::palette::space());

    // Give the four main Granular controls a slightly larger visual presence
    // while leaving the compact Header controls at their existing scale.
    densitySlider.setVisualScale(1.09f);
    sizeSlider.setVisualScale(1.09f);
    positionSlider.setVisualScale(1.09f);
    pitchSlider.setVisualScale(1.09f);

    // Explicit three-stop arc palettes keep the colour travel visible at
    // rest, not only during hover.  Each one stays within the identity of its
    // control while giving the active arc a clear dark -> vivid -> light path.
    densitySlider.setArcGradientColours(
        juce::Colour(0xff005967u),
        juce::Colour(0xff00e5ffu),
        juce::Colour(0xffd7fdffu)
    );
    densitySlider.setArcContrastColour(juce::Colour(0xff398bffu));
    sizeSlider.setArcGradientColours(
        juce::Colour(0xff781047u),
        juce::Colour(0xffff3cacu),
        juce::Colour(0xffffd0eeu)
    );
    sizeSlider.setArcContrastColour(juce::Colour(0xffa94dffu));
    positionSlider.setArcGradientColours(
        juce::Colour(0xff3d7026u),
        juce::Colour(0xffd6ff3fu),
        juce::Colour(0xfff3ffb6u)
    );
    positionSlider.setArcContrastColour(juce::Colour(0xff39d98au));
    pitchSlider.setArcGradientColours(
        juce::Colour(0xff4c2a8cu),
        juce::Colour(0xffa78bfau),
        juce::Colour(0xffede8ffu)
    );
    pitchSlider.setArcContrastColour(juce::Colour(0xff398bffu));

    dryWetSlider.setArcGradientColours(
        juce::Colour(0xff4c6416u),
        juce::Colour(0xffd6ff3fu),
        juce::Colour(0xfff3ffb6u)
    );
    dryWetSlider.setArcContrastColour(juce::Colour(0xff39d98au));
    outputGainSlider.setArcGradientColours(
        juce::Colour(0xff005967u),
        juce::Colour(0xff00e5ffu),
        juce::Colour(0xffd7fdffu)
    );
    outputGainSlider.setArcContrastColour(juce::Colour(0xff398bffu));

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
    densityModeSelector.setComponentID(hypercolor::ParameterIDs::grainDensityMode);
    densityModeSelector.addItem("FREE", 1);
    densityModeSelector.addItem("SYNC", 2);
    densityModeSelector.setTextWhenNothingSelected("FREE");
    densityModeSelector.setJustificationType(juce::Justification::centred);
    densityModeSelector.setColour(
        juce::ComboBox::backgroundColourId,
        hypercolor::ui::palette::background().brighter(0.04f)
    );
    densityModeSelector.setColour(
        juce::ComboBox::outlineColourId,
        hypercolor::ui::palette::border()
    );
    densityModeSelector.setColour(
        juce::ComboBox::textColourId,
        hypercolor::ui::palette::text()
    );
    densityModeSelector.setColour(
        juce::ComboBox::arrowColourId,
        cyan
    );
    densityModeSelector.setColour(
        juce::ComboBox::focusedOutlineColourId,
        cyan
    );
    addAndMakeVisible(densityModeSelector);

    sourceModeSelector.setComponentID(
        hypercolor::ParameterIDs::granularSourceMode
    );
    sourceModeSelector.addItem("LIVE", 1);
    sourceModeSelector.addItem("HISTORY", 2);
    sourceModeSelector.setTextWhenNothingSelected("LIVE");
    sourceModeSelector.setJustificationType(juce::Justification::centred);
    sourceModeSelector.setColour(
        juce::ComboBox::backgroundColourId,
        hypercolor::ui::palette::background().brighter(0.04f)
    );
    sourceModeSelector.setColour(
        juce::ComboBox::outlineColourId,
        hypercolor::ui::palette::border()
    );
    sourceModeSelector.setColour(
        juce::ComboBox::textColourId,
        hypercolor::ui::palette::text()
    );
    sourceModeSelector.setColour(
        juce::ComboBox::arrowColourId,
        hypercolor::ui::palette::space()
    );
    sourceModeSelector.setColour(
        juce::ComboBox::focusedOutlineColourId,
        hypercolor::ui::palette::space()
    );
    sourceModeSelector.setTooltip(
        "Live — читает причинно доступный входящий сигнал. Положение Position "
        "и параметры гранулы определяют участок доступного Live-сигнала.\n\n"
        "History — читает накопленную историю входящего сигнала. Источник и "
        "anchor фиксируются при запуске гранулы.\n\n"
        "Режим применяется к новым гранулам. Уже активные и ожидающие гранулы "
        "сохраняют исходный режим."
    );
    addAndMakeVisible(sourceModeSelector);

    densitySlider.onInteractionFinished = [this]
    {
        synchronizeDensityMode();
    };

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
        button.setButtonText(index == 1 ? "MOD" : effectNames[index]);
        button.setTooltip(effectNames[index]);
        button.setClickingTogglesState(true);
        button.setRadioGroupId(1701);
        button.setColour(juce::TextButton::buttonColourId, panelRaised);
        button.setColour(
            juce::TextButton::buttonOnColourId,
            effectAccent(index)
        );
        button.setColour(juce::TextButton::textColourOffId, text);
        button.setColour(juce::TextButton::textColourOnId, hypercolor::ui::palette::background());
        button.onStateChange = [this, index]
        {
            if (effectButtons[index].getToggleState())
                selectEffect(index);
        };
        button.setComponentID("module." + juce::String(static_cast<int>(index)));
        addAndMakeVisible(button);

        auto& enableButton = fxEnableButtons[index];
        const auto accent = effectAccent(index);
        enableButton.setClickingTogglesState(true);
        enableButton.setComponentID(effectEnableParameterIDs[index]);
        enableButton.setColour(
            juce::TextButton::buttonColourId,
            hypercolor::ui::palette::panelRaised()
        );
        enableButton.setColour(
            juce::TextButton::buttonOnColourId,
            accent
        );
        enableButton.setColour(
            juce::TextButton::textColourOffId,
            hypercolor::ui::palette::mutedText()
        );
        enableButton.setColour(
            juce::TextButton::textColourOnId,
            hypercolor::ui::palette::background()
        );
        enableButton.setTooltip(
            juce::String(effectNames[index]) + " effect ON / OFF"
        );
        enableButton.onStateChange = [enableButtonPtr = &enableButton]
        {
            enableButtonPtr->setButtonText(
                enableButtonPtr->getToggleState() ? "ON" : "OFF"
            );
        };
        enableButton.onStateChange();
        addAndMakeVisible(enableButton);
    }
    granularNavButton.setToggleState(true, juce::dontSendNotification);
    moduleTitleLabel.setText("GRANULAR CORE", juce::dontSendNotification);
    moduleSubtitleLabel.setText("XY CHARACTER FIELD  //  REAL-TIME HISTORY  //  64 VOICES", juce::dontSendNotification);

    auto& state = audioProcessor.getValueTreeState();
    for (std::size_t index = 0; index < fxEnableButtons.size(); ++index)
    {
        fxEnableAttachments[index] = std::make_unique<ButtonAttachment>(
            state,
            effectEnableParameterIDs[index],
            fxEnableButtons[index]
        );
    }
    densityModeAttachment = std::make_unique<ComboBoxAttachment>(
        state,
        hypercolor::ParameterIDs::grainDensityMode,
        densityModeSelector
    );
    sourceModeAttachment = std::make_unique<ComboBoxAttachment>(
        state,
        hypercolor::ParameterIDs::granularSourceMode,
        sourceModeSelector
    );
    densityModeSelector.onChange = [this]
    {
        triggerAsyncUpdate();
    };
    state.addParameterListener(
        hypercolor::ParameterIDs::grainDensityMode,
        this
    );
    appliedDensityMode = readDensityMode();
    rebindDensityControl(appliedDensityMode);
    rebuildInspector();

    setResizable(true, true);
    setResizeLimits(760, 560, 1600, 1000);
    setSize(1100, 700);
}

HypercolorAudioProcessorEditor::~HypercolorAudioProcessorEditor()
{
    audioProcessor.getValueTreeState().removeParameterListener(
        hypercolor::ParameterIDs::grainDensityMode,
        this
    );
    cancelPendingUpdate();
    stopTimer();
    densitySlider.onInteractionFinished = nullptr;
    densityModeSelector.onChange = nullptr;
    densityAttachment.reset();
    densityModeAttachment.reset();
    sourceModeAttachment.reset();
    setLookAndFeel(nullptr);
}

HypercolorAudioProcessorEditor::DensityMode
HypercolorAudioProcessorEditor::readDensityMode() const noexcept
{
    const auto* parameter = dynamic_cast<const juce::AudioParameterChoice*>(
        audioProcessor.getValueTreeState().getParameter(
            hypercolor::ParameterIDs::grainDensityMode
        )
    );
    if (parameter == nullptr)
        return DensityMode::free;

    return parameter->getIndex() >= 1
        ? DensityMode::bpmSync
        : DensityMode::free;
}

void HypercolorAudioProcessorEditor::parameterChanged(
    const juce::String& parameterID,
    float newValue
)
{
    if (parameterID != hypercolor::ParameterIDs::grainDensityMode)
        return;

    pendingDensityMode.store(
        newValue >= 0.5f ? 1 : 0,
        std::memory_order_release
    );
    triggerAsyncUpdate();
}

void HypercolorAudioProcessorEditor::handleAsyncUpdate()
{
    synchronizeDensityMode();
}

void HypercolorAudioProcessorEditor::synchronizeDensityMode()
{
    const auto requestedMode = readDensityMode();
    pendingDensityMode.store(
        requestedMode == DensityMode::bpmSync ? 1 : 0,
        std::memory_order_release
    );

    if (densitySlider.isMouseButtonDown())
    {
        densityRebindDeferred = true;
        return;
    }

    if (!densityRebindDeferred
        && densityAttachment != nullptr
        && requestedMode == appliedDensityMode)
        return;

    rebindDensityControl(requestedMode);
}

void HypercolorAudioProcessorEditor::rebindDensityControl(DensityMode mode)
{
    if (densitySlider.isMouseButtonDown())
    {
        densityRebindDeferred = true;
        pendingDensityMode.store(
            mode == DensityMode::bpmSync ? 1 : 0,
            std::memory_order_release
        );
        return;
    }

    densityRebindDeferred = false;

    // The old attachment must be gone before changing the slider range or
    // formatter.  This prevents a stale Free value from being written into
    // the choice parameter during a mode transition.
    densityAttachment.reset();
    densitySlider.setValue(0.0, juce::dontSendNotification);
    densitySlider.setTextValueSuffix({});

    auto& state = audioProcessor.getValueTreeState();
    const auto* divisionParameter = dynamic_cast<const juce::AudioParameterChoice*>(
        state.getParameter(hypercolor::ParameterIDs::grainDensityDivision)
    );

    const auto cyan = hypercolor::ui::palette::cyan();
    if (mode == DensityMode::free)
    {
        densitySlider.setRange(0.0, 120.0, 0.1);
        densitySlider.setNumDecimalPlacesToDisplay(1);
        densitySlider.textFromValueFunction = [](double value)
        {
            return juce::String(value, 1) + " gr/s";
        };
        densitySlider.valueFromTextFunction = [](const juce::String& text)
        {
            return text.getDoubleValue();
        };
        densitySlider.setTargetPalette(
            cyan,
            juce::Colour(0xff005967u),
            juce::Colour(0xff00e5ffu),
            juce::Colour(0xffd7fdffu),
            juce::Colour(0xff398bffu)
        );

        densityAttachment = std::make_unique<SliderAttachment>(
            state,
            hypercolor::ParameterIDs::grainDensity,
            densitySlider
        );
    }
    else
    {
        // The parameter is created by the processor and is the single source
        // of truth for the musical labels.  Keep a defensive empty array for
        // construction-time failures rather than duplicating the APVTS list
        // in the editor.
        const auto divisionLabels = divisionParameter != nullptr
            ? divisionParameter->getAllValueStrings()
            : juce::StringArray {};
        densitySlider.setRange(0.0, 9.0, 1.0);
        densitySlider.setNumDecimalPlacesToDisplay(0);
        densitySlider.textFromValueFunction = [divisionLabels](double value)
        {
            if (divisionLabels.isEmpty())
                return juce::String("SYNC");

            const auto index = juce::jlimit(
                0,
                divisionLabels.size() - 1,
                juce::roundToInt(value)
            );
            return divisionLabels[index];
        };
        densitySlider.valueFromTextFunction = [divisionLabels](
            const juce::String& text
        )
        {
            for (int index = 0; index < divisionLabels.size(); ++index)
                if (divisionLabels[index].equalsIgnoreCase(text.trim()))
                    return static_cast<double>(index);
            return static_cast<double>(juce::jlimit(
                0,
                divisionLabels.size() - 1,
                text.getIntValue()
            ));
        };
        densitySlider.setTargetPalette(
            juce::Colour(0xffffb84au),
            juce::Colour(0xff6b3510u),
            juce::Colour(0xffffb84au),
            juce::Colour(0xfffff0b6u),
            juce::Colour(0xffa78bfau)
        );

        densityAttachment = std::make_unique<SliderAttachment>(
            state,
            hypercolor::ParameterIDs::grainDensityDivision,
            densitySlider
        );
    }

    appliedDensityMode = mode;
    densitySlider.repaint();
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
    if (selectedModule < 0 && !granularControlsArea.isEmpty())
    {
        // The primary grain controls live on their own surface.  Keeping this
        // layer separate from the XY field makes the future rotary controls
        // feel like an instrument strip instead of controls floating over the
        // visualisation.
        auto controlSurface = granularControlsArea.toFloat().reduced(1.0f);
        juce::ColourGradient granularSurfaceGradient(
            hypercolor::ui::palette::panelRaised().withAlpha(0.72f),
            controlSurface.getX(),
            controlSurface.getY(),
            hypercolor::ui::palette::background().withAlpha(0.42f),
            controlSurface.getRight(),
            controlSurface.getBottom(),
            false
        );
        graphics.setGradientFill(granularSurfaceGradient);
        graphics.fillRoundedRectangle(controlSurface, 8.0f);
        graphics.setColour(hypercolor::ui::palette::border().withAlpha(0.82f));
        graphics.drawRoundedRectangle(controlSurface, 8.0f, 1.0f);
        graphics.setColour(hypercolor::ui::palette::cyan().withAlpha(0.72f));
        graphics.fillRect(controlSurface.getX(), controlSurface.getY(), 86.0f, 2.0f);
        graphics.setColour(hypercolor::ui::palette::pink().withAlpha(0.58f));
        graphics.fillRect(controlSurface.getRight() - 42.0f, controlSurface.getY(), 42.0f, 2.0f);
    }
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
            effectAccent(selectedEffect),
            effectVisualCurrent
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
        slider.onValueChange = nullptr;
        slider.setVisible(false);
        removeChildComponent(&slider);
    }
    for (auto& knob : fxRotaryKnobs)
    {
        knob.onValueChange = nullptr;
        knob.setVisible(false);
        removeChildComponent(&knob);
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

        const auto isEffect = selectedModule >= 0;
        auto& slider = isEffect
            ? static_cast<juce::Slider&>(fxRotaryKnobs[i])
            : inspectorSliders[i];
        auto& label = inspectorLabels[i];
        const auto range = parameter->getNormalisableRange();
        const auto isEffectMix = isEffect && i == effectMixIndices[selectedEffect];
        const auto accent = isEffect
            ? effectAccent(selectedEffect)
            : (i == 1 || i == 2 ? hypercolor::ui::palette::pink()
                                : hypercolor::ui::palette::cyan());
        slider.setSliderStyle(isEffect
            ? juce::Slider::RotaryHorizontalVerticalDrag
            : juce::Slider::LinearHorizontal);
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
        if (isEffect)
        {
            auto& knob = fxRotaryKnobs[i];
            knob.setMouseDragSensitivity(180);
            knob.setVisualScale(1.0f);
            knob.setTargetPalette(
                accent,
                accent.darker(0.48f),
                accent,
                accent.brighter(0.42f),
                accent.interpolatedWith(hypercolor::ui::palette::text(), 0.48f)
            );
            knob.onValueChange = [this]
            {
                updateEffectVisualTargets(false);
            };
        }
        else
        {
            slider.setMouseDragSensitivity(140);
        }
        auto displayName = isEffect
            ? parameter->getName(24).toUpperCase()
            : juce::String(parameterID).toUpperCase();
        if (isEffectMix)
            displayName = "MIX";
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
        label.setComponentID("inspector.label." + juce::String(static_cast<int>(i)));
        label.setText(displayName, juce::dontSendNotification);
        label.setColour(juce::Label::textColourId, hypercolor::ui::palette::mutedText());
        label.setFont(juce::FontOptions(10.0f).withStyle("Bold"));
        addAndMakeVisible(label);
        addAndMakeVisible(slider);
        inspectorAttachments[i] = std::make_unique<SliderAttachment>(
            audioProcessor.getValueTreeState(), parameterID, slider
        );
    }
    updateEffectVisualTargets(true);
    resized();
}

void HypercolorAudioProcessorEditor::updateEffectVisualTargets(
    bool snapToTarget
)
{
    if (selectedModule < 0)
    {
        stopTimer();
        effectVisualCurrent.fill(0.0f);
        effectVisualTarget.fill(0.0f);
        return;
    }

    const auto parameterCount = effectParameterCounts[selectedEffect];
    for (std::size_t index = 0; index < effectVisualTarget.size(); ++index)
    {
        if (index >= parameterCount)
        {
            effectVisualTarget[index] = 0.0f;
            continue;
        }

        const auto* parameter = audioProcessor.getValueTreeState().getParameter(
            effectParameters[selectedEffect][index]
        );
        if (parameter == nullptr)
        {
            effectVisualTarget[index] = 0.0f;
            continue;
        }

        const auto& knob = fxRotaryKnobs[index];
        const auto range = knob.getNormalisableRange();
        const auto normalized = range.convertTo0to1(
            static_cast<float>(knob.getValue())
        );
        effectVisualTarget[index] = juce::jlimit(
            -1.0f,
            1.0f,
            static_cast<float>(normalized) - parameter->getDefaultValue()
        );
    }

    if (snapToTarget)
    {
        stopTimer();
        effectVisualCurrent = effectVisualTarget;
        repaint(inspectorPanel);
        return;
    }

    auto needsSmoothing = false;
    for (std::size_t index = 0; index < effectVisualCurrent.size(); ++index)
    {
        if (std::abs(effectVisualTarget[index] - effectVisualCurrent[index]) > 0.001f)
        {
            needsSmoothing = true;
            break;
        }
    }

    if (needsSmoothing && !isTimerRunning())
        startTimerHz(30);
}

void HypercolorAudioProcessorEditor::timerCallback()
{
    if (selectedModule < 0)
    {
        stopTimer();
        return;
    }

    auto stillMoving = false;
    for (std::size_t index = 0; index < effectVisualCurrent.size(); ++index)
    {
        auto& current = effectVisualCurrent[index];
        const auto target = effectVisualTarget[index];
        const auto difference = target - current;
        if (std::abs(difference) <= 0.001f)
        {
            current = target;
            continue;
        }

        current += difference * 0.28f;
        stillMoving = true;
    }

    repaint(inspectorPanel);
    if (!stillMoving)
        stopTimer();
}

void HypercolorAudioProcessorEditor::resized()
{
    // Keep the generous horizontal breathing room of the module layout, but
    // use a tighter vertical frame.  The previous single margin value made
    // the top and bottom reservations follow the editor width, leaving an
    // unnecessarily large dead band above the workspace on a 1100px window.
    const auto horizontalMargin = juce::jlimit(18, 36, getWidth() / 30);
    const auto verticalMargin = juce::jlimit(16, 24, getHeight() / 32);
    auto content = getLocalBounds()
        .withTrimmedLeft(horizontalMargin)
        .withTrimmedRight(horizontalMargin)
        .withTrimmedTop(verticalMargin)
        .withTrimmedBottom(verticalMargin);
    const auto headerHeight = juce::jlimit(
        64,
        84,
        juce::roundToInt(static_cast<float>(getHeight()) * 0.112f)
    );
    headerArea = content.removeFromTop(headerHeight);
    content.removeFromTop(6);
    const auto navigationWidth = juce::jlimit(128, 152, getWidth() / 7);
    rackPanel = content.removeFromLeft(navigationWidth);
    content.removeFromLeft(12);
    auto workspace = content;
    masterPanel = {};

    // Global controls stay in the header on every workspace.
    auto headerControls = headerArea.reduced(8, 6).removeFromRight(244);
    auto dryWetBounds = headerControls.removeFromLeft(116).reduced(6, 0);
    auto outputGainBounds = headerControls.removeFromLeft(116).reduced(6, 0);
    // Keep the caption in its own row.  The deliberate gap is larger than
    // the knob's outer arc and shadow, so the label cannot visually sit on
    // top of the control when the compact Header is resized.
    dryWetLabel.setBounds(dryWetBounds.removeFromTop(16));
    dryWetBounds.removeFromTop(10);
    dryWetSlider.setBounds(dryWetBounds);
    outputGainLabel.setBounds(outputGainBounds.removeFromTop(16));
    outputGainBounds.removeFromTop(10);
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
    for (std::size_t index = 0; index < effectButtons.size(); ++index)
    {
        auto& button = effectButtons[index];
        auto row = navigationArea.removeFromTop(30);
        auto enableBounds = row.removeFromRight(36);
        row.removeFromRight(4);
        button.setBounds(row);
        fxEnableButtons[index].setBounds(enableBounds);
        navigationArea.removeFromTop(4);
    }

    moduleTitleLabel.setBounds(workspace.removeFromTop(24));
    moduleSubtitleLabel.setBounds(workspace.removeFromTop(16));
    workspace.removeFromTop(4);

    const bool granularMode = selectedModule < 0;
    masterSectionLabel.setVisible(!granularMode);
    for (auto* component : {
             static_cast<juce::Component*>(&xyController),
             static_cast<juce::Component*>(&granularVisualizer),
             static_cast<juce::Component*>(&granularSectionLabel),
             static_cast<juce::Component*>(&densitySlider),
             static_cast<juce::Component*>(&densityModeSelector),
             static_cast<juce::Component*>(&sourceModeSelector),
             static_cast<juce::Component*>(&sizeSlider),
             static_cast<juce::Component*>(&positionSlider),
             static_cast<juce::Component*>(&pitchSlider),
             static_cast<juce::Component*>(&reverseButton),
             static_cast<juce::Component*>(&densityLabel),
             static_cast<juce::Component*>(&sizeLabel),
             static_cast<juce::Component*>(&positionLabel),
             static_cast<juce::Component*>(&pitchLabel),
             static_cast<juce::Component*>(&reverseLabel) })
        component->setVisible(granularMode);

    if (granularMode)
    {
        const auto gap = juce::jlimit(12, 20, workspace.getWidth() / 65);
        // Give the XY field the dominant column.  The variation inspector
        // remains available on the right, while primary grain controls move
        // into their own two-row instrument strip beneath the inspector
        // heading instead of competing with the field.
        const auto leftRatio = workspace.getWidth() >= 700 ? 0.63f : 0.58f;
        const auto leftWidth = juce::roundToInt(
            static_cast<float>(workspace.getWidth() - gap) * leftRatio
        );
        grainControlPanel = workspace.removeFromLeft(leftWidth);
        workspace.removeFromLeft(gap);
        inspectorPanel = workspace;
        granularControlsArea = {};

        auto grainContent = grainControlPanel.reduced(10, 8);
        constexpr int minimumVisualizerHeight = 74;
        const auto visualizerHeight = juce::jlimit(
            minimumVisualizerHeight,
            124,
            juce::roundToInt(static_cast<float>(grainContent.getHeight()) * 0.22f)
        );

        // The field owns the upper and central portion of the column.  The
        // telemetry remains a dedicated strip underneath it, never sharing
        // bounds with labels or controls.
        const auto visualizerBounds = grainContent.removeFromBottom(visualizerHeight);
        grainContent.removeFromBottom(8);
        xyController.setBounds(grainContent);
        granularVisualizer.setBounds(visualizerBounds);

        // Primary controls are now a separate two-row area in the right
        // inspector.  Three columns on the first row and two on the second
        // leave enough physical room for the future large rotary knobs.
        auto inspectorArea = inspectorPanel.reduced(16, 12);
        const auto primaryHeight = juce::jlimit(
            190,
            270,
            juce::roundToInt(static_cast<float>(inspectorArea.getHeight()) * 0.52f)
        );
        granularControlsArea = inspectorArea.removeFromTop(primaryHeight);
        auto primaryArea = granularControlsArea.reduced(10, 8);
        auto sectionHeader = primaryArea.removeFromTop(21);
        auto sourceSelectorBounds = sectionHeader.removeFromRight(
            juce::jmin(112, juce::jmax(96, sectionHeader.getWidth() / 3))
        );
        granularSectionLabel.setBounds(sectionHeader);
        sourceModeSelector.setBounds(sourceSelectorBounds.reduced(1, 1));
        primaryArea.removeFromTop(4);

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

        const auto cellWidth = juce::jmax(1, primaryArea.getWidth() / 3);
        const auto cellHeight = juce::jmax(1, primaryArea.getHeight() / 2);
        for (std::size_t index = 0; index < granularControls.size(); ++index)
        {
            const auto row = static_cast<int>(index / 3);
            const auto column = static_cast<int>(index % 3);
            const auto cellX = primaryArea.getX() + column * cellWidth;
            const auto cellRight = granularControls[index] == &reverseButton
                ? primaryArea.getRight()
                : (column == 2 ? primaryArea.getRight() : cellX + cellWidth);
            auto cell = juce::Rectangle<int>(
                cellX,
                primaryArea.getY() + row * cellHeight,
                cellRight - cellX,
                row == 1 ? primaryArea.getBottom() - (primaryArea.getY() + row * cellHeight) : cellHeight
            ).reduced(3, 1);

            granularLabels[index]->setBounds(cell.removeFromTop(18));
            if (index == 0)
            {
                densityModeSelector.setBounds(cell.removeFromTop(18).reduced(1, 0));
                cell.removeFromTop(5);
            }
            else
            {
                // Keep the parameter caption visually independent from the
                // rotary body.  This gap is part of the cell geometry rather
                // than a paint-order trick, so it remains correct on resize.
                cell.removeFromTop(10);
            }
            if (granularControls[index] == &reverseButton)
            {
                granularControls[index]->setBounds(cell.withSizeKeepingCentre(
                    juce::jmin(136, juce::jmax(92, cell.getWidth() - 8)),
                    juce::jmin(62, cell.getHeight())
                ));
            }
            else
            {
                granularControls[index]->setBounds(cell.reduced(1, 0));
            }

        }
    }
    else
    {
        grainControlPanel = {};
        granularControlsArea = {};
        const auto mixWidth = juce::jlimit(136, 168, workspace.getWidth() / 6);
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
        auto mixKnobArea = mixArea.reduced(4, 0);
        const auto mixKnobWidth = juce::jmin(168, mixKnobArea.getWidth());
        const auto mixKnobSize = juce::Point<int> {
            mixKnobWidth,
            juce::jmin(
                196,
                juce::jmin(mixKnobArea.getHeight(), mixKnobWidth + 28)
            )
        };
        fxRotaryKnobs[mixIndex].setBounds(
            juce::Rectangle<int>(mixKnobSize.x, mixKnobSize.y)
                .withCentre(mixKnobArea.getCentre())
        );
    }

    auto inspectorArea = inspectorPanel.reduced(16, 12);
    if (granularMode && !granularControlsArea.isEmpty())
    {
        // Leave a clear breathing gap between the primary grain strip and the
        // secondary variation inspector.
        inspectorArea.removeFromTop(granularControlsArea.getHeight() + 8);
    }
    inspectorSectionLabel.setBounds(inspectorArea.removeFromTop(26));
    inspectorHintLabel.setBounds(inspectorArea.removeFromTop(18));
    inspectorArea.removeFromTop(8);
    if (granularMode)
    {
        const auto rowHeight = juce::jmax(26, inspectorArea.getHeight() / 7);
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
            if (i == mixIndex || !fxRotaryKnobs[i].isVisible())
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
            ).reduced(3, 4);
            inspectorLabels[i].setBounds(cell.removeFromTop(20));
            cell.removeFromTop(3);
            const auto knobWidth = juce::jmin(168, cell.getWidth());
            const auto knobSize = juce::Point<int> {
                knobWidth,
                juce::jmin(196, juce::jmin(cell.getHeight(), knobWidth + 28))
            };
            fxRotaryKnobs[i].setBounds(
                juce::Rectangle<int>(knobSize.x, knobSize.y)
                    .withCentre(cell.getCentre())
            );
            ++cellIndex;
        }
    }

    // Panel surfaces are painted by this parent component while the controls
    // themselves are child components.  A module switch can otherwise leave
    // parts of the previous surface in the dirty-region cache until the
    // window receives focus again.
    repaint();
}
