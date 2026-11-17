#include "XYController.h"

#include "HypercolorLookAndFeel.h"

#include <algorithm>

namespace hypercolor::ui
{
XYController::XYController()
{
    setComponentID("xyController");
    setWantsKeyboardFocus(true);
    setRepaintsOnMouseActivity(true);

    // Deterministic, fixed particle layout.  The particles are deliberately
    // sparse and subdued so the grid remains the visual structure.
    for (std::size_t index = 0; index < particles.size(); ++index)
    {
        auto& particle = particles[index];
        particle.phase = std::fmod(0.11f + static_cast<float>(index) * 0.137f, 1.0f);
        particle.speed = 0.024f + static_cast<float>(index % 5) * 0.010f;
        particle.size = 1.0f + static_cast<float>(index % 3) * 0.24f;
        particle.alpha = 0.15f + static_cast<float>((index * 3) % 5) * 0.035f;
        particle.trail = index % 3 == 0 ? 0.024f : (index % 3 == 1 ? 0.012f : 0.0f);
        particle.lane = static_cast<int>(index % 10);
        particle.direction = index % 4 == 0 ? -1.0f : 1.0f;
        particle.vertical = index >= particles.size() / 2;
    }
}

XYController::~XYController()
{
    stopTimer();
}

void XYController::setOnChange(std::function<void(XYCoordinates)> callback)
{
    onChange = std::move(callback);
}

void XYController::setCoordinates(XYCoordinates newCoordinates, bool notify) noexcept
{
    const auto projected = projectToUnitCircle(newCoordinates);
    const auto deltaX = projected.x - coordinates.x;
    const auto deltaY = projected.y - coordinates.y;
    const auto distance = std::sqrt(deltaX * deltaX + deltaY * deltaY);
    const auto changed = std::abs(deltaX) > 0.000001f || std::abs(deltaY) > 0.000001f;
    coordinates = projected;
    if (changed && dragging)
    {
        const auto safeDistance = juce::jmax(distance, 0.000001f);
        targetMotionDirection = { deltaX / safeDistance, deltaY / safeDistance };
        motionSpeed = juce::jlimit(0.0f, 1.0f, distance * 7.0f);
        pushTrail(projected, distance);
    }
    repaint();
    if (notify && changed && onChange)
        onChange(coordinates);
}

juce::Rectangle<float> XYController::circleBounds() const noexcept
{
    const auto bounds = getLocalBounds().toFloat().reduced(14.0f);
    const auto diameter = std::max(1.0f, std::min(bounds.getWidth(), bounds.getHeight()));
    return bounds.withSizeKeepingCentre(diameter, diameter);
}

juce::Point<float> XYController::pointForCoordinates() const noexcept
{
    const auto circle = circleBounds();
    return {
        circle.getX() + coordinates.x * circle.getWidth(),
        circle.getBottom() - coordinates.y * circle.getHeight()
    };
}

void XYController::resized()
{
    repaint();
}

void XYController::mouseEnter(const juce::MouseEvent&)
{
    hovered = true;
    startMotionTimer();
    repaint();
}

void XYController::mouseExit(const juce::MouseEvent&)
{
    hovered = false;
    repaint();
}

void XYController::startMotionTimer() noexcept
{
    if (!isTimerRunning())
        startTimerHz(60);
}

void XYController::pushPulse(XYCoordinates value, float distance) noexcept
{
    auto& pulse = pulses[nextPulse];
    pulse.value = value;
    pulse.direction = motionDirection;
    pulse.age = 0.0f;
    pulse.strength = juce::jlimit(0.35f, 1.0f, 0.25f + distance * 4.0f);
    nextPulse = (nextPulse + 1) % pulses.size();
}

void XYController::pushTrail(XYCoordinates value, float distance) noexcept
{
    for (std::size_t index = trail.size() - 1; index > 0; --index)
        trail[index] = trail[index - 1];

    trail[0] = { value, 0.0f };
    trailSize = juce::jmin(trailSize + 1, trail.size());
    motionEnergy = juce::jmax(motionEnergy, juce::jlimit(0.2f, 1.0f, 0.22f + distance * 5.0f));
    pushPulse(value, distance);
    startMotionTimer();
}

void XYController::timerCallback()
{
    constexpr float frameSeconds = 1.0f / 60.0f;
    constexpr float trailLifetime = 0.58f;
    constexpr float pulseLifetime = 0.62f;

    for (std::size_t index = 0; index < trailSize; ++index)
        trail[index].age += frameSeconds / trailLifetime;

    while (trailSize > 0 && trail[trailSize - 1].age >= 1.0f)
        --trailSize;

    auto activePulse = false;
    for (auto& pulse : pulses)
    {
        if (pulse.age >= 1.0f)
            continue;

        pulse.age += frameSeconds / pulseLifetime;
        activePulse = activePulse || pulse.age < 1.0f;
    }

    // Particles use a bounded phase accumulator.  They animate while the
    // field is being observed or interacted with; once the pointer leaves and
    // all release energy has faded, the timer can stop again.
    if (hovered || dragging || trailSize > 0 || activePulse || motionEnergy > 0.0f)
    {
        for (auto& particle : particles)
        {
            particle.phase += particle.speed * frameSeconds * particle.direction;
            particle.phase -= std::floor(particle.phase);
        }
    }

    motionEnergy *= dragging ? 0.975f : 0.90f;
    motionSpeed *= dragging ? 0.94f : 0.84f;
    const auto directionBlend = juce::jlimit(0.0f, 1.0f, frameSeconds * (dragging ? 14.0f : 7.0f));
    motionDirection.x += (targetMotionDirection.x - motionDirection.x) * directionBlend;
    motionDirection.y += (targetMotionDirection.y - motionDirection.y) * directionBlend;
    const auto directionLength = std::sqrt(motionDirection.x * motionDirection.x + motionDirection.y * motionDirection.y);
    if (directionLength > 0.0001f)
    {
        motionDirection.x /= directionLength;
        motionDirection.y /= directionLength;
    }
    fieldPhase += frameSeconds * (0.75f + motionSpeed * 4.0f);
    if (fieldPhase >= juce::MathConstants<float>::twoPi)
        fieldPhase -= juce::MathConstants<float>::twoPi;
    if (motionEnergy < 0.001f)
        motionEnergy = 0.0f;

    if (dragging || hovered || trailSize > 0 || activePulse || motionEnergy > 0.0f)
    {
        repaint();
        return;
    }

    stopTimer();
}

void XYController::paint(juce::Graphics& graphics)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto surface = bounds.reduced(1.0f);
    const auto cyan = palette::cyan();
    const auto pink = palette::pink();
    const auto grid = palette::grid();
    const auto motion = juce::jlimit(0.0f, 1.0f, motionEnergy);

