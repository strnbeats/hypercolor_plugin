#pragma once

#include <JuceHeader.h>
#include "GranularVisualizer.h"
#include "HypercolorLookAndFeel.h"
#include "PremiumRotaryKnob.h"
#include "PluginProcessor.h"
#include "PresetManager.h"
#include "XYController.h"

#include <array>
#include <atomic>
#include <memory>

class HypercolorAudioProcessorEditor final
    : public juce::AudioProcessorEditor,
      private juce::AudioProcessorValueTreeState::Listener,
      private juce::AsyncUpdater,
      private juce::Timer
{
public:
    explicit HypercolorAudioProcessorEditor(
        HypercolorAudioProcessor& processorToEdit
    );
    ~HypercolorAudioProcessorEditor() override;

    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

private:
    enum class DensityMode
    {
        free,
        bpmSync
    };

    void selectGranular();
    void selectEffect(std::size_t rackIndex);
    void showAddEffectMenu();
    void showLfoWorkspace();
    void addEffect(hypercolor::plugin::FxType type);
    void removeEffect(std::size_t rackIndex);
    void moveEffect(std::size_t from, std::size_t to);
    void refreshFxRack();
    void rebuildInspector();
    void rebindDensityControl(DensityMode mode);
    [[nodiscard]] DensityMode readDensityMode() const noexcept;
    void synchronizeDensityMode();
    void updateEffectVisualTargets(bool snapToTarget);
    void showPresetBrowser();
    void showSaveAs();
    void saveCurrentPreset();
    void refreshPresetHeader();
    void timerCallback() override;
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
    std::unique_ptr<hypercolor::plugin::PresetManager> presetManager;
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
    juce::ComboBox sourceModeSelector;
    juce::TextButton macroAssignmentButton { "MACRO MAP" };

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
    std::array<juce::TextButton, hypercolor::plugin::maximumFxChainModules> effectButtons;
    std::array<juce::TextButton, hypercolor::plugin::maximumFxChainModules> fxEnableButtons;
    std::array<juce::TextButton, hypercolor::plugin::maximumFxChainModules> fxRemoveButtons;
    juce::TextButton addFxButton;
    juce::TextButton lfoWorkspaceButton { "MODULATION / LFO" };
    juce::TextButton previousPresetButton { "‹" };
    juce::TextButton nextPresetButton { "›" };
    juce::TextButton presetBrowserButton { "PRESETS" };
    juce::TextButton presetSaveButton { "SAVE" };
    juce::TextButton presetSaveAsButton { "SAVE AS" };
    juce::Label presetNameLabel;
    std::array<juce::Slider, 9> inspectorSliders;
    std::array<hypercolor::ui::PremiumRotaryKnob, 9> fxRotaryKnobs;
    std::array<juce::Label, 9> inspectorLabels;
    std::array<std::unique_ptr<SliderAttachment>, 9> inspectorAttachments;
    std::array<float, 9> effectVisualCurrent {};
    std::array<float, 9> effectVisualTarget {};
    std::size_t selectedEffect = 0;
    std::uint8_t selectedEffectSlot = 0;
    std::size_t selectedRackIndex = 0;
    int draggingRackIndex = -1;
    int dropRackIndex = -1;
    int selectedModule = -1;
    std::uint64_t focusedLfoIdentity = 0;

    SliderAttachment dryWetAttachment;
    SliderAttachment outputGainAttachment;
    std::unique_ptr<SliderAttachment> densityAttachment;
    SliderAttachment sizeAttachment;
    SliderAttachment positionAttachment;
    SliderAttachment pitchAttachment;
    ButtonAttachment reverseAttachment;
    std::array<std::unique_ptr<ButtonAttachment>, hypercolor::plugin::maximumFxChainModules> fxEnableAttachments;
    std::unique_ptr<ComboBoxAttachment> densityModeAttachment;
    std::unique_ptr<ComboBoxAttachment> sourceModeAttachment;

    std::atomic<int> pendingDensityMode { -1 };
    std::atomic<float> pendingMacroX { 0.5f };
    std::atomic<float> pendingMacroY { 0.5f };
    DensityMode appliedDensityMode = DensityMode::free;
    bool densityRebindDeferred = false;
    juce::TooltipWindow tooltipWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(
        HypercolorAudioProcessorEditor
    )
};
