#pragma once

#include <JuceHeader.h>

#include <array>
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

class XYController final : public juce::Component, private juce::Timer
{
public:
    XYController();
    ~XYController() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseEnter(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

    void setCoordinates(XYCoordinates coordinates, bool notify = true) noexcept;
    [[nodiscard]] XYCoordinates getCoordinates() const noexcept { return coordinates; }
    void setOnChange(std::function<void(XYCoordinates)> callback);

private:
    static constexpr std::size_t trailCapacity = 24;
    static constexpr std::size_t pulseCapacity = 6;
    static constexpr std::size_t particleCapacity = 20;

    struct TrailPoint
    {
        XYCoordinates value {};
        float age = 1.0f;
    };

    struct MotionPulse
    {
        XYCoordinates value {};
        XYCoordinates direction {};
        float age = 1.0f;
        float strength = 0.0f;
    };

    struct MicroParticle
    {
        float phase = 0.0f;
        float speed = 0.04f;
        float size = 1.2f;
        float alpha = 0.24f;
        float trail = 0.0f;
        int lane = 0;
        float direction = 1.0f;
        bool vertical = false;
    };

    void updateFromMouse(juce::Point<float> position, bool notify);
    void timerCallback() override;
    void startMotionTimer() noexcept;
    void pushTrail(XYCoordinates value, float distance) noexcept;
    void pushPulse(XYCoordinates value, float distance) noexcept;
    [[nodiscard]] juce::Rectangle<float> circleBounds() const noexcept;
    [[nodiscard]] juce::Point<float> pointForCoordinates() const noexcept;

    XYCoordinates coordinates {};
    std::function<void(XYCoordinates)> onChange;
    juce::Point<float> dragOffset {};
    std::array<TrailPoint, trailCapacity> trail {};
    std::size_t trailSize = 0;
    std::array<MotionPulse, pulseCapacity> pulses {};
    std::size_t nextPulse = 0;
    std::array<MicroParticle, particleCapacity> particles {};
    float motionEnergy = 0.0f;
    XYCoordinates motionDirection {};
    XYCoordinates targetMotionDirection {};
    float motionSpeed = 0.0f;
    float fieldPhase = 0.0f;
    bool hovered = false;
    bool dragging = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(XYController)
};
}