    juce::ColourGradient surfaceGradient(palette::background().brighter(0.035f),
                                         surface.getX(), surface.getY(),
                                         palette::panelRaised().darker(0.18f),
                                         surface.getRight(), surface.getBottom(), false);
    graphics.setGradientFill(surfaceGradient);
    graphics.fillRoundedRectangle(surface, 13.0f);
    graphics.setColour(grid.withAlpha(0.8f));
    graphics.drawRoundedRectangle(surface, 13.0f, 1.0f);
    graphics.setColour(cyan.withAlpha(hovered ? 0.42f : 0.18f));
    graphics.drawRoundedRectangle(surface.reduced(3.0f), 10.0f, hovered ? 1.2f : 0.7f);

    // Small rails create a visual frame without competing with the control point.
    graphics.setColour(cyan.withAlpha(0.78f));
    graphics.fillRect(surface.getX() + 18.0f, surface.getY() + 15.0f, 68.0f, 2.0f);
    graphics.setColour(pink.withAlpha(0.75f));
    graphics.fillRect(surface.getRight() - 76.0f, surface.getBottom() - 17.0f, 58.0f, 2.0f);

    // Keep the painted field and the interaction geometry identical.  The
    // radial projection and the handle must reach the same boundary.
    const auto circle = circleBounds();
    const auto centre = circle.getCentre();
    const auto radius = circle.getWidth() * 0.5f;
    const auto field = circle.expanded(7.0f);
    const auto point = pointForCoordinates();
    const auto motionVector = juce::Point<float>(motionDirection.x, -motionDirection.y);

