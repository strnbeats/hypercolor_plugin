#pragma once

#include <JuceHeader.h>

namespace hypercolor::ui
{
namespace palette
{
inline juce::Colour background() noexcept { return juce::Colour(0xff090a0fu); }
inline juce::Colour panel() noexcept { return juce::Colour(0xff191b25u); }
inline juce::Colour panelRaised() noexcept { return juce::Colour(0xff20232fu); }
inline juce::Colour border() noexcept { return juce::Colour(0xff343848u); }
inline juce::Colour grid() noexcept { return juce::Colour(0xff2a2e3bu); }
inline juce::Colour cyan() noexcept { return juce::Colour(0xff00e5ffu); }
inline juce::Colour pink() noexcept { return juce::Colour(0xffff3cacu); }
inline juce::Colour lime() noexcept { return juce::Colour(0xffd6ff3fu); }
inline juce::Colour text() noexcept { return juce::Colour(0xffedf1f7u); }
inline juce::Colour mutedText() noexcept { return juce::Colour(0xff858b9eu); }
}

class HypercolorLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    HypercolorLookAndFeel();

    void drawRotarySlider(
        juce::Graphics& graphics,
        int x,
        int y,
        int width,
        int height,
        float sliderPosition,
        float rotaryStartAngle,
        float rotaryEndAngle,
        juce::Slider& slider
    ) override;

    void drawButtonBackground(
        juce::Graphics& graphics,
        juce::Button& button,
        const juce::Colour& backgroundColour,
        bool shouldDrawButtonAsHighlighted,
        bool shouldDrawButtonAsDown
    ) override;

    void drawButtonText(
        juce::Graphics& graphics,
        juce::TextButton& button,
        bool shouldDrawButtonAsHighlighted,
        bool shouldDrawButtonAsDown
    ) override;

    [[nodiscard]] juce::Font getTextButtonFont(
        juce::TextButton& button,
        int buttonHeight
    ) override;

    [[nodiscard]] juce::Font getLabelFont(juce::Label& label) override;
};
}
