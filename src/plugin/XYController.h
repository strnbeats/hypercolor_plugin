#pragma once

#include <JuceHeader.h>

#include <cmath>
#include <functional>

namespace hypercolor::ui
{
struct XYCoordinates
{
    float x = 0.5f;
    float y = 0.5f;
};

// Pure normalized-space projection. The input is not modified in place.
[[nodiscard]] inline XYCoordinates projectToUnitCircle(
    XYCoordinates coordinates
) noexcept
{
    if (!std::isfinite(coordinates.x) || !std::isfinite(coordinates.y))
        return {};

    const auto u = juce::jlimit(-1.0f, 1.0f, coordinates.x * 2.0f - 1.0f);
    const auto v = juce::jlimit(-1.0f, 1.0f, coordinates.y * 2.0f - 1.0f);
    const auto radiusSquared = u * u + v * v;

    if (!std::isfinite(radiusSquared))
        return {};

    if (radiusSquared > 1.0f)
    {
        const auto radius = std::sqrt(radiusSquared);
        if (!std::isfinite(radius) || radius <= 0.0f)
            return {};

        return {
            0.5f + 0.5f * u / radius,
            0.5f + 0.5f * v / radius
        };
    }

    return { 0.5f + 0.5f * u, 0.5f + 0.5f * v };
}

class XYController final : public juce::Component
{
public:
    XYController();

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

    void setCoordinates(XYCoordinates coordinates, bool notify = true) noexcept;
    [[nodiscard]] XYCoordinates getCoordinates() const noexcept { return coordinates; }
    void setOnChange(std::function<void(XYCoordinates)> callback);

private:
    void updateFromMouse(juce::Point<float> position, bool notify);
    [[nodiscard]] juce::Rectangle<float> circleBounds() const noexcept;
    [[nodiscard]] juce::Point<float> pointForCoordinates() const noexcept;

    XYCoordinates coordinates {};
    std::function<void(XYCoordinates)> onChange;
    juce::Point<float> dragOffset {};
    bool dragging = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(XYController)
};
}