    juce::ColourGradient fieldGradient(cyan.withAlpha(0.035f + motion * 0.045f), field.getX(), field.getY(),
                                        pink.withAlpha(0.018f + motion * 0.035f), field.getRight(), field.getBottom(), true);
    graphics.setGradientFill(fieldGradient);
    graphics.fillEllipse(field);

    graphics.setColour(cyan.withAlpha(0.12f + motion * 0.08f));
    graphics.drawEllipse(field, 1.0f);
    graphics.setColour(grid.withAlpha(0.54f));
    graphics.drawEllipse(circle, 1.25f);

    constexpr float axisDashes[] { 3.0f, 5.0f };
    graphics.setColour(grid.withAlpha(0.34f));
    graphics.drawDashedLine({ circle.getX(), centre.y, circle.getRight(), centre.y }, axisDashes, 2, 0.7f);
    graphics.drawDashedLine({ centre.x, circle.getY(), centre.x, circle.getBottom() }, axisDashes, 2, 0.7f);

    // Radial ticks are a compact coordinate scale and also make the circular
    // boundary feel machined rather than decorative.
    for (int tick = 0; tick < 16; ++tick)
    {
        const auto angle = juce::MathConstants<float>::twoPi * static_cast<float>(tick) / 16.0f;
        const auto inner = centre + juce::Point<float>(std::cos(angle), std::sin(angle)) * (radius - 1.0f);
        const auto outer = centre + juce::Point<float>(std::cos(angle), std::sin(angle)) * (radius + (tick % 4 == 0 ? 7.0f : 4.0f));
        graphics.setColour((tick % 4 == 0 ? cyan : grid).withAlpha(tick % 4 == 0 ? 0.48f : 0.25f));
        graphics.drawLine(inner.x, inner.y, outer.x, outer.y, tick % 4 == 0 ? 1.0f : 0.6f);
    }

    // A denser perimeter of tiny, static markers adds instrument-like detail
    // without turning the boundary into a heavy scale.  The phase-based
    // alpha variation is deterministic, so the dots never shimmer or depend
    // on the motion timer.
    constexpr int perimeterDotCount = 48;
    for (int dot = 0; dot < perimeterDotCount; ++dot)
    {
        const auto angle = juce::MathConstants<float>::twoPi
            * static_cast<float>(dot) / static_cast<float>(perimeterDotCount);
        const auto direction = juce::Point<float>(std::cos(angle), std::sin(angle));
        const auto position = centre + direction * (radius + 0.5f);
        const auto major = dot % 12 == 0;
        const auto brightness = 0.84f + 0.16f
            * (0.5f + 0.5f * std::sin(static_cast<float>(dot) * 1.71f + 0.4f));
        const auto colour = dot % 6 == 0
            ? cyan
            : (dot % 6 == 3 ? pink : grid);
        const auto alpha = (major ? 0.42f : 0.24f) * brightness;
        const auto dotRadius = major ? 1.35f : 0.88f;
        graphics.setColour(colour.withAlpha(alpha));
        graphics.fillEllipse(position.x - dotRadius,
                             position.y - dotRadius,
                             dotRadius * 2.0f,
                             dotRadius * 2.0f);
    }

    // Fluid grid.  Only the thin, flowing filaments remain here: the larger
    // moving body shapes from the previous iteration were visually noisy and
    // made the core appear to drag objects behind it.
    graphics.saveState();
    juce::Path fluidClip;
    fluidClip.addEllipse(circle.reduced(1.15f));
    graphics.reduceClipRegion(fluidClip);

