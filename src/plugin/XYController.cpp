#include "XYController.h"

#include "HypercolorLookAndFeel.h"

#include <algorithm>

namespace hypercolor::ui
{
XYController::XYController()
{
    setComponentID("xyController");
    setWantsKeyboardFocus(true);
}

void XYController::setOnChange(std::function<void(XYCoordinates)> callback)
{
    onChange = std::move(callback);
}

void XYController::setCoordinates(XYCoordinates newCoordinates, bool notify) noexcept
{
    const auto projected = projectToUnitCircle(newCoordinates);
    const auto changed = std::abs(projected.x - coordinates.x) > 0.000001f
        || std::abs(projected.y - coordinates.y) > 0.000001f;
    coordinates = projected;
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

void XYController::paint(juce::Graphics& graphics)
{
    const auto bounds = getLocalBounds().toFloat();
    graphics.setColour(palette::panelRaised().withAlpha(0.56f));
    graphics.fillRoundedRectangle(bounds.reduced(1.0f), 14.0f);
    graphics.setColour(palette::cyan().withAlpha(0.08f));
    graphics.drawRoundedRectangle(bounds.reduced(1.0f), 14.0f, 1.0f);

    const auto circle = circleBounds();
    const auto centre = circle.getCentre();
    const auto radius = circle.getWidth() * 0.5f;

    graphics.setColour(palette::grid().withAlpha(0.86f));
    graphics.drawEllipse(circle, 1.2f);
    for (const auto fraction : { 0.25f, 0.5f, 0.75f })
    {
        const auto inset = radius * (1.0f - fraction);
        graphics.drawEllipse(circle.reduced(inset), 0.6f);
    }
    graphics.drawLine(centre.x - radius, centre.y, centre.x + radius, centre.y, 0.7f);
    graphics.drawLine(centre.x, centre.y - radius, centre.x, centre.y + radius, 0.7f);

    graphics.setColour(palette::cyan().withAlpha(0.6f));
    graphics.drawLine(centre.x, circle.getY(), centre.x, centre.y - radius * 0.76f, 1.4f);
    graphics.setColour(palette::pink().withAlpha(0.6f));
    graphics.drawLine(centre.x, centre.y + radius * 0.76f, centre.x, circle.getBottom(), 1.4f);

    const auto point = pointForCoordinates();
    juce::Path vector;
    vector.startNewSubPath(centre);
    vector.lineTo(point);
    graphics.setColour(palette::cyan().withAlpha(0.38f));
    graphics.strokePath(vector, juce::PathStrokeType(1.5f));

    graphics.setColour(palette::pink().withAlpha(0.18f));
    graphics.fillEllipse(point.x - 20.0f, point.y - 20.0f, 40.0f, 40.0f);
    graphics.setColour(palette::cyan().withAlpha(0.22f));
    graphics.drawEllipse(point.x - 13.0f, point.y - 13.0f, 26.0f, 26.0f, 1.0f);
    graphics.setColour(palette::text());
    graphics.drawEllipse(point.x - 10.0f, point.y - 10.0f, 20.0f, 20.0f, 1.6f);
    graphics.setColour(palette::pink());
    graphics.fillEllipse(point.x - 5.0f, point.y - 5.0f, 10.0f, 10.0f);

    // Give the open space around the field a restrained brand signature.  It
    // keeps the main header compact while making the XY surface feel like a
    // deliberate instrument face rather than an empty panel.
    const auto boundsInt = getLocalBounds();
    graphics.setFont(juce::FontOptions(12.0f).withStyle("Bold"));
    graphics.setColour(palette::text().withAlpha(0.18f));
    graphics.drawText(
        "HYPER",
        boundsInt.getX() + 18,
        boundsInt.getY() + 12,
        58,
        16,
        juce::Justification::left,
        false
    );
    graphics.setColour(palette::pink().withAlpha(0.26f));
    graphics.drawText(
        "COLOR",
        boundsInt.getRight() - 70,
        boundsInt.getY() + 12,
        54,
        16,
        juce::Justification::right,
        false
    );
    graphics.setFont(juce::FontOptions(8.0f).withStyle("Bold"));
    graphics.setColour(palette::cyan().withAlpha(0.52f));
    graphics.drawText(
        "CHARACTER FIELD // XY",
        boundsInt.getX() + 18,
        boundsInt.getBottom() - 39,
        118,
        14,
        juce::Justification::left,
        false
    );

    graphics.setFont(juce::FontOptions(9.0f).withStyle("Bold"));
    graphics.setColour(palette::cyan());
    graphics.drawText("X", static_cast<int>(circle.getRight() - 12.0f), static_cast<int>(centre.y - 14.0f), 14, 16, juce::Justification::centred);
    graphics.setColour(palette::pink());
    graphics.drawText("Y", static_cast<int>(centre.x + 7.0f), static_cast<int>(circle.getY() + 1.0f), 14, 16, juce::Justification::centred);
    graphics.setColour(palette::mutedText());
    graphics.drawFittedText(
        juce::String("X ") + juce::String(coordinates.x * 100.0f, 1) + "%   Y "
            + juce::String(coordinates.y * 100.0f, 1) + "%",
        getLocalBounds().removeFromBottom(21).reduced(10, 0),
        juce::Justification::centred,
        1
    );
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
}
}
