#pragma once

#include <JuceHeader.h>

namespace hypercolor::ui
{
/**
    A reusable instrument-style rotary control.

    The class deliberately remains a juce::Slider so existing APVTS
    SliderAttachments, automation, keyboard handling and value editing keep
    their normal behaviour.  Only the visual surface is specialised here.
*/
class PremiumRotaryKnob final : public juce::Slider, private juce::Timer
{
public:
    PremiumRotaryKnob();
    ~PremiumRotaryKnob() override;

    void setAccentColour(juce::Colour colour);
    void setSecondaryAccentColour(juce::Colour colour);
    void setArcGradientColours(juce::Colour start,
                               juce::Colour middle,
                               juce::Colour end);
    void setArcContrastColour(juce::Colour colour);
    void setVisualScale(float scale);
    [[nodiscard]] juce::Colour getAccentColour() const noexcept { return accentColour; }

    void paint(juce::Graphics&) override;
    void mouseEnter(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    void timerCallback() override;

    juce::Colour accentColour;
    juce::Colour arcStartColour;
    juce::Colour arcMiddleColour;
    juce::Colour arcEndColour;
    juce::Colour arcContrastColour;
    juce::Colour secondaryAccentColour;
    float interactionEnergy = 0.0f;
    float arcPhase = 0.0f;
    double lastAnimationTimeMs = 0.0;
    float visualScale = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PremiumRotaryKnob)
};
}