    // Ten lanes in each direction create an even but non-technical
    // distribution.  The original eight lanes keep their intensity and
    // character; two quiet edge lanes extend the fluid coverage to the rim.
    constexpr float horizontalOffsets[] { -0.82f, -0.61f, -0.39f, -0.14f, 0.13f, 0.38f, 0.62f, 0.83f, -0.94f, 0.95f };
    constexpr float verticalOffsets[] { -0.8f, -0.57f, -0.32f, -0.07f, 0.18f, 0.43f, 0.67f, 0.84f, -0.94f, 0.95f };
    constexpr float lineIntensity[] { 0.34f, 0.62f, 0.26f, 0.78f, 0.4f, 0.58f, 0.22f, 0.68f, 0.18f, 0.3f };
    constexpr int lanesPerDirection = 10;

    const auto pointOnFluidLane = [&](bool vertical, int laneIndex, float u)
    {
        const auto laneId = vertical ? 8 + laneIndex : laneIndex;
        const auto lanePosition = vertical ? verticalOffsets[laneIndex] : horizontalOffsets[laneIndex];
        const auto phase = fieldPhase * (0.42f + static_cast<float>(laneId) * 0.017f)
            + static_cast<float>(laneId) * 0.57f;
        const auto along = u * juce::MathConstants<float>::twoPi;
        const auto wave = std::sin(along * (1.0f + static_cast<float>(laneIndex % 3) * 0.12f) + phase)
            + 0.32f * std::sin(along * 2.0f - phase * 0.7f);
        const auto baseCurve = std::sin(along * 0.72f + static_cast<float>(laneId) * 1.35f)
            * radius * (0.012f + std::abs(lanePosition) * 0.012f);
        const auto laneCurl = std::sin(along * 1.45f + phase * 0.3f)
            * radius * (0.008f + motion * 0.02f) * (0.45f + std::abs(lanePosition));

        if (!vertical)
        {
            const auto x = circle.getX() - radius * 0.15f + u * radius * 2.3f;
            const auto focus = std::exp(-((x - point.x) * (x - point.x))
                                       / juce::jmax(1.0f, radius * radius * 0.42f));
            const auto bend = (point.y - centre.y) * focus * (0.08f + motion * 0.16f);
            const auto directional = motionVector.y * motionSpeed * radius * (0.02f + focus * 0.045f);
            const auto y = centre.y + lanePosition * radius
                + baseCurve + laneCurl + wave * radius * (0.012f + motion * 0.032f) + bend + directional;
            const auto xShift = motionVector.x * motionSpeed * radius * focus * 0.035f
                + std::sin(phase + u * 3.0f) * radius * (0.002f + motion * 0.006f);
            return juce::Point<float> { x + xShift, y };
        }

        const auto y = circle.getY() - radius * 0.15f + u * radius * 2.3f;
        const auto focus = std::exp(-((y - point.y) * (y - point.y))
                                   / juce::jmax(1.0f, radius * radius * 0.42f));
        const auto bend = (point.x - centre.x) * focus * (0.075f + motion * 0.15f);
        const auto directional = motionVector.x * motionSpeed * radius * (0.02f + focus * 0.045f);
        const auto x = centre.x + lanePosition * radius
            + baseCurve + laneCurl + wave * radius * (0.012f + motion * 0.032f) + bend + directional;
        const auto yShift = motionVector.y * motionSpeed * radius * focus * 0.035f
            + std::sin(phase + u * 3.0f) * radius * (0.002f + motion * 0.006f);
        return juce::Point<float> { x, y + yShift };
    };

