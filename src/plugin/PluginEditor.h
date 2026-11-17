#pragma once

#include <JuceHeader.h>
#include "GranularVisualizer.h"
#include "HypercolorLookAndFeel.h"
#include "PremiumRotaryKnob.h"
#include "PluginProcessor.h"
#include "XYController.h"

#include <array>
#include <atomic>
#include <memory>

class HypercolorAudioProcessorEditor final
    : public juce::AudioProcessorEditor,
      private juce::AudioProcessorValueTreeState::Listener,
      private juce::AsyncUpdater
{
public:
    explicit HypercolorAudioProcessorEditor(
        HypercolorAudioProcessor& processorToEdit
    );
    ~HypercolorAudioProcessorEditor() override;

    void paint(juce::Graphics& graphics) override;
    void resized() override;

private:
    enum class DensityMode
    {
        free,
        bpmSync
    };

    void selectGranular();
    void selectEffect(std::size_t index);
    void rebuildInspector();
    void rebindDensityControl(DensityMode mode);
    [[nodiscard]] DensityMode readDensityMode() const noexcept;
    void synchronizeDensityMode();
    void parameterChanged(const juce::String& parameterID,
                          float newValue) override;
    void handleAsyncUpdate() override;

private:
    using SliderAttachment =
        juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment =
        juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboBoxAttachment =
        juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    HypercolorAudioProcessor& audioProcessor;
    hypercolor::ui::HypercolorLookAndFeel lookAndFeel;
    GranularVisualizer granularVisualizer;
    hypercolor::ui::XYController xyController;

    juce::Rectangle<int> headerArea;
    juce::Rectangle<int> grainControlPanel;
    juce::Rectangle<int> granularControlsArea;
    juce::Rectangle<int> inspectorPanel;
    juce::Rectangle<int> rackPanel;
    juce::Rectangle<int> masterPanel;

    hypercolor::ui::PremiumRotaryKnob dryWetSlider;
    hypercolor::ui::PremiumRotaryKnob outputGainSlider;
    hypercolor::ui::PremiumRotaryKnob densitySlider;
    hypercolor::ui::PremiumRotaryKnob sizeSlider;
    hypercolor::ui::PremiumRotaryKnob positionSlider;
    hypercolor::ui::PremiumRotaryKnob pitchSlider;
    juce::TextButton reverseButton;
    juce::ComboBox densityModeSelector;

    juce::Label dryWetLabel;
    juce::Label outputGainLabel;
    juce::Label densityLabel;
    juce::Label sizeLabel;
    juce::Label positionLabel;
    juce::Label pitchLabel;
    juce::Label reverseLabel;
    juce::Label granularSectionLabel;
    juce::Label masterSectionLabel;
    juce::Label inspectorSectionLabel;
    juce::Label inspectorHintLabel;
    juce::Label fxRackLabel;
    juce::Label moduleTitleLabel;
    juce::Label moduleSubtitleLabel;

    juce::TextButton granularNavButton;
    std::array<juce::TextButton, 8> effectButtons;
    std::array<juce::Slider, 9> inspectorSliders;
    std::array<juce::Label, 9> inspectorLabels;
    std::array<std::unique_ptr<SliderAttachment>, 9> inspectorAttachments;
    std::size_t selectedEffect = 0;
    int selectedModule = -1;

    SliderAttachment dryWetAttachment;
    SliderAttachment outputGainAttachment;
    std::unique_ptr<SliderAttachment> densityAttachment;
    SliderAttachment sizeAttachment;
    SliderAttachment positionAttachment;
    SliderAttachment pitchAttachment;
    ButtonAttachment reverseAttachment;
    std::unique_ptr<ComboBoxAttachment> densityModeAttachment;

    std::atomic<int> pendingDensityMode { -1 };
    DensityMode appliedDensityMode = DensityMode::free;
    bool densityRebindDeferred = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(
        HypercolorAudioProcessorEditor
    )
};
