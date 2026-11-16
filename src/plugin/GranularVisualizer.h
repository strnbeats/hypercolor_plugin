#pragma once

#include <JuceHeader.h>

#include "PluginProcessor.h"

class GranularVisualizer final
    : public juce::Component,
      private juce::Timer
{
public:
    static constexpr auto componentID = "granularVisualizer";

    explicit GranularVisualizer(HypercolorAudioProcessor& processorToObserve);
    ~GranularVisualizer() override;

    void paint(juce::Graphics& graphics) override;

    // Bounded message-thread operation. Public to allow deterministic UI
    // lifecycle tests without waiting for an asynchronous timer callback.
    [[nodiscard]] bool consumePendingTelemetry() noexcept;
    [[nodiscard]] bool copyLatestSnapshot(
        hypercolor::dsp::granular::GranularTelemetrySnapshot& destination
    ) const noexcept;

private:
    void timerCallback() override;

    void drawWaveform(
        juce::Graphics& graphics,
        juce::Rectangle<float> bounds
    ) const;
    void drawGrains(
        juce::Graphics& graphics,
        juce::Rectangle<float> bounds
    ) const;

    HypercolorAudioProcessor& audioProcessor;
    hypercolor::dsp::granular::GranularTelemetrySnapshot latestSnapshot {};
    bool hasSnapshot = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GranularVisualizer)
};