    for (int lane = 0; lane < lanesPerDirection * 2; ++lane)
    {
        const auto vertical = lane >= lanesPerDirection;
        const auto laneIndex = lane % lanesPerDirection;
        const auto intensity = lineIntensity[laneIndex];
        constexpr int sampleCount = 44;
        std::array<juce::Point<float>, sampleCount> samples {};
        for (int sample = 0; sample < sampleCount; ++sample)
        {
            const auto u = static_cast<float>(sample) / static_cast<float>(sampleCount - 1);
            samples[static_cast<std::size_t>(sample)] = pointOnFluidLane(vertical, laneIndex, u);
        }

        juce::Path filament;
        filament.startNewSubPath((samples[0] + samples[1]) * 0.5f);
        for (int sample = 1; sample < sampleCount - 1; ++sample)
        {
            const auto current = samples[static_cast<std::size_t>(sample)];
            const auto next = samples[static_cast<std::size_t>(sample + 1)];
            filament.quadraticTo(current, (current + next) * 0.5f);
        }
        filament.quadraticTo(samples.back(), samples.back());

        const auto colourMix = vertical ? 0.5f + static_cast<float>(laneIndex) * 0.045f
                                       : 0.08f + static_cast<float>(laneIndex) * 0.045f;
        const auto filamentColour = cyan.interpolatedWith(pink, colourMix);
        const auto alpha = 0.018f + intensity * 0.035f + motion * (0.018f + intensity * 0.045f);
        graphics.setColour(filamentColour.withAlpha(alpha));
        graphics.strokePath(filament, juce::PathStrokeType(0.5f + intensity * 0.18f + motion * 0.25f));
    }

    // Micro particles travel on the same procedural lanes as the grid.  They
    // are intentionally tiny and sparse: their job is to reveal movement in
    // the fluid surface, not to become a second visual system.
    const auto wrapUnit = [](float value) noexcept
    {
        return value - std::floor(value);
    };
    for (const auto& particle : particles)
    {
        const auto current = pointOnFluidLane(particle.vertical, particle.lane, particle.phase);
        const auto tailPhase = wrapUnit(particle.phase - particle.direction * particle.trail);
        const auto tail = pointOnFluidLane(particle.vertical, particle.lane, tailPhase);
        const auto shimmer = 0.72f + 0.28f
            * std::sin(fieldPhase * (0.9f + particle.speed * 3.0f) + particle.phase * juce::MathConstants<float>::twoPi);
        const auto alpha = juce::jlimit(0.0f, 0.42f, particle.alpha * (0.82f + shimmer * 0.18f)
                                                     * (hovered || dragging ? 1.0f : 0.72f));
        const auto colourMix = particle.vertical ? 0.56f + static_cast<float>(particle.lane) * 0.028f
                                                 : 0.18f + static_cast<float>(particle.lane) * 0.028f;
        const auto particleColour = cyan.interpolatedWith(pink, juce::jlimit(0.0f, 1.0f, colourMix));
        const auto particleSize = particle.size * (dragging ? 1.12f : 1.0f);

        if (particle.trail > 0.0f)
        {
            graphics.setColour(particleColour.withAlpha(alpha * 0.24f));
            graphics.drawLine(tail.x, tail.y, current.x, current.y, 0.75f);
        }

        graphics.setColour(particleColour.withAlpha(alpha * 0.22f));
        graphics.fillEllipse(current.x - particleSize * 2.0f, current.y - particleSize * 2.0f,
                             particleSize * 4.0f, particleSize * 4.0f);
        graphics.setColour(particleColour.withAlpha(alpha));
        graphics.fillEllipse(current.x - particleSize, current.y - particleSize,
                             particleSize * 2.0f, particleSize * 2.0f);
    }

    // Magnetic Field remains a quiet secondary layer.  It is only strong
    // enough to reveal attraction and direction while the fluid filaments do
    // the visual heavy lifting.
    for (int lane = 0; lane < 4; ++lane)
    {
        const auto angle = juce::MathConstants<float>::twoPi * static_cast<float>(lane) / 4.0f
            + fieldPhase * (0.008f + motion * 0.018f);
        const auto radial = juce::Point<float>(std::cos(angle), std::sin(angle));
        const auto tangent = juce::Point<float>(-radial.y, radial.x);
        const auto start = centre + radial * (radius * 0.86f);
        const auto end = point + radial * (radius * (0.16f + 0.06f * static_cast<float>(lane % 2)));
        const auto bend = tangent * ((0.08f + motion * 0.18f) * radius * (lane % 2 == 0 ? 1.0f : -1.0f))
            + motionVector * (motionSpeed * 10.0f);
        juce::Path streamline;
        streamline.startNewSubPath(start);
        streamline.cubicTo(start + (end - start) * 0.34f + bend,
                           start + (end - start) * 0.72f - bend * 0.5f,
                           end);
        const auto streamlineAlpha = (0.008f + motion * 0.04f) * (hovered ? 1.1f : 1.0f);
        graphics.setColour((lane % 2 == 0 ? cyan : pink).withAlpha(streamlineAlpha));
        graphics.strokePath(streamline, juce::PathStrokeType(0.55f + motion * 0.4f));
    }
    graphics.restoreState();

    const auto centredX = coordinates.x * 2.0f - 1.0f;
    const auto centredY = coordinates.y * 2.0f - 1.0f;
    const auto normalizedRadius = std::sqrt(centredX * centredX + centredY * centredY);
    const auto boundary = juce::jlimit(0.0f, 1.0f, (normalizedRadius - 0.72f) / 0.28f);
    if (boundary > 0.0f)
    {
        graphics.setColour(pink.withAlpha((0.12f + boundary * 0.2f) * (0.4f + motion * 0.6f)));
        graphics.drawEllipse(circle.expanded(2.0f + boundary * 4.0f), 1.0f + boundary * 1.6f);
    }

    graphics.setColour(cyan.withAlpha(0.75f));
    graphics.drawLine(centre.x, circle.getY() + 3.0f, centre.x, centre.y - radius * 0.68f, 1.5f);
    graphics.setColour(pink.withAlpha(0.75f));
    graphics.drawLine(centre.x, centre.y + radius * 0.68f, centre.x, circle.getBottom() - 3.0f, 1.5f);

    // The trail is a connected, fading trajectory rather than a set of
    // decorative dots.  Its fixed storage keeps dragging allocation-free.
    for (std::size_t index = trailSize; index > 1; --index)
    {
        const auto& newer = trail[index - 2];
        const auto& older = trail[index - 1];
        const auto newerPoint = juce::Point<float>(circle.getX() + newer.value.x * circle.getWidth(),
                                                   circle.getBottom() - newer.value.y * circle.getHeight());
        const auto olderPoint = juce::Point<float>(circle.getX() + older.value.x * circle.getWidth(),
                                                   circle.getBottom() - older.value.y * circle.getHeight());
        const auto fade = juce::jlimit(0.0f, 1.0f, 1.0f - juce::jmax(newer.age, older.age));
        const auto glowAlpha = 0.05f + fade * (dragging ? 0.24f : 0.17f);
        graphics.setColour(cyan.withAlpha(glowAlpha * 0.55f));
        graphics.drawLine(olderPoint.x, olderPoint.y, newerPoint.x, newerPoint.y, 5.0f + fade * 3.0f);
        graphics.setColour((index % 2 == 0 ? cyan : pink).withAlpha(glowAlpha));
        graphics.drawLine(olderPoint.x, olderPoint.y, newerPoint.x, newerPoint.y, 1.0f + fade * 1.8f);
    }

    // Each meaningful movement emits a pair of expanding, deformed contours.
    // They are short-lived, so Release naturally returns to Idle.
    for (const auto& pulse : pulses)
    {
        if (pulse.age >= 1.0f)
            continue;

        const auto pulsePoint = juce::Point<float>(circle.getX() + pulse.value.x * circle.getWidth(),
                                                   circle.getBottom() - pulse.value.y * circle.getHeight());
        const auto remaining = 1.0f - pulse.age;
        const auto fade = remaining * remaining * pulse.strength;
        for (int ring = 0; ring < 2; ++ring)
        {
            const auto ringRadius = 8.0f + pulse.age * (24.0f + static_cast<float>(ring) * 17.0f);
            constexpr int sampleCount = 32;
            std::array<juce::Point<float>, sampleCount> samples {};
            for (int sample = 0; sample < sampleCount; ++sample)
            {
                const auto angle = juce::MathConstants<float>::twoPi * static_cast<float>(sample) / static_cast<float>(sampleCount);
                const auto directional = std::cos(angle - std::atan2(-pulse.direction.y, pulse.direction.x));
                const auto wobble = std::sin(angle * 3.0f + fieldPhase * 1.5f + static_cast<float>(ring)) * (1.2f + motion * 4.0f)
                    + directional * motionSpeed * 5.0f;
                const auto deformedRadius = ringRadius + pulse.age * wobble;
                samples[static_cast<std::size_t>(sample)] =
                    pulsePoint + juce::Point<float>(std::cos(angle), std::sin(angle)) * deformedRadius;
            }
            juce::Path wave;
            wave.startNewSubPath((samples[0] + samples[1]) * 0.5f);
            for (int sample = 1; sample <= sampleCount; ++sample)
            {
                const auto current = samples[static_cast<std::size_t>(sample % sampleCount)];
                const auto next = samples[static_cast<std::size_t>((sample + 1) % sampleCount)];
                wave.quadraticTo(current, (current + next) * 0.5f);
            }
            wave.closeSubPath();
            graphics.setColour((ring == 0 ? pink : cyan).withAlpha(fade * (ring == 0 ? 0.3f : 0.2f)));
            graphics.strokePath(wave, juce::PathStrokeType(ring == 0 ? 1.35f : 0.85f));
        }
    }

    juce::Path vector;
    vector.startNewSubPath(centre);
    vector.lineTo(point);
    graphics.setColour(cyan.withAlpha(dragging ? 0.72f : 0.42f + motion * 0.2f));
    graphics.strokePath(vector, juce::PathStrokeType(dragging ? 2.0f : 1.45f));

    // The visual core is intentionally compact.  The hit area remains the
    // generous 28 px region in mouseDown(), so precision and discoverability
    // are not traded for the smaller, more integrated appearance.
    const auto handleRadius = dragging ? 9.5f : (hovered ? 8.5f : 7.5f + motion * 0.7f);
    const auto coreBeat = 0.5f + 0.5f * std::sin(fieldPhase * 2.1f);
    const auto glowRadius = handleRadius * (1.65f + motion * 0.9f + coreBeat * 0.16f);
    juce::ColourGradient outerGlow(cyan.withAlpha(0.09f + motion * 0.09f),
                                   point.x - glowRadius * 0.45f, point.y - glowRadius * 0.55f,
                                   pink.withAlpha(0.0f), point.x + glowRadius, point.y + glowRadius, true);
    graphics.setGradientFill(outerGlow);
    graphics.fillEllipse(point.x - glowRadius, point.y - glowRadius, glowRadius * 2.0f, glowRadius * 2.0f);
    const auto shellRadius = handleRadius * 1.55f;
    juce::ColourGradient outerShell(pink.withAlpha(dragging ? 0.22f : 0.1f + motion * 0.08f),
                                    point.x - shellRadius * 0.65f, point.y - shellRadius * 0.7f,
                                    cyan.withAlpha(0.02f), point.x + shellRadius, point.y + shellRadius, true);
    graphics.setGradientFill(outerShell);
    graphics.fillEllipse(point.x - shellRadius, point.y - shellRadius, shellRadius * 2.0f, shellRadius * 2.0f);
    const auto innerShellRadius = handleRadius * 1.18f;
    juce::ColourGradient innerShell(cyan.withAlpha(0.14f + motion * 0.1f),
                                    point.x - innerShellRadius * 0.4f, point.y - innerShellRadius * 0.5f,
                                    pink.withAlpha(0.04f), point.x + innerShellRadius, point.y + innerShellRadius, true);
    graphics.setGradientFill(innerShell);
    graphics.fillEllipse(point.x - innerShellRadius, point.y - innerShellRadius,
                         innerShellRadius * 2.0f, innerShellRadius * 2.0f);
    graphics.setColour(cyan.withAlpha(0.72f));
    graphics.drawEllipse(point.x - handleRadius * 1.08f, point.y - handleRadius * 1.08f,
                         handleRadius * 2.16f, handleRadius * 2.16f, dragging ? 1.5f : 0.9f);
    const auto coreRadius = handleRadius * 0.76f;
    juce::ColourGradient coreGradient(palette::text().withAlpha(0.98f),
                                      point.x - coreRadius * 0.45f, point.y - coreRadius * 0.55f,
                                      pink.withAlpha(0.78f), point.x + coreRadius, point.y + coreRadius, true);
    graphics.setGradientFill(coreGradient);
    graphics.fillEllipse(point.x - coreRadius, point.y - coreRadius,
                         coreRadius * 2.0f, coreRadius * 2.0f);
    graphics.setColour(pink.withAlpha(0.96f));
    graphics.fillEllipse(point.x - handleRadius * 0.34f, point.y - handleRadius * 0.34f,
                         handleRadius * 0.68f, handleRadius * 0.68f);
    graphics.setColour(palette::text().withAlpha(0.55f + motion * 0.25f));
    const auto highlightOffset = juce::Point<float>(-handleRadius * 0.17f, -handleRadius * 0.19f);
    graphics.fillEllipse(point.x + highlightOffset.x - handleRadius * 0.09f,
                         point.y + highlightOffset.y - handleRadius * 0.09f,
                         handleRadius * 0.18f, handleRadius * 0.18f);

    const auto boundsInt = getLocalBounds();
    graphics.setFont(juce::FontOptions(10.0f).withStyle("Bold"));
    graphics.setColour(palette::text().withAlpha(0.52f));
    graphics.drawText("HYPER", boundsInt.getX() + 18, boundsInt.getY() + 12, 52, 14, juce::Justification::left, false);
    graphics.setColour(pink.withAlpha(0.7f));
    graphics.drawText("COLOR", boundsInt.getRight() - 66, boundsInt.getY() + 12, 50, 14, juce::Justification::right, false);
    graphics.setFont(juce::FontOptions(8.0f).withStyle("Bold"));
    graphics.setColour(cyan.withAlpha(0.62f));
    graphics.drawText("KINETIC CONTROL FIELD", boundsInt.getX() + 18, boundsInt.getBottom() - 32, 140, 13, juce::Justification::left, false);

    graphics.setFont(juce::FontOptions(9.0f).withStyle("Bold"));
    graphics.setColour(cyan);
    graphics.drawText("X", static_cast<int>(circle.getRight() - 10.0f), static_cast<int>(centre.y - 14.0f), 14, 16, juce::Justification::centred);
    graphics.setColour(pink);
    graphics.drawText("Y", static_cast<int>(centre.x + 7.0f), static_cast<int>(circle.getY() + 1.0f), 14, 16, juce::Justification::centred);
    graphics.setColour(palette::mutedText());
    graphics.drawFittedText(juce::String("X ") + juce::String(coordinates.x * 100.0f, 1) + "%   Y "
                                + juce::String(coordinates.y * 100.0f, 1) + "%",
                            getLocalBounds().removeFromBottom(21).reduced(10, 0),
                            juce::Justification::centred, 1);
}

void XYController::updateFromMouse(juce::Point<float> position, bool notify)
{
    const auto circle = circleBounds();
    if (circle.getWidth() <= 1.0f || circle.getHeight() <= 1.0f)
        return;

    const auto localX = (position.x - circle.getX()) / circle.getWidth();
    const auto localY = (circle.getBottom() - position.y) / circle.getHeight();
    setCoordinates({ localX, localY }, notify);
}

void XYController::mouseDown(const juce::MouseEvent& event)
{
    const auto point = pointForCoordinates();
    const auto distance = point.getDistanceFrom(event.position);
    dragging = distance <= 28.0f || circleBounds().contains(event.position);
    if (!dragging)
        return;

    dragOffset = distance <= 28.0f ? point - event.position : juce::Point<float> {};
    startMotionTimer();
    updateFromMouse(event.position + dragOffset, true);
    grabKeyboardFocus();
}

void XYController::mouseDrag(const juce::MouseEvent& event)
{
    if (dragging)
        updateFromMouse(event.position + dragOffset, true);
}

void XYController::mouseUp(const juce::MouseEvent&)
{
    dragging = false;
    dragOffset = {};
    if (trailSize > 0 || motionEnergy > 0.0f)
        startMotionTimer();
}
}
