#include "PluginEditor.h"
#include "PresetManager.h"

#include <array>
#include <cmath>
#include <cstring>
#include <vector>

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

class FxMacroAssignmentPanel final : public juce::Component
{
public:
    explicit FxMacroAssignmentPanel(HypercolorAudioProcessor& processor)
        : processorToEdit(processor)
    {
        setSize(430, 330);
        title.setText("XY MACRO ASSIGNMENTS", juce::dontSendNotification);
        title.setColour(juce::Label::textColourId, hypercolor::ui::palette::text());
        addAndMakeVisible(title);
        axis.addItem("MACRO X", 1);
        axis.addItem("MACRO Y", 2);
        addAndMakeVisible(axis);
        addAndMakeVisible(instance);
        addAndMakeVisible(parameter);
        addAndMakeVisible(assignment);
        for (auto* combo : { &axis, &instance, &parameter, &assignment })
        {
            combo->setColour(juce::ComboBox::backgroundColourId, hypercolor::ui::palette::panelRaised());
            combo->setColour(juce::ComboBox::textColourId, hypercolor::ui::palette::text());
            combo->setColour(juce::ComboBox::outlineColourId, hypercolor::ui::palette::border());
            combo->setColour(juce::ComboBox::arrowColourId, hypercolor::ui::palette::cyan());
        }
        configureRangeSlider(minimum, "MIN");
        configureRangeSlider(maximum, "MAX");
        minLabel.setText("MIN", juce::dontSendNotification);
        maxLabel.setText("MAX", juce::dontSendNotification);
        minLabel.setColour(juce::Label::textColourId, hypercolor::ui::palette::mutedText());
        maxLabel.setColour(juce::Label::textColourId, hypercolor::ui::palette::mutedText());
        addAndMakeVisible(minLabel);
        addAndMakeVisible(maxLabel);
        addAndMakeVisible(addOrUpdate);
        addAndMakeVisible(remove);
        addOrUpdate.setColour(juce::TextButton::buttonColourId, hypercolor::ui::palette::panelRaised());
        addOrUpdate.setColour(juce::TextButton::textColourOffId, hypercolor::ui::palette::cyan());
        remove.setColour(juce::TextButton::buttonColourId, hypercolor::ui::palette::panelRaised());
        remove.setColour(juce::TextButton::textColourOffId, hypercolor::ui::palette::mutedText());
        addAndMakeVisible(hint);
        hint.setText("XY is active while mapped; the rotary value remains stored as the base.",
                     juce::dontSendNotification);
        hint.setColour(juce::Label::textColourId, hypercolor::ui::palette::mutedText());
        axis.onChange = [this] { updateHint(); updateSaveButtonLabel(); };
        instance.onChange = [this] { rebuildParameters(); };
        parameter.onChange = [this] { updateSaveButtonLabel(); };
        assignment.onChange = [this] { loadSelectedAssignment(); };
        minimum.onValueChange = [this]
        {
            maximum.setRange(minimum.getValue(), maximum.getMaximum(), maximum.getInterval());
        };
        maximum.onValueChange = [this]
        {
            minimum.setRange(minimum.getMinimum(), maximum.getValue(), minimum.getInterval());
        };
        addOrUpdate.onClick = [this] { saveAssignment(); };
        remove.onClick = [this] { removeAssignment(); };
        refreshAll();
    }

    void paint(juce::Graphics& graphics) override
    {
        auto bounds = getLocalBounds().toFloat();
        graphics.setColour(hypercolor::ui::palette::panel().darker(0.1f));
        graphics.fillRoundedRectangle(bounds, 8.0f);
        graphics.setColour(hypercolor::ui::palette::border());
        graphics.drawRoundedRectangle(bounds.reduced(0.5f), 8.0f, 1.0f);
        graphics.setColour(hypercolor::ui::palette::cyan());
        graphics.fillRect(0.0f, 0.0f, 52.0f, 2.0f);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(14, 12);
        title.setBounds(area.removeFromTop(23));
        area.removeFromTop(9);
        auto assignmentRow = area.removeFromTop(28);
        assignmentRow.removeFromLeft(4);
        assignment.setBounds(assignmentRow);
        area.removeFromTop(9);
        auto axisRow = area.removeFromTop(28);
        axis.setBounds(axisRow.removeFromLeft(104));
        axisRow.removeFromLeft(8);
        instance.setBounds(axisRow);
        area.removeFromTop(8);
        parameter.setBounds(area.removeFromTop(28));
        area.removeFromTop(8);
        auto ranges = area.removeFromTop(54);
        auto minColumn = ranges.removeFromLeft(ranges.getWidth() / 2);
        minLabel.setBounds(minColumn.removeFromLeft(28));
        minimum.setBounds(minColumn.reduced(1, 0));
        maxLabel.setBounds(ranges.removeFromLeft(28));
        maximum.setBounds(ranges.reduced(1, 0));
        area.removeFromTop(7);
        auto buttons = area.removeFromTop(30);
        addOrUpdate.setBounds(buttons.removeFromLeft(buttons.getWidth() / 2 - 4));
        buttons.removeFromLeft(8);
        remove.setBounds(buttons);
        area.removeFromTop(5);
        hint.setBounds(area.removeFromTop(28));
    }

private:
    void configureRangeSlider(juce::Slider& slider, const juce::String& name)
    {
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 76, 20);
        slider.setName(name);
        slider.setColour(juce::Slider::trackColourId, hypercolor::ui::palette::cyan());
        slider.setColour(juce::Slider::thumbColourId, hypercolor::ui::palette::text());
        slider.setColour(juce::Slider::textBoxTextColourId, hypercolor::ui::palette::text());
        slider.setColour(juce::Slider::textBoxBackgroundColourId, hypercolor::ui::palette::background());
        slider.setColour(juce::Slider::textBoxOutlineColourId, hypercolor::ui::palette::border());
        slider.setRange(0.0, 1.0, 0.001);
        addAndMakeVisible(slider);
    }

    void refreshAll()
    {
        const auto oldIdentity = selectedIdentity;
        instance.clear(juce::dontSendNotification);
        instances.clear();
        const auto chain = processorToEdit.getFxChainConfiguration();
        for (std::size_t i = 0; i < chain.count; ++i)
        {
            const auto id = chain.order[i];
            instances.push_back(id);
            const auto name = effectNames[hypercolor::plugin::toIndex(id.type)];
            instance.addItem(juce::String(name) + " #" + juce::String(static_cast<int>(id.slot + 1)),
                             static_cast<int>(instances.size()));
        }
        auto selectedIndex = 0;
        for (std::size_t i = 0; i < instances.size(); ++i)
            if (instances[i].stableIdentity == oldIdentity)
                selectedIndex = static_cast<int>(i + 1);
        instance.setSelectedId(selectedIndex == 0 && instance.getNumItems() > 0 ? 1 : selectedIndex,
                               juce::dontSendNotification);
        rebuildParameters();
        rebuildAssignments();
    }

    void rebuildParameters()
    {
        parameter.clear(juce::dontSendNotification);
        parameterIDs.clear();
        const auto selected = instance.getSelectedId() - 1;
        if (selected >= 0 && static_cast<std::size_t>(selected) < instances.size())
        {
            const auto id = instances[static_cast<std::size_t>(selected)];
            const auto type = hypercolor::plugin::toIndex(id.type);
            for (std::size_t i = 0; i < effectParameterCounts[type]; ++i)
            {
                const auto legacy = effectParameters[type][i];
                if (legacy == nullptr)
                    continue;
                const auto parameterID = hypercolor::plugin::fxParameterID(id.type, id.slot, legacy);
                auto* candidate = processorToEdit.getValueTreeState().getParameter(parameterID);
                auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(candidate);
                if (ranged == nullptr)
                    continue;
                parameterIDs.push_back(parameterID);
                parameter.addItem(candidate->getName(36), static_cast<int>(parameterIDs.size()));
            }
        }
        parameter.setSelectedId(parameter.getNumItems() > 0 ? 1 : 0,
                                juce::dontSendNotification);
        const auto selectedParam = parameter.getSelectedId() - 1;
        if (selectedParam >= 0 && static_cast<std::size_t>(selectedParam) < parameterIDs.size())
        {
            auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(
                processorToEdit.getValueTreeState().getParameter(parameterIDs[static_cast<std::size_t>(selectedParam)]));
            const auto range = ranged->getNormalisableRange();
            minimum.setRange(range.start, range.end, range.interval);
            maximum.setRange(range.start, range.end, range.interval);
            minimum.setValue(range.start, juce::dontSendNotification);
            maximum.setValue(range.end, juce::dontSendNotification);
        }
        addOrUpdate.setEnabled(!parameterIDs.empty());
        updateSaveButtonLabel();
        updateHint();
    }

    void rebuildAssignments()
    {
        assignment.clear(juce::dontSendNotification);
        const auto stored = processorToEdit.getFxMacroAssignments();
        for (std::size_t i = 0; i < stored.count; ++i)
        {
            const auto& item = stored.assignments[i];
            juce::String effect = "Missing FX";
            for (std::size_t row = 0; row < instances.size(); ++row)
                if (instances[row].stableIdentity == item.instanceIdentity)
                    effect = juce::String(effectNames[hypercolor::plugin::toIndex(instances[row].type)])
                        + " #" + juce::String(static_cast<int>(instances[row].slot + 1));
            const auto* parameterObject = processorToEdit.getValueTreeState().getParameter(item.parameterID);
            const auto parameterName = parameterObject != nullptr ? parameterObject->getName(32) : item.parameterID;
            assignment.addItem(juce::String(item.axis == hypercolor::plugin::MacroAxis::x ? "X" : "Y")
                + "  ·  " + effect + "  ·  " + parameterName,
                static_cast<int>(i + 1));
        }
        if (stored.count > 0 && assignment.getSelectedId() == 0)
            assignment.setSelectedId(1, juce::dontSendNotification);
        remove.setEnabled(stored.count > 0 && assignment.getSelectedId() > 0);
        addOrUpdate.setButtonText("ADD ASSIGNMENT");
    }

    void loadSelectedAssignment()
    {
        const auto selected = assignment.getSelectedId() - 1;
        const auto stored = processorToEdit.getFxMacroAssignments();
        if (selected < 0 || static_cast<std::size_t>(selected) >= stored.count)
        {
            remove.setEnabled(false);
            addOrUpdate.setButtonText("ADD ASSIGNMENT");
            return;
        }
        const auto& item = stored.assignments[static_cast<std::size_t>(selected)];
        const auto selectedAssignmentId = selected + 1;
        axis.setSelectedId(item.axis == hypercolor::plugin::MacroAxis::x ? 1 : 2,
                           juce::dontSendNotification);
        selectedIdentity = item.instanceIdentity;
        refreshAll();
        assignment.setSelectedId(selectedAssignmentId, juce::dontSendNotification);
        for (std::size_t row = 0; row < instances.size(); ++row)
            if (instances[row].stableIdentity == item.instanceIdentity)
                instance.setSelectedId(static_cast<int>(row + 1), juce::dontSendNotification);
        rebuildParameters();
        for (std::size_t i = 0; i < parameterIDs.size(); ++i)
            if (parameterIDs[i] == item.parameterID)
                parameter.setSelectedId(static_cast<int>(i + 1), juce::dontSendNotification);
        minimum.setValue(item.minimum, juce::dontSendNotification);
        maximum.setValue(item.maximum, juce::dontSendNotification);
        addOrUpdate.setButtonText("UPDATE ASSIGNMENT");
        updateSaveButtonLabel();
        remove.setEnabled(true);
        updateHint();
    }

    void updateSaveButtonLabel()
    {
        const auto selectedInstance = instance.getSelectedId() - 1;
        const auto selectedParameter = parameter.getSelectedId() - 1;
        const auto stored = processorToEdit.getFxMacroAssignments();
        bool matchesExisting = false;
        if (selectedInstance >= 0 && static_cast<std::size_t>(selectedInstance) < instances.size()
            && selectedParameter >= 0 && static_cast<std::size_t>(selectedParameter) < parameterIDs.size())
        {
            const auto identity = instances[static_cast<std::size_t>(selectedInstance)].stableIdentity;
            const auto& parameterID = parameterIDs[static_cast<std::size_t>(selectedParameter)];
            for (std::size_t i = 0; i < stored.count; ++i)
                matchesExisting = matchesExisting
                    || (stored.assignments[i].instanceIdentity == identity
                        && stored.assignments[i].parameterID == parameterID);
        }
        addOrUpdate.setButtonText(matchesExisting ? "UPDATE ASSIGNMENT" : "ADD ASSIGNMENT");
    }

    void saveAssignment()
    {
        const auto selectedInstance = instance.getSelectedId() - 1;
        const auto selectedParameter = parameter.getSelectedId() - 1;
        if (selectedInstance < 0 || static_cast<std::size_t>(selectedInstance) >= instances.size()
            || selectedParameter < 0 || static_cast<std::size_t>(selectedParameter) >= parameterIDs.size())
            return;
        auto stored = processorToEdit.getFxMacroAssignments();
        const auto identity = instances[static_cast<std::size_t>(selectedInstance)].stableIdentity;
        const auto& id = parameterIDs[static_cast<std::size_t>(selectedParameter)];
        int replace = -1;
        for (std::size_t i = 0; i < stored.count; ++i)
            if (stored.assignments[i].instanceIdentity == identity
                && stored.assignments[i].parameterID == id)
                replace = static_cast<int>(i);
        if (replace < 0 || static_cast<std::size_t>(replace) >= stored.count)
        {
            if (stored.count >= hypercolor::plugin::maximumFxMacroAssignments)
                return;
            replace = stored.count++;
        }
        stored.assignments[static_cast<std::size_t>(replace)] = {
            axis.getSelectedId() == 2 ? hypercolor::plugin::MacroAxis::y : hypercolor::plugin::MacroAxis::x,
            identity, id, static_cast<float>(minimum.getValue()), static_cast<float>(maximum.getValue())
        };
        if (processorToEdit.setFxMacroAssignments(stored))
            rebuildAssignments();
    }

    void removeAssignment()
    {
        auto stored = processorToEdit.getFxMacroAssignments();
        const auto selected = assignment.getSelectedId() - 1;
        if (selected < 0 || static_cast<std::size_t>(selected) >= stored.count)
            return;
        for (std::size_t i = static_cast<std::size_t>(selected) + 1; i < stored.count; ++i)
            stored.assignments[i - 1] = stored.assignments[i];
        --stored.count;
        if (processorToEdit.setFxMacroAssignments(stored))
            rebuildAssignments();
    }

    void updateHint()
    {
        hint.setText(axis.getSelectedId() == 2
            ? "Macro Y follows the vertical XY coordinate."
            : "Macro X follows the horizontal XY coordinate.", juce::dontSendNotification);
    }

    HypercolorAudioProcessor& processorToEdit;
    juce::Label title, hint, minLabel, maxLabel;
    juce::ComboBox axis, instance, parameter, assignment;
    juce::Slider minimum, maximum;
    juce::TextButton addOrUpdate { "ADD ASSIGNMENT" }, remove { "REMOVE" };
    std::vector<hypercolor::plugin::FxInstanceId> instances;
    std::vector<juce::String> parameterIDs;
    std::uint64_t selectedIdentity = 0;
};

class LfoWavePreview final : public juce::Component
{
public:
    using Waveform = hypercolor::dsp::modulation::Lfo::CustomWaveform;
    void setState(hypercolor::dsp::modulation::LfoWaveform value,
                  const Waveform& custom, float depthValue, float offsetValue,
                  float phaseValue, bool enabledValue, bool usedValue)
    {
        waveform = value; customWaveform = custom;
        depth = juce::jlimit(0.0f, 1.0f, depthValue);
        offset = juce::jlimit(-1.0f, 1.0f, offsetValue);
        phase = phaseValue - std::floor(phaseValue);
        enabled = enabledValue; used = usedValue;
        rebuildPath(); repaint();
    }
    void setPhase(float value)
    {
        if (std::isfinite(value))
        { phase = value - std::floor(value); repaint(); }
    }
    void paint(juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced(2.0f);
        g.setColour(hypercolor::ui::palette::background());
        g.fillRoundedRectangle(r, 4.0f);
        g.setColour(hypercolor::ui::palette::border());
        g.drawRoundedRectangle(r, 4.0f, 1.0f);
        const auto usableHeight = r.getHeight() * 0.36f;
        const auto centreY = r.getCentreY() - offset * usableHeight;
        const auto depthHeight = depth * usableHeight;
        const auto waveformTransform = juce::AffineTransform()
            .scaled(r.getWidth(), -usableHeight).translated(r.getX(), r.getCentreY());
        g.setColour(hypercolor::ui::palette::space().withAlpha(enabled ? 0.12f : 0.035f));
        g.fillRect(r.getX(), centreY - depthHeight, r.getWidth(), 2.0f * depthHeight);
        g.setColour(hypercolor::ui::palette::mutedText().withAlpha(0.45f));
        g.drawHorizontalLine(juce::roundToInt(r.getCentreY()), r.getX() + 3.0f, r.getRight() - 3.0f);
        g.setColour(hypercolor::ui::palette::border().brighter(0.15f));
        g.strokePath(path, juce::PathStrokeType(1.8f), waveformTransform);
        auto waveColour = enabled ? hypercolor::ui::palette::space()
                                  : hypercolor::ui::palette::mutedText().withAlpha(0.45f);
        g.setColour(waveColour.withAlpha(enabled ? 0.95f : 0.4f));
        g.strokePath(path, juce::PathStrokeType(1.8f), waveformTransform);
        if (used && enabled)
        {
            g.setColour(hypercolor::ui::palette::text());
            g.fillEllipse(r.getRight() - 8.0f, r.getY() + 3.0f, 4.0f, 4.0f);
        }
        if (enabled)
        {
            const auto x = r.getX() + phase * r.getWidth();
            g.setColour(hypercolor::ui::palette::text().withAlpha(0.2f));
            g.drawLine(x, r.getY() + 3.0f, x, r.getBottom() - 3.0f, 4.0f);
            g.setColour(hypercolor::ui::palette::text());
            g.drawLine(x, r.getY() + 3.0f, x, r.getBottom() - 3.0f, 1.2f);
        }
    }
private:
    void rebuildPath()
    {
        path.clear();
        constexpr std::array held { -0.72f, 0.24f, 0.68f, -0.18f,
                                     0.82f, -0.45f, 0.12f, -0.62f };
        for (int i = 0; i <= 96; ++i)
        {
            const auto x = static_cast<float>(i) / 96.0f;
            auto y = 0.0f;
            if (waveform == hypercolor::dsp::modulation::LfoWaveform::sampleAndHold)
                y = held[juce::jmin(held.size() - 1,
                    static_cast<std::size_t>(x * static_cast<float>(held.size())))];
            else
                y = hypercolor::dsp::modulation::Lfo::evaluate(waveform, x, 0.0f,
                    waveform == hypercolor::dsp::modulation::LfoWaveform::custom
                        ? &customWaveform : nullptr);
            const auto point = juce::Point<float>(x, offset - y * depth);
            if (i == 0) path.startNewSubPath(point); else path.lineTo(point);
        }
    }
    hypercolor::dsp::modulation::LfoWaveform waveform
        = hypercolor::dsp::modulation::LfoWaveform::sine;
    Waveform customWaveform = hypercolor::dsp::modulation::Lfo::makeDefaultCustomWaveform();
    juce::Path path;
    float depth = 1.0f, offset = 0.0f, phase = 0.0f;
    bool enabled = true, used = false;
};

class CustomLfoWaveEditor final : public juce::Component
{
public:
    using Waveform = hypercolor::dsp::modulation::Lfo::CustomWaveform;
    std::function<void(const Waveform&)> onWaveformChanged;
    std::function<void()> onDrawStarted;
    void setWaveform(const Waveform& raw, const Waveform& display)
    { waveform = raw; displayWaveform = display; rebuildPath(); repaint(); }
    [[nodiscard]] const Waveform& getWaveform() const noexcept { return waveform; }

    void paint(juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced(1.0f);
        g.setColour(hypercolor::ui::palette::background()); g.fillRoundedRectangle(r, 5.0f);
        g.setColour(hypercolor::ui::palette::border()); g.drawRoundedRectangle(r, 5.0f, 1.0f);
        g.saveState(); g.reduceClipRegion(getLocalBounds().reduced(4));
        const auto inner = r.reduced(8.0f);
        g.setColour(hypercolor::ui::palette::border().withAlpha(0.35f));
        for (int i = 1; i < 8; ++i)
        {
            const auto x = inner.getX() + inner.getWidth() * static_cast<float>(i) / 8.0f;
            g.drawVerticalLine(juce::roundToInt(x), inner.getY(), inner.getBottom());
        }
        for (int i = 1; i < 4; ++i)
        {
            const auto y = inner.getY() + inner.getHeight() * static_cast<float>(i) / 4.0f;
            g.drawHorizontalLine(juce::roundToInt(y), inner.getX(), inner.getRight());
        }
        g.setColour(hypercolor::ui::palette::mutedText().withAlpha(0.8f));
        g.drawHorizontalLine(juce::roundToInt(inner.getCentreY()), inner.getX(), inner.getRight());
        juce::AffineTransform transform;
        transform = transform.scaled(inner.getWidth(), -inner.getHeight() * 0.5f)
                             .translated(inner.getX(), inner.getCentreY());
        g.setColour(hypercolor::ui::palette::space().withAlpha(0.14f));
        g.fillPath(fillPath, transform);
        g.setColour(hypercolor::ui::palette::space());
        g.strokePath(path, juce::PathStrokeType(2.0f), transform);
        g.restoreState();
        if (drawing)
        {
            g.setColour(hypercolor::ui::palette::text());
            g.drawRoundedRectangle(r.reduced(1.5f), 5.0f, 1.4f);
        }
    }

    void mouseDown(const juce::MouseEvent& event) override
    { drawing = event.mods.isLeftButtonDown(); previousIndex = -1;
      if (drawing) { if (onDrawStarted) onDrawStarted(); writeAt(event.position); } }
    void mouseDrag(const juce::MouseEvent& event) override
    { if (drawing) writeAt(event.position); }
    void mouseUp(const juce::MouseEvent&) override
    { drawing = false; previousIndex = -1; repaint(); }

private:
    void rebuildPath()
    {
        path.clear();
        for (std::size_t i = 0; i < displayWaveform.size(); ++i)
        {
            const auto x = static_cast<float>(i) / static_cast<float>(displayWaveform.size() - 1);
            const auto point = juce::Point<float>(x, displayWaveform[i]);
            if (i == 0) path.startNewSubPath(point); else path.lineTo(point);
        }
        fillPath = path;
        fillPath.lineTo(1.0f, 0.0f); fillPath.lineTo(0.0f, 0.0f);
        fillPath.closeSubPath();
    }
    void writeAt(juce::Point<float> point)
    {
        auto inner = getLocalBounds().toFloat().reduced(9.0f);
        if (inner.isEmpty()) return;
        const auto x = juce::jlimit(0.0f, 1.0f, (point.x - inner.getX()) / inner.getWidth());
        const auto y = juce::jlimit(-1.0f, 1.0f,
            1.0f - 2.0f * (point.y - inner.getY()) / inner.getHeight());
        if (previousIndex >= 0)
            hypercolor::dsp::modulation::Lfo::drawCustomSegment(waveform, previousX, previousY, x, y);
        else
            hypercolor::dsp::modulation::Lfo::drawCustomSegment(waveform, x, y, x, y);
        previousX = x; previousY = y;
        previousIndex = juce::roundToInt(x * static_cast<float>(waveform.size() - 2));
        rebuildPath(); repaint();
        if (onWaveformChanged) onWaveformChanged(waveform);
    }
    Waveform waveform = hypercolor::dsp::modulation::Lfo::makeDefaultCustomWaveform();
    Waveform displayWaveform = waveform;
    juce::Path path;
    juce::Path fillPath;
    int previousIndex = -1;
    float previousX = 0.0f, previousY = 0.0f;
    bool drawing = false;
};

class LfoModuleCard final : public juce::Component
{
public:
    using Waveform = hypercolor::dsp::modulation::Lfo::CustomWaveform;
    std::function<void()> onClick;
    void setState(int moduleNumber, hypercolor::dsp::modulation::LfoWaveform shape,
                  const Waveform& table, bool isEnabled, bool isSelected, float currentPhase)
    {
        const bool rebuild = number != moduleNumber || waveform != shape
            || customWaveform != table || enabled != isEnabled;
        number = moduleNumber; waveform = shape; customWaveform = table;
        enabled = isEnabled; selected = isSelected;
        if (std::isfinite(currentPhase)) phase = currentPhase - std::floor(currentPhase);
        if (rebuild) rebuildPath();
        repaint();
    }
    void setSelected(bool value) { if (selected != value) { selected = value; repaint(); } }
    void setPhase(float value)
    { if (std::isfinite(value)) { phase = value - std::floor(value); repaint(); } }
    void mouseDown(const juce::MouseEvent& event) override
    { mousePressed = event.mods.isLeftButtonDown(); }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced(1.0f);
        auto fill = selected ? hypercolor::ui::palette::space().withAlpha(0.22f)
                             : hypercolor::ui::palette::panelRaised();
        if (!enabled) fill = fill.withMultipliedAlpha(0.62f);
        g.setColour(fill); g.fillRoundedRectangle(bounds, 5.0f);
        g.setColour(selected ? hypercolor::ui::palette::space()
                             : hypercolor::ui::palette::border());
        g.drawRoundedRectangle(bounds, 5.0f, selected ? 1.4f : 1.0f);
        g.setColour(enabled ? hypercolor::ui::palette::text()
                            : hypercolor::ui::palette::mutedText());
        g.setFont(11.0f);
        g.drawText("L" + juce::String(number) + (enabled ? " ON" : " OFF"),
                   getLocalBounds().reduced(6, 2).removeFromTop(16),
                   juce::Justification::centredLeft, false);
        auto graph = bounds.reduced(7.0f);
        graph.removeFromTop(15.0f);
        if (graph.getHeight() < 5.0f) return;
        const auto transform = juce::AffineTransform().scaled(graph.getWidth(), -graph.getHeight() * 0.42f)
            .translated(graph.getX(), graph.getCentreY());
        g.setColour(hypercolor::ui::palette::border().withAlpha(0.65f));
        g.drawHorizontalLine(juce::roundToInt(graph.getCentreY()), graph.getX(), graph.getRight());
        g.setColour(enabled ? hypercolor::ui::palette::space()
                            : hypercolor::ui::palette::mutedText().withAlpha(0.45f));
        g.strokePath(path, juce::PathStrokeType(1.35f), transform);
        if (enabled)
        {
            const auto x = graph.getX() + phase * graph.getWidth();
            g.setColour(hypercolor::ui::palette::text().withAlpha(0.26f));
            g.drawLine(x, graph.getY(), x, graph.getBottom(), 2.8f);
            g.setColour(hypercolor::ui::palette::text());
            g.drawLine(x, graph.getY(), x, graph.getBottom(), 0.9f);
        }
    }
    void mouseUp(const juce::MouseEvent& event) override
    { if (mousePressed && getLocalBounds().contains(event.getPosition()) && onClick) onClick();
      mousePressed = false; }

private:
    void rebuildPath()
    {
        path.clear();
        constexpr std::array<float, 8> held { -0.72f, 0.24f, 0.68f, -0.18f,
                                               0.82f, -0.45f, 0.12f, -0.62f };
        for (int i = 0; i <= 64; ++i)
        {
            const auto p = static_cast<float>(i) / 64.0f;
            const auto value = waveform == hypercolor::dsp::modulation::LfoWaveform::sampleAndHold
                ? held[juce::jmin(held.size() - 1, static_cast<std::size_t>(p * held.size()))]
                : hypercolor::dsp::modulation::Lfo::evaluate(waveform, p, 0.0f,
                    waveform == hypercolor::dsp::modulation::LfoWaveform::custom
                        ? &customWaveform : nullptr);
            const juce::Point<float> point { p, value };
            if (i == 0) path.startNewSubPath(point); else path.lineTo(point);
        }
    }
    int number = 0;
    hypercolor::dsp::modulation::LfoWaveform waveform = hypercolor::dsp::modulation::LfoWaveform::sine;
    Waveform customWaveform = hypercolor::dsp::modulation::Lfo::makeDefaultCustomWaveform();
    juce::Path path;
    float phase = 0.0f;
    bool enabled = false, selected = false, mousePressed = false;
};

class LfoWorkspacePanel final : public juce::Component, private juce::Timer
{
public:
    LfoWorkspacePanel(HypercolorAudioProcessor& processor,
                      std::function<void(std::uint64_t)> onFocus)
        : owner(processor), focusCallback(std::move(onFocus))
    {
        setSize(560, 720);
        title.setText("LFO MODULATION", juce::dontSendNotification);
        title.setColour(juce::Label::textColourId, hypercolor::ui::palette::text());
        addAndMakeVisible(title);
        auto styleCombo = [](juce::ComboBox& c)
        {
            c.setColour(juce::ComboBox::backgroundColourId, hypercolor::ui::palette::panelRaised());
            c.setColour(juce::ComboBox::textColourId, hypercolor::ui::palette::text());
            c.setColour(juce::ComboBox::outlineColourId, hypercolor::ui::palette::border());
            c.setColour(juce::ComboBox::arrowColourId, hypercolor::ui::palette::space());
        };
        for (auto* c : { &waveform, &rateMode, &division, &fxInstance, &parameter, &mapping })
        { styleCombo(*c); addAndMakeVisible(*c); }
        module.setTextWhenNothingSelected("Select LFO");
        module.setVisible(false);
        for (std::size_t i = 0; i < moduleCards.size(); ++i)
        {
            auto& card = moduleCards[i];
            card.onClick = [this, i]
            {
                selectedModule = static_cast<int>(i);
                module.setSelectedId(static_cast<int>(i + 1), juce::dontSendNotification);
                loadModule();
            };
            addAndMakeVisible(card);
        }
        waveform.addItemList({ "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Random / S&H", "Custom" }, 1);
        rateMode.addItemList({ "FREE RATE", "BPM SYNC" }, 1);
        for (std::size_t i = 0; i < hypercolor::plugin::lfoDivisionLabels.size(); ++i)
            division.addItem(hypercolor::plugin::lfoDivisionLabels[i], static_cast<int>(i + 1));
        for (auto* s : { &rate, &depth, &phase, &offset, &smooth, &amount })
        {
            s->setSliderStyle(juce::Slider::LinearHorizontal);
            s->setTextBoxStyle(juce::Slider::TextBoxRight, false, 58, 18);
            s->setColour(juce::Slider::trackColourId, hypercolor::ui::palette::space());
            s->setColour(juce::Slider::thumbColourId, hypercolor::ui::palette::text());
            s->setColour(juce::Slider::textBoxTextColourId, hypercolor::ui::palette::text());
            s->setColour(juce::Slider::textBoxBackgroundColourId, hypercolor::ui::palette::background());
            addAndMakeVisible(*s);
        }
        rate.setRange(0.0, 40.0, 0.01); rate.setTextValueSuffix(" Hz");
        depth.setRange(0.0, 1.0, 0.001); phase.setRange(0.0, 1.0, 0.001);
        offset.setRange(-1.0, 1.0, 0.001); smooth.setRange(0.0, 1.0, 0.01);
        smooth.textFromValueFunction = [](double value)
            { return juce::String(juce::roundToInt(value * 100.0)) + "%"; };
        amount.setRange(0.0, 1.0, 0.001);
        amount.setValue(0.5, juce::dontSendNotification);
        for (auto* b : { &enabled, &bipolar, &inverted, &addModule, &removeModule, &addMapping, &removeMapping })
        { addAndMakeVisible(*b); b->setColour(juce::TextButton::buttonColourId, hypercolor::ui::palette::panelRaised());
          b->setColour(juce::TextButton::textColourOffId, hypercolor::ui::palette::space()); }
        enabled.setButtonText("ON"); enabled.setClickingTogglesState(true);
        bipolar.setButtonText("BIPOLAR"); bipolar.setClickingTogglesState(true);
        inverted.setButtonText("INVERT"); inverted.setClickingTogglesState(true);
        addModule.setButtonText("+ ADD LFO"); removeModule.setButtonText("REMOVE LFO");
        addMapping.setButtonText("ADD ASSIGNMENT"); removeMapping.setButtonText("REMOVE");
        undoWave.setButtonText("UNDO"); clearWave.setButtonText("CLEAR"); resetWave.setButtonText("RESET");
        addAndMakeVisible(preview);
        addAndMakeVisible(customEditor);
        for (auto* button : { &undoWave, &clearWave, &resetWave })
        { addAndMakeVisible(*button); button->setColour(juce::TextButton::buttonColourId, hypercolor::ui::palette::panelRaised());
          button->setColour(juce::TextButton::textColourOffId, hypercolor::ui::palette::space()); }
        addAndMakeVisible(title2);
        addAndMakeVisible(rateLabel); addAndMakeVisible(depthLabel); addAndMakeVisible(phaseLabel);
        addAndMakeVisible(offsetLabel); addAndMakeVisible(amountLabel); addAndMakeVisible(help);
        addAndMakeVisible(smoothLabel);
        title2.setColour(juce::Label::textColourId, hypercolor::ui::palette::space());
        for (auto* label : { &rateLabel, &depthLabel, &phaseLabel, &offsetLabel, &smoothLabel, &amountLabel })
            label->setColour(juce::Label::textColourId, hypercolor::ui::palette::mutedText());
        rateLabel.setText("RATE", juce::dontSendNotification); depthLabel.setText("DEPTH", juce::dontSendNotification);
        phaseLabel.setText("PHASE", juce::dontSendNotification); offsetLabel.setText("OFFSET", juce::dontSendNotification);
        amountLabel.setText("AMOUNT", juce::dontSendNotification);
        smoothLabel.setText("SMOOTH", juce::dontSendNotification);
        help.setText("Continuous controls only. LFO sums after XY Macro; base FX values remain stored.",
                     juce::dontSendNotification);
        help.setColour(juce::Label::textColourId, hypercolor::ui::palette::mutedText());
        module.onChange = [this] { loadModule(); };
        waveform.onChange = [this] { saveModule(); };
        rateMode.onChange = [this] { division.setEnabled(rateMode.getSelectedId() == 2); rate.setEnabled(rateMode.getSelectedId() == 1); saveModule(); };
        division.onChange = [this] { saveModule(); };
        enabled.onClick = [this] { enabled.setButtonText(enabled.getToggleState() ? "ON" : "OFF"); saveModule(); };
        rate.onValueChange = [this] { saveModule(); };
        depth.onValueChange = [this] { saveModule(); };
        phase.onValueChange = [this] { saveModule(); };
        offset.onValueChange = [this] { saveModule(); };
        smooth.onValueChange = [this] { saveModule(); };
        customEditor.onDrawStarted = [this]
        {
            if (selectedModule < 0) return;
            configuration = owner.getLfoConfiguration();
            if (static_cast<std::size_t>(selectedModule) < configuration.moduleCount)
            { undoWaveform = configuration.modules[static_cast<std::size_t>(selectedModule)].customWaveform;
              undoLfoIdentity = configuration.modules[static_cast<std::size_t>(selectedModule)].stableIdentity;
              hasUndoWaveform = true; undoWave.setEnabled(true); }
        };
        customEditor.onWaveformChanged = [this](const auto& value) { acceptDrawnWaveform(value); };
        undoWave.onClick = [this]
        {
            if (selectedModule >= 0 && static_cast<std::size_t>(selectedModule) < configuration.moduleCount
                && configuration.modules[static_cast<std::size_t>(selectedModule)].stableIdentity == undoLfoIdentity)
                applyCustomWaveform(undoWaveform);
            hasUndoWaveform = false; undoLfoIdentity = 0; undoWave.setEnabled(false);
        };
        clearWave.onClick = [this]
        {
            captureUndo();
            hypercolor::dsp::modulation::Lfo::CustomWaveform flat {};
            hypercolor::dsp::modulation::Lfo::clearCustomWaveform(flat);
            applyCustomWaveform(flat);
        };
        resetWave.onClick = [this]
        {
            captureUndo();
            applyCustomWaveform(hypercolor::dsp::modulation::Lfo::makeDefaultCustomWaveform());
        };
        fxInstance.onChange = [this] { rebuildParameters(); };
        mapping.onChange = [this] { loadMapping(); };
        amount.onValueChange = [this] { saveMapping(); };
        bipolar.onClick = [this] { saveMapping(); };
        inverted.onClick = [this] { saveMapping(); };
        addModule.onClick = [this] { createModule(); };
        removeModule.onClick = [this] { deleteModule(); };
        addMapping.onClick = [this] { createMapping(); };
        removeMapping.onClick = [this] { deleteMapping(); };
        refresh();
        startTimerHz(30);
    }

    ~LfoWorkspacePanel() override { stopTimer(); }

    void paint(juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour(hypercolor::ui::palette::panel().darker(0.08f)); g.fillRoundedRectangle(r, 9.0f);
        g.setColour(hypercolor::ui::palette::border()); g.drawRoundedRectangle(r.reduced(0.5f), 9.0f, 1.0f);
        g.setColour(hypercolor::ui::palette::space()); g.fillRect(0.0f, 0.0f, 70.0f, 2.0f);
    }
    void resized() override
    {
        auto a = getLocalBounds().reduced(14, 12);
        title.setBounds(a.removeFromTop(24)); a.removeFromTop(6);
        auto row = a.removeFromTop(28); addModule.setBounds(row.removeFromLeft(120));
        row.removeFromLeft(8); removeModule.setBounds(row);
        a.removeFromTop(4); row = a.removeFromTop(50);
        const auto visibleCards = juce::jmax(1, module.getNumItems());
        const auto cardWidth = juce::jmax(48, row.getWidth() / visibleCards);
        for (std::size_t i = 0; i < moduleCards.size(); ++i)
            if (moduleCards[i].isVisible())
                moduleCards[i].setBounds(row.removeFromLeft(cardWidth).reduced(2, 0));
        a.removeFromTop(8); row = a.removeFromTop(28); enabled.setBounds(row.removeFromLeft(56));
        row.removeFromLeft(8); waveform.setBounds(row.removeFromLeft(140)); row.removeFromLeft(8);
        rateMode.setBounds(row.removeFromLeft(116)); row.removeFromLeft(8); division.setBounds(row);
        a.removeFromTop(7); preview.setBounds(a.removeFromTop(46));
        if (customEditor.isVisible())
        {
            a.removeFromTop(5); customEditor.setBounds(a.removeFromTop(86));
            row = a.removeFromTop(24); undoWave.setBounds(row.removeFromLeft(78)); row.removeFromLeft(6);
            clearWave.setBounds(row.removeFromLeft(78)); row.removeFromLeft(6); resetWave.setBounds(row.removeFromLeft(78));
            a.removeFromTop(3);
        }
        auto sliderRow = [&](juce::Label& l, juce::Slider& s)
        { auto r = a.removeFromTop(25); l.setBounds(r.removeFromLeft(62)); s.setBounds(r); a.removeFromTop(2); };
        sliderRow(rateLabel, rate); sliderRow(depthLabel, depth); sliderRow(phaseLabel, phase); sliderRow(offsetLabel, offset);
        if (smooth.isVisible()) sliderRow(smoothLabel, smooth);
        a.removeFromTop(2); row = a.removeFromTop(24); row.removeFromLeft(0); row.removeFromRight(0);
        row.removeFromLeft(0); bipolar.setBounds(row.removeFromLeft(100)); row.removeFromLeft(6); inverted.setBounds(row.removeFromLeft(100));
        a.removeFromTop(9); title2.setBounds(a.removeFromTop(20));
        row = a.removeFromTop(28); mapping.setBounds(row); a.removeFromTop(6);
        row = a.removeFromTop(28); fxInstance.setBounds(row.removeFromLeft(row.getWidth() / 2 - 4)); row.removeFromLeft(8); parameter.setBounds(row);
        a.removeFromTop(5); row = a.removeFromTop(25); amountLabel.setBounds(row.removeFromLeft(62)); amount.setBounds(row);
        a.removeFromTop(5); row = a.removeFromTop(28); addMapping.setBounds(row.removeFromLeft(row.getWidth() / 2 - 4));
        row.removeFromLeft(8); removeMapping.setBounds(row); a.removeFromTop(5); help.setBounds(a.removeFromTop(28));
    }

private:
    void timerCallback() override
    {
        const auto revision = owner.getLfoConfigurationRevision();
        if ((revision & 1u) == 0u && revision != observedRevision)
            refresh();
        else if (selectedModule >= 0)
        {
            preview.setPhase(owner.getLfoPhaseForDisplay(static_cast<std::size_t>(selectedModule)));
            for (std::size_t i = 0; i < moduleCards.size(); ++i)
                if (moduleCards[i].isVisible())
                    moduleCards[i].setPhase(owner.getLfoPhaseForDisplay(i));
        }
    }
    void refresh()
    {
        refreshing = true;
        configuration = owner.getLfoConfiguration();
        observedRevision = owner.getLfoConfigurationRevision();
        addModule.setEnabled(configuration.moduleCount < hypercolor::plugin::maximumLfoModules);
        removeModule.setEnabled(configuration.moduleCount > 0);
        module.clear(juce::dontSendNotification);
        for (std::size_t i = 0; i < configuration.moduleCount; ++i)
            module.addItem("LFO " + juce::String(static_cast<int>(i + 1)), static_cast<int>(i + 1));
        for (std::size_t i = 0; i < moduleCards.size(); ++i)
        {
            const bool active = i < configuration.moduleCount;
            moduleCards[i].setVisible(active);
            if (active)
            {
                hypercolor::dsp::modulation::Lfo::CustomWaveform effective {};
                hypercolor::dsp::modulation::Lfo::prepareCustomWaveform(
                    configuration.modules[i].customWaveform, configuration.modules[i].smooth, effective);
                moduleCards[i].setState(static_cast<int>(i + 1), configuration.modules[i].waveform,
                    effective, configuration.modules[i].enabled, static_cast<int>(i) == selectedModule,
                    owner.getLfoPhaseForDisplay(i));
            }
        }
        if (configuration.moduleCount > 0)
            module.setSelectedId(juce::jlimit(1, static_cast<int>(configuration.moduleCount), selectedModule + 1), juce::dontSendNotification);
        else
            selectedModule = -1;
        refreshInstances();
        refreshing = false;
        loadModule();
        resized();
    }
    void refreshInstances()
    {
        instances.clear(); fxInstance.clear(juce::dontSendNotification);
        const auto chain = owner.getFxChainConfiguration();
        for (std::size_t i = 0; i < chain.count; ++i)
        {
            instances.push_back(chain.order[i]);
            const auto id = chain.order[i];
            fxInstance.addItem(juce::String(effectNames[hypercolor::plugin::toIndex(id.type)])
                + " #" + juce::String(static_cast<int>(id.slot + 1)), static_cast<int>(instances.size()));
        }
        if (!instances.empty()) fxInstance.setSelectedId(1, juce::dontSendNotification);
        rebuildParameters();
    }
    void rebuildParameters()
    {
        parameter.clear(juce::dontSendNotification); parameterIDs.clear();
        const auto selected = fxInstance.getSelectedId() - 1;
        if (selected >= 0 && static_cast<std::size_t>(selected) < instances.size())
        {
            const auto id = instances[static_cast<std::size_t>(selected)];
            const auto type = hypercolor::plugin::toIndex(id.type);
            for (std::size_t i = 0; i < effectParameterCounts[type]; ++i)
                if (effectParameters[type][i] != nullptr)
                {
                    const auto pid = hypercolor::plugin::fxParameterID(id.type, id.slot, effectParameters[type][i]);
                    auto* target = owner.getValueTreeState().getParameter(pid);
                    if (target != nullptr
                        && dynamic_cast<juce::AudioParameterChoice*>(target) == nullptr
                        && dynamic_cast<juce::AudioParameterBool*>(target) == nullptr)
                    { parameterIDs.push_back(pid); parameter.addItem(target->getName(32), static_cast<int>(parameterIDs.size())); }
                }
        }
        if (parameter.getNumItems() > 0) parameter.setSelectedId(1, juce::dontSendNotification);
    }
    void loadModule()
    {
        if (refreshing) return;
        selectedModule = module.getSelectedId() - 1;
        for (std::size_t i = 0; i < moduleCards.size(); ++i)
            moduleCards[i].setSelected(static_cast<int>(i) == selectedModule);
        configuration = owner.getLfoConfiguration();
        if (selectedModule < 0 || static_cast<std::size_t>(selectedModule) >= configuration.moduleCount)
        { title.setText("LFO MODULATION", juce::dontSendNotification);
          customEditor.setVisible(false); smoothLabel.setVisible(false); smooth.setVisible(false);
          undoWave.setVisible(false); clearWave.setVisible(false); resetWave.setVisible(false);
          setControlsEnabled(false); rebuildMappings(); return; }
        const auto& m = configuration.modules[static_cast<std::size_t>(selectedModule)];
        title.setText("LFO MODULATION  /  LFO " + juce::String(selectedModule + 1),
                      juce::dontSendNotification);
        if (focusCallback) focusCallback(m.stableIdentity);
        waveform.setSelectedId(static_cast<int>(m.waveform) + 1, juce::dontSendNotification);
        rateMode.setSelectedId(static_cast<int>(m.rateMode) + 1, juce::dontSendNotification);
        division.setSelectedId(static_cast<int>(m.division) + 1, juce::dontSendNotification);
        enabled.setToggleState(m.enabled, juce::dontSendNotification);
        enabled.setButtonText(m.enabled ? "ON" : "OFF");
        rate.setValue(m.rateHz, juce::dontSendNotification); depth.setValue(m.depth, juce::dontSendNotification);
        phase.setValue(m.phase, juce::dontSendNotification); offset.setValue(m.offset, juce::dontSendNotification);
        smooth.setValue(m.smooth, juce::dontSendNotification);
        const auto isUsed = hasAssignments(m.stableIdentity);
        hypercolor::dsp::modulation::Lfo::CustomWaveform effective {};
        hypercolor::dsp::modulation::Lfo::prepareCustomWaveform(m.customWaveform, m.smooth, effective);
        customEditor.setWaveform(m.customWaveform, effective);
        const auto customSelected = m.waveform == hypercolor::dsp::modulation::LfoWaveform::custom;
        help.setText(customSelected
            ? "Hold and drag to draw. Custom waveform is saved with this LFO."
            : "Continuous controls only. LFO sums after XY Macro; base FX values remain stored.",
            juce::dontSendNotification);
        customEditor.setVisible(customSelected);
        smoothLabel.setVisible(customSelected); smooth.setVisible(customSelected);
        undoWave.setVisible(customSelected); clearWave.setVisible(customSelected); resetWave.setVisible(customSelected);
        preview.setState(m.waveform, effective, m.depth, m.offset,
            owner.getLfoPhaseForDisplay(static_cast<std::size_t>(selectedModule)), m.enabled, isUsed);
        undoWave.setEnabled(hasUndoWaveform && undoLfoIdentity == m.stableIdentity);
        setControlsEnabled(true); rate.setEnabled(m.rateMode == hypercolor::plugin::LfoRateMode::free);
        division.setEnabled(m.rateMode == hypercolor::plugin::LfoRateMode::bpmSync);
        rebuildMappings();
    }
    bool hasAssignments(std::uint64_t identity) const
    {
        for (std::size_t i = 0; i < configuration.assignmentCount; ++i)
            if (configuration.assignments[i].lfoIdentity == identity)
                return true;
        return false;
    }
    void captureUndo()
    {
        if (selectedModule < 0) return;
        configuration = owner.getLfoConfiguration();
        if (static_cast<std::size_t>(selectedModule) >= configuration.moduleCount) return;
        undoWaveform = configuration.modules[static_cast<std::size_t>(selectedModule)].customWaveform;
        undoLfoIdentity = configuration.modules[static_cast<std::size_t>(selectedModule)].stableIdentity;
        hasUndoWaveform = true; undoWave.setEnabled(true);
    }
    void acceptDrawnWaveform(const hypercolor::dsp::modulation::Lfo::CustomWaveform& value)
    {
        if (refreshing || selectedModule < 0) return;
        configuration = owner.getLfoConfiguration();
        if (static_cast<std::size_t>(selectedModule) >= configuration.moduleCount) return;
        auto& target = configuration.modules[static_cast<std::size_t>(selectedModule)].customWaveform;
        for (std::size_t i = 0; i < target.size(); ++i)
            target[i] = std::isfinite(value[i]) ? juce::jlimit(-1.0f, 1.0f, value[i]) : 0.0f;
        target.back() = target.front();
        if (owner.setLfoConfiguration(configuration))
            observedRevision = owner.getLfoConfigurationRevision();
        auto& moduleConfig = configuration.modules[static_cast<std::size_t>(selectedModule)];
        hypercolor::dsp::modulation::Lfo::CustomWaveform effective {};
        hypercolor::dsp::modulation::Lfo::prepareCustomWaveform(moduleConfig.customWaveform,
                                                      moduleConfig.smooth, effective);
        customEditor.setWaveform(moduleConfig.customWaveform, effective);
        preview.setState(moduleConfig.waveform, effective, moduleConfig.depth, moduleConfig.offset,
            owner.getLfoPhaseForDisplay(static_cast<std::size_t>(selectedModule)),
            moduleConfig.enabled, hasAssignments(moduleConfig.stableIdentity));
        moduleCards[static_cast<std::size_t>(selectedModule)].setState(selectedModule + 1,
            moduleConfig.waveform, effective, moduleConfig.enabled, true,
            owner.getLfoPhaseForDisplay(static_cast<std::size_t>(selectedModule)));
    }
    void applyCustomWaveform(const hypercolor::dsp::modulation::Lfo::CustomWaveform& value)
    {
        if (selectedModule < 0) return;
        configuration = owner.getLfoConfiguration();
        if (static_cast<std::size_t>(selectedModule) >= configuration.moduleCount) return;
        auto& target = configuration.modules[static_cast<std::size_t>(selectedModule)].customWaveform;
        target = value; target.back() = target.front();
        if (owner.setLfoConfiguration(configuration))
            observedRevision = owner.getLfoConfigurationRevision();
        auto& moduleConfig = configuration.modules[static_cast<std::size_t>(selectedModule)];
        hypercolor::dsp::modulation::Lfo::CustomWaveform effective {};
        hypercolor::dsp::modulation::Lfo::prepareCustomWaveform(moduleConfig.customWaveform,
                                                      moduleConfig.smooth, effective);
        customEditor.setWaveform(moduleConfig.customWaveform, effective);
        preview.setState(moduleConfig.waveform, effective, moduleConfig.depth, moduleConfig.offset,
            owner.getLfoPhaseForDisplay(static_cast<std::size_t>(selectedModule)),
            moduleConfig.enabled, hasAssignments(moduleConfig.stableIdentity));
        moduleCards[static_cast<std::size_t>(selectedModule)].setState(selectedModule + 1,
            moduleConfig.waveform, effective, moduleConfig.enabled, true,
            owner.getLfoPhaseForDisplay(static_cast<std::size_t>(selectedModule)));
    }
    void setControlsEnabled(bool value)
    { enabled.setEnabled(value); waveform.setEnabled(value); rateMode.setEnabled(value); rate.setEnabled(value); depth.setEnabled(value); phase.setEnabled(value); offset.setEnabled(value); smooth.setEnabled(value); bipolar.setEnabled(value); inverted.setEnabled(value); addMapping.setEnabled(value); }
    void saveModule()
    {
        if (refreshing || selectedModule < 0) return;
        configuration = owner.getLfoConfiguration();
        if (static_cast<std::size_t>(selectedModule) >= configuration.moduleCount) return;
        auto& m = configuration.modules[static_cast<std::size_t>(selectedModule)];
        m.enabled = enabled.getToggleState();
        m.waveform = static_cast<hypercolor::dsp::modulation::LfoWaveform>(juce::jmax(0, waveform.getSelectedId() - 1));
        m.rateMode = static_cast<hypercolor::plugin::LfoRateMode>(juce::jmax(0, rateMode.getSelectedId() - 1));
        m.division = static_cast<hypercolor::plugin::LfoDivision>(juce::jmax(0, division.getSelectedId() - 1));
        m.rateHz = static_cast<float>(rate.getValue()); m.depth = static_cast<float>(depth.getValue());
        m.phase = static_cast<float>(phase.getValue()); m.offset = static_cast<float>(offset.getValue());
        m.smooth = static_cast<float>(smooth.getValue());
        hypercolor::dsp::modulation::Lfo::CustomWaveform cardWave {};
        hypercolor::dsp::modulation::Lfo::prepareCustomWaveform(m.customWaveform, m.smooth, cardWave);
        moduleCards[static_cast<std::size_t>(selectedModule)].setState(selectedModule + 1,
            m.waveform, cardWave, m.enabled, true,
            owner.getLfoPhaseForDisplay(static_cast<std::size_t>(selectedModule)));
        if (owner.setLfoConfiguration(configuration))
            observedRevision = owner.getLfoConfigurationRevision();
        hypercolor::dsp::modulation::Lfo::CustomWaveform effective {};
        hypercolor::dsp::modulation::Lfo::prepareCustomWaveform(m.customWaveform, m.smooth, effective);
        customEditor.setWaveform(m.customWaveform, effective);
        const auto customSelected = m.waveform == hypercolor::dsp::modulation::LfoWaveform::custom;
        help.setText(customSelected
            ? "Hold and drag to draw. Custom waveform is saved with this LFO."
            : "Continuous controls only. LFO sums after XY Macro; base FX values remain stored.",
            juce::dontSendNotification);
        customEditor.setVisible(customSelected); smoothLabel.setVisible(customSelected); smooth.setVisible(customSelected);
        undoWave.setVisible(customSelected); clearWave.setVisible(customSelected); resetWave.setVisible(customSelected);
        preview.setState(m.waveform, effective, m.depth, m.offset,
            owner.getLfoPhaseForDisplay(static_cast<std::size_t>(selectedModule)), m.enabled,
            hasAssignments(m.stableIdentity));
        resized();
    }
    void rebuildMappings()
    {
        mapping.clear(juce::dontSendNotification);
        if (selectedModule < 0 || static_cast<std::size_t>(selectedModule) >= configuration.moduleCount) return;
        int item = 0;
        for (std::size_t i = 0; i < configuration.assignmentCount; ++i)
        {
            const auto& a = configuration.assignments[i];
            if (a.lfoIdentity != configuration.modules[static_cast<std::size_t>(selectedModule)].stableIdentity) continue;
            auto targetName = a.parameterID;
            for (const auto& fx : instances)
                if (fx.stableIdentity == a.fxInstanceIdentity)
                {
                    targetName = juce::String(effectNames[hypercolor::plugin::toIndex(fx.type)])
                        + " #" + juce::String(static_cast<int>(fx.slot + 1))
                        + "  /  " + owner.getValueTreeState().getParameter(a.parameterID)->getName(30);
                    break;
                }
            mapping.addItem(targetName + "  × " + juce::String(a.amount, 2), ++item);
        }
        if (item > 0) mapping.setSelectedId(1, juce::dontSendNotification);
        loadMapping();
    }
    void loadMapping()
    {
        if (refreshing) return;
        amount.setValue(0.0, juce::dontSendNotification); bipolar.setToggleState(true, juce::dontSendNotification); inverted.setToggleState(false, juce::dontSendNotification);
        const auto selected = mapping.getSelectedId() - 1;
        if (selected < 0 || selectedModule < 0) return;
        std::size_t filtered = 0;
        const auto identity = configuration.modules[static_cast<std::size_t>(selectedModule)].stableIdentity;
        for (std::size_t i = 0; i < configuration.assignmentCount; ++i)
            if (configuration.assignments[i].lfoIdentity == identity)
            {
                if (filtered++ == static_cast<std::size_t>(selected))
                { amount.setValue(configuration.assignments[i].amount, juce::dontSendNotification); bipolar.setToggleState(configuration.assignments[i].bipolar, juce::dontSendNotification); inverted.setToggleState(configuration.assignments[i].inverted, juce::dontSendNotification); break; }
            }
    }
    int selectedAssignmentIndex() const
    {
        if (selectedModule < 0) return -1;
        const auto selected = mapping.getSelectedId() - 1;
        if (selected < 0) return -1;
        const auto identity = configuration.modules[static_cast<std::size_t>(selectedModule)].stableIdentity;
        int filtered = 0;
        for (std::size_t i = 0; i < configuration.assignmentCount; ++i)
            if (configuration.assignments[i].lfoIdentity == identity && filtered++ == selected)
                return static_cast<int>(i);
        return -1;
    }
    void saveMapping()
    {
        if (refreshing) return;
        configuration = owner.getLfoConfiguration();
        const auto selected = selectedAssignmentIndex();
        if (selected < 0) return;
        auto& a = configuration.assignments[static_cast<std::size_t>(selected)];
        a.amount = static_cast<float>(amount.getValue()); a.bipolar = bipolar.getToggleState(); a.inverted = inverted.getToggleState();
        if (owner.setLfoConfiguration(configuration))
            observedRevision = owner.getLfoConfigurationRevision();
        if (focusCallback && selectedModule >= 0
            && static_cast<std::size_t>(selectedModule) < configuration.moduleCount)
            focusCallback(configuration.modules[static_cast<std::size_t>(selectedModule)].stableIdentity);
        rebuildMappings();
    }
    void createModule()
    {
        configuration = owner.getLfoConfiguration();
        if (configuration.moduleCount >= hypercolor::plugin::maximumLfoModules) return;
        auto& m = configuration.modules[configuration.moduleCount++];
        m.stableIdentity = owner.allocateLfoIdentity();
        m.enabled = true;
        if (owner.setLfoConfiguration(configuration)) { selectedModule = static_cast<int>(configuration.moduleCount - 1); refresh(); }
    }
    void deleteModule()
    {
        configuration = owner.getLfoConfiguration();
        const auto selected = module.getSelectedId() - 1;
        if (selected < 0 || static_cast<std::size_t>(selected) >= configuration.moduleCount) return;
        const auto identity = configuration.modules[static_cast<std::size_t>(selected)].stableIdentity;
        for (std::size_t i = 0; i < configuration.assignmentCount;)
        {
            if (configuration.assignments[i].lfoIdentity == identity)
            { for (std::size_t j = i + 1; j < configuration.assignmentCount; ++j) configuration.assignments[j - 1] = configuration.assignments[j]; --configuration.assignmentCount; }
            else ++i;
        }
        for (std::size_t i = static_cast<std::size_t>(selected) + 1; i < configuration.moduleCount; ++i) configuration.modules[i - 1] = configuration.modules[i];
        --configuration.moduleCount; selectedModule = juce::jmax(0, selected - 1);
        if (owner.setLfoConfiguration(configuration)) refresh();
    }
    void createMapping()
    {
        configuration = owner.getLfoConfiguration();
        const auto m = module.getSelectedId() - 1, fx = fxInstance.getSelectedId() - 1, p = parameter.getSelectedId() - 1;
        if (m < 0 || fx < 0 || p < 0 || static_cast<std::size_t>(m) >= configuration.moduleCount
            || static_cast<std::size_t>(fx) >= instances.size() || static_cast<std::size_t>(p) >= parameterIDs.size()
            || configuration.assignmentCount >= hypercolor::plugin::maximumLfoAssignments) return;
        const auto instanceID = instances[static_cast<std::size_t>(fx)];
        auto& a = configuration.assignments[configuration.assignmentCount++];
        a.lfoIdentity = configuration.modules[static_cast<std::size_t>(m)].stableIdentity;
        a.fxInstanceIdentity = instanceID.stableIdentity; a.parameterID = parameterIDs[static_cast<std::size_t>(p)];
        a.amount = static_cast<float>(amount.getValue()); a.bipolar = bipolar.getToggleState(); a.inverted = inverted.getToggleState();
        if (owner.setLfoConfiguration(configuration))
        {
            if (focusCallback) focusCallback(configuration.modules[static_cast<std::size_t>(m)].stableIdentity);
            refresh(); mapping.setSelectedId(mapping.getNumItems(), juce::dontSendNotification);
        }
    }
    void deleteMapping()
    {
        configuration = owner.getLfoConfiguration(); const auto selected = selectedAssignmentIndex();
        if (selected < 0) return;
        for (std::size_t i = static_cast<std::size_t>(selected) + 1; i < configuration.assignmentCount; ++i) configuration.assignments[i - 1] = configuration.assignments[i];
        --configuration.assignmentCount; if (owner.setLfoConfiguration(configuration)) refresh();
    }

    HypercolorAudioProcessor& owner;
    std::function<void(std::uint64_t)> focusCallback;
    hypercolor::plugin::LfoConfiguration configuration;
    juce::Label title, title2 { "ASSIGNMENTS" }, rateLabel, depthLabel, phaseLabel, offsetLabel, smoothLabel, amountLabel, help;
    juce::ComboBox module, waveform, rateMode, division, fxInstance, parameter, mapping;
    std::array<LfoModuleCard, hypercolor::plugin::maximumLfoModules> moduleCards;
    juce::Slider rate, depth, phase, offset, smooth, amount;
    juce::TextButton enabled { "ON" }, bipolar { "BIPOLAR" }, inverted { "INVERT" }, addModule, removeModule, addMapping, removeMapping { "REMOVE" };
    juce::TextButton undoWave, clearWave, resetWave;
    LfoWavePreview preview;
    CustomLfoWaveEditor customEditor;
    hypercolor::dsp::modulation::Lfo::CustomWaveform undoWaveform {};
    std::uint64_t undoLfoIdentity = 0;
    std::vector<hypercolor::plugin::FxInstanceId> instances;
    std::vector<juce::String> parameterIDs;
    int selectedModule = -1;
    bool refreshing = false;
    bool hasUndoWaveform = false;
    std::uint32_t observedRevision = 0;
};

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

namespace
{
class PresetBrowserPanel final : public juce::Component,
                                 private juce::ListBoxModel
{
public:
    PresetBrowserPanel(hypercolor::plugin::PresetManager& managerToUse,
                       std::function<void()> onChanged)
        : manager(managerToUse), changed(std::move(onChanged))
    {
        setSize(520, 560);
        title.setText("PRESET LIBRARY", juce::dontSendNotification);
        title.setColour(juce::Label::textColourId, hypercolor::ui::palette::text());
        title.setFont(juce::FontOptions(16.0f).withStyle("Bold"));
        addAndMakeVisible(title);
        search.setTextToShowWhenEmpty("Search presets", hypercolor::ui::palette::mutedText());
        addAndMakeVisible(search);
        source.addItem("ALL", 1); source.addItem("FACTORY", 2);
        source.addItem("USER", 3); source.addItem("FAVORITES", 4);
        source.setSelectedId(1, juce::dontSendNotification);
        addAndMakeVisible(source);
        category.addItem("ALL CATEGORIES", 1);
        const auto& categories = manager.factoryCategories();
        for (int i = 0; i < categories.size(); ++i) category.addItem(categories[i], i + 2);
        category.setSelectedId(1, juce::dontSendNotification);
        addAndMakeVisible(category);
        list.setModel(this);
        list.setRowHeight(34);
        list.setColour(juce::ListBox::backgroundColourId, hypercolor::ui::palette::panel());
        list.setColour(juce::ListBox::outlineColourId, hypercolor::ui::palette::border());
        addAndMakeVisible(list);
        for (auto* button : { &favorite, &rename, &remove })
        {
            button->setColour(juce::TextButton::buttonColourId, hypercolor::ui::palette::panelRaised());
            button->setColour(juce::TextButton::textColourOffId, hypercolor::ui::palette::cyan());
            addAndMakeVisible(*button);
        }
        favorite.setButtonText("☆ FAVORITE"); rename.setButtonText("RENAME"); remove.setButtonText("DELETE");
        search.onTextChange = [this] { rebuild(); };
        source.onChange = [this] { rebuild(); };
        category.onChange = [this] { rebuild(); };
        favorite.onClick = [this] { toggleFavorite(); };
        rename.onClick = [this] { renameSelected(); };
        remove.onClick = [this] { deleteSelected(); };
        rebuild();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(hypercolor::ui::palette::panel().darker(0.12f));
        g.setColour(hypercolor::ui::palette::border());
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 8.0f, 1.0f);
        g.setColour(hypercolor::ui::palette::cyan()); g.fillRect(0, 0, 64, 2);
        if (visible.empty())
        {
            g.setColour(hypercolor::ui::palette::mutedText());
            g.setFont(juce::FontOptions(12.0f));
            g.drawText("Нет пресетов для выбранного фильтра.", list.getBounds(), juce::Justification::centred);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced(14, 12);
        title.setBounds(r.removeFromTop(26)); r.removeFromTop(8);
        auto filters = r.removeFromTop(30);
        source.setBounds(filters.removeFromLeft(128)); filters.removeFromLeft(8); category.setBounds(filters);
        r.removeFromTop(8); search.setBounds(r.removeFromTop(30)); r.removeFromTop(8);
        auto actions = r.removeFromBottom(30); list.setBounds(r);
        favorite.setBounds(actions.removeFromLeft(150)); actions.removeFromLeft(8);
        rename.setBounds(actions.removeFromLeft(120)); actions.removeFromLeft(8); remove.setBounds(actions);
    }

private:
    int getNumRows() override { return static_cast<int>(visible.size()); }
    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (row < 0 || row >= static_cast<int>(visible.size())) return;
        const auto& info = visible[static_cast<std::size_t>(row)];
        g.fillAll(selected ? hypercolor::ui::palette::cyan().withAlpha(0.15f)
                           : hypercolor::ui::palette::panel());
        g.setColour(info.id == manager.currentId() ? hypercolor::ui::palette::lime()
                                                   : hypercolor::ui::palette::text());
        g.setFont(juce::FontOptions(12.0f).withStyle("Bold"));
        g.drawText(info.name + (info.favorite ? "  ★" : ""), 10, 1, width - 18, 17,
                   juce::Justification::centredLeft, true);
        g.setColour(hypercolor::ui::palette::mutedText());
        g.setFont(juce::FontOptions(9.0f));
        g.drawText((info.factory ? "FACTORY" : "USER") + juce::String("  ·  ") + info.category,
                   10, 17, width - 18, height - 17, juce::Justification::centredLeft, true);
    }
    void selectedRowsChanged(int row) override
    {
        const bool selected = row >= 0 && row < static_cast<int>(visible.size());
        const bool user = selected && !visible[static_cast<std::size_t>(row)].factory;
        favorite.setEnabled(selected); rename.setEnabled(user); remove.setEnabled(user);
        favorite.setButtonText(selected && visible[static_cast<std::size_t>(row)].favorite
            ? "★ FAVORITE" : "☆ FAVORITE");
    }
    void rebuild()
    {
        const auto oldId = selectedInfo() ? selectedInfo()->id : juce::String{};
        visible.clear();
        const auto text = search.getText().trim().toLowerCase();
        const auto sourceFilter = source.getSelectedId();
        const auto categoryFilter = category.getSelectedId();
        for (const auto& info : manager.getPresets())
        {
            if (sourceFilter == 2 && !info.factory) continue;
            if (sourceFilter == 3 && info.factory) continue;
            if (sourceFilter == 4 && !info.favorite) continue;
            if (categoryFilter > 1 && info.category != category.getItemText(categoryFilter - 1)) continue;
            if (text.isNotEmpty() && !info.name.toLowerCase().contains(text)
                && !info.description.toLowerCase().contains(text)) continue;
            visible.push_back(info);
        }
        list.updateContent();
        int selected = -1;
        for (std::size_t i = 0; i < visible.size(); ++i)
            if (visible[i].id == oldId || (oldId.isEmpty() && visible[i].id == manager.currentId())) selected = static_cast<int>(i);
        if (selected >= 0) list.selectRow(selected); else list.deselectAllRows();
        selectedRowsChanged(selected);
        repaint();
    }
    const hypercolor::plugin::PresetInfo* selectedInfo() const
    {
        const auto row = list.getSelectedRow();
        return row >= 0 && row < static_cast<int>(visible.size()) ? &visible[static_cast<std::size_t>(row)] : nullptr;
    }
    void listBoxItemClicked(int row, const juce::MouseEvent&) override
    {
        if (row < 0 || row >= static_cast<int>(visible.size())) return;
        if (manager.load(visible[static_cast<std::size_t>(row)].id)) { if (changed) changed(); rebuild(); }
    }
    void toggleFavorite()
    {
        if (const auto* info = selectedInfo()) { (void) manager.setFavorite(info->id, !info->favorite); rebuild(); }
    }
    void renameSelected()
    {
        const auto* info = selectedInfo(); if (info == nullptr || info->factory) return;
        juce::AlertWindow::showAsync(juce::MessageBoxOptions().withIconType(juce::MessageBoxIconType::NoIcon)
            .withTitle("Rename Preset").withMessage("Enter a new preset name.").withButton("Rename")
            .withButton("Cancel").withTextEditor("name", info->name),
            [this, id = info->id](int result)
            {
                if (result == 1)
                    if (auto* box = juce::AlertWindow::getCurrentlyModalComponent())
                        (void) manager.rename(id, box->getTextEditorContents("name"));
                rebuild(); if (changed) changed();
            });
    }
    void deleteSelected()
    {
        const auto* info = selectedInfo(); if (info == nullptr || info->factory) return;
        const auto id = info->id;
        juce::AlertWindow::showAsync(juce::MessageBoxOptions().withIconType(juce::MessageBoxIconType::WarningIcon)
            .withTitle("Delete Preset?").withMessage("This user preset will be removed.")
            .withButton("Delete").withButton("Cancel"), [this, id](int result)
            { if (result == 1) (void) manager.erase(id); rebuild(); if (changed) changed(); });
    }

    hypercolor::plugin::PresetManager& manager;
    std::function<void()> changed;
    juce::Label title;
    juce::TextEditor search;
    juce::ComboBox source, category;
    juce::ListBox list;
    juce::TextButton favorite, rename, remove;
    std::vector<hypercolor::plugin::PresetInfo> visible;
};

class PresetSavePanel final : public juce::Component
{
public:
    PresetSavePanel(hypercolor::plugin::PresetManager& managerToUse,
                    std::function<void()> onSaved)
        : manager(managerToUse), saved(std::move(onSaved))
    {
        setSize(360, 230);
        title.setText("SAVE PRESET AS", juce::dontSendNotification);
        title.setColour(juce::Label::textColourId, hypercolor::ui::palette::text());
        addAndMakeVisible(title);
        name.setText(manager.currentIsFactory() ? manager.currentName() : "New Preset");
        name.setTextToShowWhenEmpty("Preset name", hypercolor::ui::palette::mutedText()); addAndMakeVisible(name);
        for (int i = 0; i < manager.factoryCategories().size(); ++i)
            category.addItem(manager.factoryCategories()[i], i + 1);
        category.setSelectedId(1); addAndMakeVisible(category);
        description.setTextToShowWhenEmpty("Optional description", hypercolor::ui::palette::mutedText()); addAndMakeVisible(description);
        save.setButtonText("SAVE"); cancel.setButtonText("CANCEL");
        for (auto* button : { &save, &cancel }) { button->setColour(juce::TextButton::buttonColourId, hypercolor::ui::palette::panelRaised()); addAndMakeVisible(*button); }
        save.setColour(juce::TextButton::textColourOffId, hypercolor::ui::palette::cyan());
        save.onClick = [this]
        {
            if (manager.saveAs(name.getText(), category.getText(), description.getText()))
            { if (saved) saved(); dismiss(); }
            else name.setColour(juce::TextEditor::outlineColourId, hypercolor::ui::palette::drive());
        };
        cancel.onClick = [this] { dismiss(); };
    }
    void paint(juce::Graphics& g) override
    { g.fillAll(hypercolor::ui::palette::panel().darker(0.12f)); g.setColour(hypercolor::ui::palette::border()); g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 8, 1); }
    void resized() override
    {
        auto r = getLocalBounds().reduced(14); title.setBounds(r.removeFromTop(24)); r.removeFromTop(8);
        name.setBounds(r.removeFromTop(28)); r.removeFromTop(6); category.setBounds(r.removeFromTop(28));
        r.removeFromTop(6); description.setBounds(r.removeFromTop(28)); r.removeFromTop(8);
        auto row = r.removeFromTop(30); save.setBounds(row.removeFromLeft(row.getWidth() / 2 - 4)); row.removeFromLeft(8); cancel.setBounds(row);
    }
private:
    void dismiss() { if (auto* callout = findParentComponentOfClass<juce::CallOutBox>()) callout->dismiss(); }
    hypercolor::plugin::PresetManager& manager;
    std::function<void()> saved;
    juce::Label title;
    juce::TextEditor name, description;
    juce::ComboBox category;
    juce::TextButton save, cancel;
};
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
    xyController.setComponentID("xy.controller");
    xyController.setCoordinates({ 0.5f, 0.5f }, false);
    macroAssignmentButton.setComponentID("xy.macroAssignments");
    macroAssignmentButton.setTooltip("Assign XY Macro X and Y to FX parameters");
    macroAssignmentButton.setColour(juce::TextButton::buttonColourId, panelRaised);
    macroAssignmentButton.setColour(juce::TextButton::buttonOnColourId, cyan.withAlpha(0.25f));
    macroAssignmentButton.setColour(juce::TextButton::textColourOffId, cyan);
    addAndMakeVisible(macroAssignmentButton);
    macroAssignmentButton.onClick = [this]
    {
        auto panel = std::make_unique<FxMacroAssignmentPanel>(audioProcessor);
        juce::CallOutBox::launchAsynchronously(std::move(panel), macroAssignmentButton, this);
    };

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
        button.setClickingTogglesState(true);
        button.setRadioGroupId(1701);
        button.setColour(juce::TextButton::buttonColourId, panelRaised);
        button.setColour(juce::TextButton::buttonOnColourId, hypercolor::ui::palette::lime());
        button.setColour(juce::TextButton::textColourOffId, text);
        button.setColour(juce::TextButton::textColourOnId, hypercolor::ui::palette::background());
        button.onStateChange = [this, index]
        {
            if (effectButtons[index].getToggleState())
                selectEffect(index);
        };
        button.setComponentID("module." + juce::String(static_cast<int>(index)));
        button.addMouseListener(this, false);
        addAndMakeVisible(button);

        auto& enableButton = fxEnableButtons[index];
        enableButton.setClickingTogglesState(true);
        enableButton.setColour(
            juce::TextButton::buttonColourId,
            hypercolor::ui::palette::panelRaised()
        );
        enableButton.setColour(juce::TextButton::buttonOnColourId, hypercolor::ui::palette::lime());
        enableButton.setColour(
            juce::TextButton::textColourOffId,
            hypercolor::ui::palette::mutedText()
        );
        enableButton.setColour(
            juce::TextButton::textColourOnId,
            hypercolor::ui::palette::background()
        );
        enableButton.onStateChange = [enableButtonPtr = &enableButton]
        {
            enableButtonPtr->setButtonText(
                enableButtonPtr->getToggleState() ? "ON" : "OFF"
            );
        };
        enableButton.onStateChange();
        addAndMakeVisible(enableButton);

        auto& removeButton = fxRemoveButtons[index];
        removeButton.setButtonText("×");
        removeButton.setComponentID("fx.remove." + juce::String(static_cast<int>(index)));
        removeButton.setTooltip("Remove this FX instance");
        removeButton.setColour(juce::TextButton::buttonColourId, panelRaised);
        removeButton.setColour(juce::TextButton::buttonOnColourId, hypercolor::ui::palette::drive());
        removeButton.setColour(juce::TextButton::textColourOffId, hypercolor::ui::palette::mutedText());
        removeButton.onClick = [this, index] { removeEffect(index); };
        addAndMakeVisible(removeButton);
    }
    addFxButton.setButtonText("+ ADD EFFECT");
    addFxButton.setComponentID("fx.add");
    addFxButton.setColour(juce::TextButton::buttonColourId, panelRaised);
    addFxButton.setColour(juce::TextButton::buttonOnColourId, hypercolor::ui::palette::lime());
    addFxButton.setColour(juce::TextButton::textColourOffId, hypercolor::ui::palette::lime());
    addFxButton.onClick = [this] { showAddEffectMenu(); };
    addAndMakeVisible(addFxButton);
    lfoWorkspaceButton.setComponentID("lfo.workspace");
    lfoWorkspaceButton.setTooltip("Create DSP LFOs and assign them to FX parameters");
    lfoWorkspaceButton.setColour(juce::TextButton::buttonColourId, panelRaised);
    lfoWorkspaceButton.setColour(juce::TextButton::textColourOffId, hypercolor::ui::palette::space());
    addAndMakeVisible(lfoWorkspaceButton);
    lfoWorkspaceButton.onClick = [this] { showLfoWorkspace(); };
    granularNavButton.setToggleState(true, juce::dontSendNotification);
    moduleTitleLabel.setText("GRANULAR CORE", juce::dontSendNotification);
    moduleSubtitleLabel.setText("XY CHARACTER FIELD  //  REAL-TIME HISTORY  //  64 VOICES", juce::dontSendNotification);

    auto& state = audioProcessor.getValueTreeState();
    refreshFxRack();
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
    state.addParameterListener(hypercolor::ParameterIDs::macroX, this);
    state.addParameterListener(hypercolor::ParameterIDs::macroY, this);
    const auto* macroX = state.getRawParameterValue(hypercolor::ParameterIDs::macroX);
    const auto* macroY = state.getRawParameterValue(hypercolor::ParameterIDs::macroY);
    const auto initialX = macroX != nullptr ? macroX->load(std::memory_order_relaxed) : 0.5f;
    const auto initialY = macroY != nullptr ? macroY->load(std::memory_order_relaxed) : 0.5f;
    pendingMacroX.store(initialX, std::memory_order_relaxed);
    pendingMacroY.store(initialY, std::memory_order_relaxed);
    xyController.setCoordinates({ initialX, initialY }, false);
    xyController.setOnChange([this](hypercolor::ui::XYCoordinates coordinates)
    {
        auto& valueTree = audioProcessor.getValueTreeState();
        if (auto* parameter = valueTree.getParameter(hypercolor::ParameterIDs::macroX))
            parameter->setValueNotifyingHost(coordinates.x);
        if (auto* parameter = valueTree.getParameter(hypercolor::ParameterIDs::macroY))
            parameter->setValueNotifyingHost(coordinates.y);
    });
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
    audioProcessor.getValueTreeState().removeParameterListener(
        hypercolor::ParameterIDs::macroX, this);
    audioProcessor.getValueTreeState().removeParameterListener(
        hypercolor::ParameterIDs::macroY, this);
    cancelPendingUpdate();
    stopTimer();
    densitySlider.onInteractionFinished = nullptr;
    densityModeSelector.onChange = nullptr;
    macroAssignmentButton.onClick = nullptr;
    lfoWorkspaceButton.onClick = nullptr;
    xyController.setOnChange(nullptr);
    densityAttachment.reset();
    densityModeAttachment.reset();
    sourceModeAttachment.reset();
    setLookAndFeel(nullptr);
}

void HypercolorAudioProcessorEditor::showLfoWorkspace()
{
    juce::Component::SafePointer<HypercolorAudioProcessorEditor> safeEditor(this);
    auto panel = std::make_unique<LfoWorkspacePanel>(audioProcessor,
        [safeEditor](std::uint64_t identity)
        {
            if (safeEditor != nullptr)
            {
                safeEditor->focusedLfoIdentity = identity;
                safeEditor->rebuildInspector();
            }
        });
    juce::CallOutBox::launchAsynchronously(std::move(panel), lfoWorkspaceButton, this);
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
    {
        if (parameterID == hypercolor::ParameterIDs::macroX)
            pendingMacroX.store(juce::jlimit(0.0f, 1.0f, newValue), std::memory_order_release);
        else if (parameterID == hypercolor::ParameterIDs::macroY)
            pendingMacroY.store(juce::jlimit(0.0f, 1.0f, newValue), std::memory_order_release);
        else
            return;
        triggerAsyncUpdate();
        return;
    }

    pendingDensityMode.store(
        newValue >= 0.5f ? 1 : 0,
        std::memory_order_release
    );
    triggerAsyncUpdate();
}

void HypercolorAudioProcessorEditor::handleAsyncUpdate()
{
    synchronizeDensityMode();
    xyController.setCoordinates({
        pendingMacroX.load(std::memory_order_acquire),
        pendingMacroY.load(std::memory_order_acquire)
    }, false);
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
    if (dropRackIndex >= 0)
    {
        const auto configuration = audioProcessor.getFxChainConfiguration();
        if (configuration.count > 0)
        {
            const auto index = std::min(static_cast<std::size_t>(dropRackIndex),
                                        static_cast<std::size_t>(configuration.count - 1));
            const auto bounds = effectButtons[index].getBounds();
            const auto y = dropRackIndex >= static_cast<int>(configuration.count)
                ? bounds.getBottom() : bounds.getY();
            graphics.setColour(hypercolor::ui::palette::lime().withAlpha(0.9f));
            graphics.fillRect(rackPanel.getX() + 8, y - 1, rackPanel.getWidth() - 16, 2);
        }
    }
}

void HypercolorAudioProcessorEditor::mouseDown(const juce::MouseEvent& event)
{
    for (std::size_t i = 0; i < effectButtons.size(); ++i)
        if (event.originalComponent == &effectButtons[i])
        {
            draggingRackIndex = static_cast<int>(i);
            dropRackIndex = static_cast<int>(i);
            return;
        }
}

void HypercolorAudioProcessorEditor::mouseDrag(const juce::MouseEvent& event)
{
    if (draggingRackIndex < 0)
        return;
    const auto point = event.getEventRelativeTo(this).getPosition();
    const auto configuration = audioProcessor.getFxChainConfiguration();
    for (std::size_t i = 0; i < configuration.count; ++i)
    {
        if (point.y < effectButtons[i].getBounds().getCentreY())
        {
            dropRackIndex = static_cast<int>(i);
            effectButtons[static_cast<std::size_t>(draggingRackIndex)].setAlpha(0.55f);
            repaint(rackPanel);
            return;
        }
    }
    dropRackIndex = static_cast<int>(configuration.count);
    effectButtons[static_cast<std::size_t>(draggingRackIndex)].setAlpha(0.55f);
    repaint(rackPanel);
}

void HypercolorAudioProcessorEditor::mouseUp(const juce::MouseEvent&)
{
    if (draggingRackIndex >= 0)
    {
        auto& dragged = effectButtons[static_cast<std::size_t>(draggingRackIndex)];
        dragged.setAlpha(1.0f);
        auto target = dropRackIndex;
        const auto configuration = audioProcessor.getFxChainConfiguration();
        if (target >= static_cast<int>(configuration.count))
            target = static_cast<int>(configuration.count) - 1;
        if (target >= 0)
            moveEffect(static_cast<std::size_t>(draggingRackIndex), static_cast<std::size_t>(target));
    }
    draggingRackIndex = -1;
    dropRackIndex = -1;
    repaint(rackPanel);
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
    const auto configuration = audioProcessor.getFxChainConfiguration();
    if (index >= configuration.count || index >= effectButtons.size())
        return;

    selectedRackIndex = index;
    selectedModule = static_cast<int>(index);
    selectedEffect = hypercolor::plugin::toIndex(configuration.order[index].type);
    selectedEffectSlot = configuration.order[index].slot;
    granularNavButton.setToggleState(false, juce::dontSendNotification);
    for (std::size_t i = 0; i < effectButtons.size(); ++i)
        effectButtons[i].setToggleState(i == selectedRackIndex, juce::dontSendNotification);
    rebuildInspector();
}

void HypercolorAudioProcessorEditor::refreshFxRack()
{
    for (auto& attachment : fxEnableAttachments)
        attachment.reset();
    const auto configuration = audioProcessor.getFxChainConfiguration();
    auto& state = audioProcessor.getValueTreeState();
    for (std::size_t i = 0; i < effectButtons.size(); ++i)
    {
        const auto active = i < configuration.count;
        effectButtons[i].setVisible(active);
        fxEnableButtons[i].setVisible(active);
        fxRemoveButtons[i].setVisible(active);
        if (!active)
            continue;
        const auto id = configuration.order[i];
        const auto type = hypercolor::plugin::toIndex(id.type);
        const auto label = juce::String(effectNames[type])
            + (id.slot == 0 ? juce::String() : " #" + juce::String(static_cast<int>(id.slot + 1)));
        effectButtons[i].setButtonText(label);
        effectButtons[i].setTooltip(label + " — drag to reorder");
        effectButtons[i].setColour(juce::TextButton::buttonOnColourId, effectAccent(type));
        effectButtons[i].setToggleState(
            selectedModule >= 0 && selectedRackIndex == i,
            juce::dontSendNotification
        );
        const auto enabledID = hypercolor::plugin::fxEnabledParameterID(
            id.type, id.slot, effectEnableParameterIDs[type]
        );
        fxEnableButtons[i].setComponentID(enabledID);
        fxRemoveButtons[i].setComponentID("fx.remove." + juce::String(static_cast<int>(i)));
        fxEnableButtons[i].setColour(juce::TextButton::buttonOnColourId, effectAccent(type));
        fxEnableButtons[i].setTooltip(label + " ON / OFF");
        fxEnableAttachments[i] = std::make_unique<ButtonAttachment>(
            state, enabledID, fxEnableButtons[i]
        );
    }
    addFxButton.setEnabled(configuration.count < hypercolor::plugin::maximumFxChainModules);
    addFxButton.setButtonText(configuration.count >= hypercolor::plugin::maximumFxChainModules
        ? "FX RACK FULL" : "+ ADD EFFECT");
    resized();
    repaint(rackPanel);
}

void HypercolorAudioProcessorEditor::showAddEffectMenu()
{
    juce::PopupMenu menu;
    const auto configuration = audioProcessor.getFxChainConfiguration();
    for (std::size_t i = 0; i < effectNames.size(); ++i)
    {
        std::size_t instances = 0;
        for (std::size_t j = 0; j < configuration.count; ++j)
            instances += hypercolor::plugin::toIndex(configuration.order[j].type) == i ? 1u : 0u;
        menu.addItem(static_cast<int>(i + 1), effectNames[i],
                     instances < hypercolor::plugin::maximumFxInstancesPerType);
    }
    juce::Component::SafePointer<HypercolorAudioProcessorEditor> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&addFxButton),
        [safe](int result)
        {
            if (safe != nullptr && result >= 1 && result <= 8)
                safe->addEffect(static_cast<hypercolor::plugin::FxType>(result - 1));
        });
}

void HypercolorAudioProcessorEditor::addEffect(hypercolor::plugin::FxType type)
{
    auto configuration = audioProcessor.getFxChainConfiguration();
    if (configuration.count >= hypercolor::plugin::maximumFxChainModules)
        return;
    std::array<bool, hypercolor::plugin::maximumFxInstancesPerType> used {};
    for (std::size_t i = 0; i < configuration.count; ++i)
        if (configuration.order[i].type == type)
            used[configuration.order[i].slot] = true;
    const auto free = std::find(used.begin(), used.end(), false);
    if (free == used.end())
        return;
    const auto slot = static_cast<std::uint8_t>(std::distance(used.begin(), free));
    configuration.order[configuration.count++] = { type, slot };
    configuration.order[configuration.count - 1].stableIdentity =
        audioProcessor.allocateFxInstanceIdentity();
    if (configuration.order[configuration.count - 1].stableIdentity == 0)
        return;
    if (!audioProcessor.setFxChainConfiguration(configuration))
        return;
    selectedModule = static_cast<int>(configuration.count - 1);
    selectedRackIndex = configuration.count - 1;
    selectedEffect = hypercolor::plugin::toIndex(type);
    selectedEffectSlot = slot;
    refreshFxRack();
    rebuildInspector();
}

void HypercolorAudioProcessorEditor::removeEffect(std::size_t rackIndex)
{
    auto configuration = audioProcessor.getFxChainConfiguration();
    if (rackIndex >= configuration.count)
        return;
    for (std::size_t i = rackIndex + 1; i < configuration.count; ++i)
        configuration.order[i - 1] = configuration.order[i];
    --configuration.count;
    if (!audioProcessor.setFxChainConfiguration(configuration))
        return;
    if (configuration.count == 0)
    {
        selectGranular();
        refreshFxRack();
        return;
    }
    selectedRackIndex = std::min(rackIndex, static_cast<std::size_t>(configuration.count - 1));
    selectedModule = static_cast<int>(selectedRackIndex);
    selectedEffect = hypercolor::plugin::toIndex(configuration.order[selectedRackIndex].type);
    selectedEffectSlot = configuration.order[selectedRackIndex].slot;
    refreshFxRack();
    rebuildInspector();
}

void HypercolorAudioProcessorEditor::moveEffect(std::size_t from, std::size_t to)
{
    auto configuration = audioProcessor.getFxChainConfiguration();
    if (from >= configuration.count || to >= configuration.count || from == to)
        return;
    const auto moved = configuration.order[from];
    if (from < to)
        for (std::size_t i = from; i < to; ++i)
            configuration.order[i] = configuration.order[i + 1];
    else
        for (std::size_t i = from; i > to; --i)
            configuration.order[i] = configuration.order[i - 1];
    configuration.order[to] = moved;
    if (audioProcessor.setFxChainConfiguration(configuration))
    {
        selectedRackIndex = to;
        selectedEffect = hypercolor::plugin::toIndex(moved.type);
        selectedEffectSlot = moved.slot;
        selectedModule = static_cast<int>(to);
        refreshFxRack();
        rebuildInspector();
    }
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
    const auto parameterCount = selectedModule < 0
        ? std::size_t { 7 }
        : effectParameterCounts[selectedEffect];

    inspectorSectionLabel.setText(
        selectedModule < 0
            ? "GRANULAR DETAIL  /  VARIATION"
            : juce::String("MODULE CONTROL  /  ") + effectNames[selectedEffect]
                + (selectedEffectSlot == 0 ? juce::String() : "  #" + juce::String(static_cast<int>(selectedEffectSlot + 1))),
        juce::dontSendNotification
    );
    moduleTitleLabel.setText(
        selectedModule < 0 ? "GRANULAR CORE" : juce::String(effectNames[selectedEffect])
            + (selectedEffectSlot == 0 ? juce::String() : "  #" + juce::String(static_cast<int>(selectedEffectSlot + 1))),
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
        const auto parameterID = selectedModule < 0
            ? juce::String(granularParameters[i] == nullptr ? "" : granularParameters[i])
            : hypercolor::plugin::fxParameterID(
                static_cast<hypercolor::plugin::FxType>(selectedEffect),
                selectedEffectSlot,
                effectParameters[selectedEffect][i]
            );
        if (parameterID.isEmpty())
            continue;
        auto* parameter = audioProcessor.getValueTreeState().getParameter(parameterID);
        if (parameter == nullptr)
            continue;

        if (selectedModule >= 0)
        {
            auto tooltip = parameter->getName(48);
            const auto currentIdentity = audioProcessor.getFxChainConfiguration()
                .order[static_cast<std::size_t>(selectedModule)].stableIdentity;
            const auto assignments = audioProcessor.getFxMacroAssignments();
            for (std::size_t assignmentIndex = 0; assignmentIndex < assignments.count; ++assignmentIndex)
            {
                const auto& mapping = assignments.assignments[assignmentIndex];
                if (mapping.instanceIdentity == currentIdentity
                    && mapping.parameterID == parameterID)
                {
                    tooltip << " — mapped to Macro "
                            << (mapping.axis == hypercolor::plugin::MacroAxis::x ? "X" : "Y")
                            << "; the XY value controls DSP while the base value remains stored.";
                    break;
                }
            }
            if (focusedLfoIdentity != 0)
            {
                const auto lfoConfig = audioProcessor.getLfoConfiguration();
                for (std::size_t assignmentIndex = 0; assignmentIndex < lfoConfig.assignmentCount; ++assignmentIndex)
                {
                    const auto& mapping = lfoConfig.assignments[assignmentIndex];
                    if (mapping.lfoIdentity == focusedLfoIdentity
                        && mapping.fxInstanceIdentity == currentIdentity
                        && mapping.parameterID == parameterID)
                    {
                        tooltip << " — modulated by selected LFO";
                        break;
                    }
                }
            }
            fxRotaryKnobs[i].setTooltip(tooltip);
        }

        const auto isEffect = selectedModule >= 0;
        auto& slider = isEffect
            ? static_cast<juce::Slider&>(fxRotaryKnobs[i])
            : inspectorSliders[i];
        auto& label = inspectorLabels[i];
        const auto range = parameter->getNormalisableRange();
        const auto isEffectMix = isEffect && i == effectMixIndices[selectedEffect];
        auto accent = isEffect
            ? effectAccent(selectedEffect)
            : (i == 1 || i == 2 ? hypercolor::ui::palette::pink()
                                : hypercolor::ui::palette::cyan());
        bool mappedByFocusedLfo = false;
        if (isEffect && focusedLfoIdentity != 0)
        {
            const auto chain = audioProcessor.getFxChainConfiguration();
            const auto identity = chain.order[static_cast<std::size_t>(selectedModule)].stableIdentity;
            const auto lfoConfig = audioProcessor.getLfoConfiguration();
            for (std::size_t assignmentIndex = 0; assignmentIndex < lfoConfig.assignmentCount; ++assignmentIndex)
            {
                const auto& mapping = lfoConfig.assignments[assignmentIndex];
                if (mapping.lfoIdentity == focusedLfoIdentity
                    && mapping.fxInstanceIdentity == identity
                    && mapping.parameterID == parameterID)
                { mappedByFocusedLfo = true; break; }
            }
            if (mappedByFocusedLfo)
                accent = hypercolor::ui::palette::space();
        }
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
            if (parameterID == hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::drive, selectedEffectSlot, hypercolor::ParameterIDs::driveMode))
                displayName = "MODE";
            else if (parameterID == hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::drive, selectedEffectSlot, hypercolor::ParameterIDs::driveAmount))
                displayName = "DRIVE";
            else if (parameterID == hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::drive, selectedEffectSlot, hypercolor::ParameterIDs::driveTone))
                displayName = "TONE";
            else if (parameterID == hypercolor::plugin::fxParameterID(hypercolor::plugin::FxType::drive, selectedEffectSlot, hypercolor::ParameterIDs::driveOutput))
                displayName = "OUTPUT";
        }
        label.setComponentID("inspector.label." + juce::String(static_cast<int>(i)));
        label.setText(displayName, juce::dontSendNotification);
        label.setColour(juce::Label::textColourId, hypercolor::ui::palette::mutedText());
        if (mappedByFocusedLfo)
            label.setColour(juce::Label::textColourId, hypercolor::ui::palette::space());
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
            hypercolor::plugin::fxParameterID(
                static_cast<hypercolor::plugin::FxType>(selectedEffect),
                selectedEffectSlot,
                effectParameters[selectedEffect][index]
            )
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
    const auto rackConfiguration = audioProcessor.getFxChainConfiguration();
    const auto addRowHeight = 29;
    const auto fxRackRowHeight = rackConfiguration.count == 0 ? 28
        : juce::jlimit(21, 30, (navigationArea.getHeight() - addRowHeight)
            / static_cast<int>(rackConfiguration.count));
    for (std::size_t index = 0; index < rackConfiguration.count; ++index)
    {
        auto& button = effectButtons[index];
        auto row = navigationArea.removeFromTop(fxRackRowHeight);
        auto removeBounds = row.removeFromRight(22);
        row.removeFromRight(2);
        auto enableBounds = row.removeFromRight(32);
        row.removeFromRight(3);
        button.setBounds(row);
        fxEnableButtons[index].setBounds(enableBounds);
        fxRemoveButtons[index].setBounds(removeBounds);
        if (index + 1 < rackConfiguration.count)
            navigationArea.removeFromTop(2);
    }
    addFxButton.setBounds(navigationArea.removeFromTop(addRowHeight));
    if (navigationArea.getHeight() >= 25)
    {
        navigationArea.removeFromTop(4);
        lfoWorkspaceButton.setBounds(navigationArea.removeFromTop(26));
    }
    else
    {
        lfoWorkspaceButton.setBounds(rackPanel.getX() + 10, rackPanel.getBottom() - 31,
                                     rackPanel.getWidth() - 20, 25);
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
             static_cast<juce::Component*>(&macroAssignmentButton),
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
        auto macroButtonBounds = sectionHeader.removeFromRight(78);
        sectionHeader.removeFromRight(4);
        auto sourceSelectorBounds = sectionHeader.removeFromRight(
            juce::jmin(104, juce::jmax(84, sectionHeader.getWidth() / 3))
        );
        granularSectionLabel.setBounds(sectionHeader);
        sourceModeSelector.setBounds(sourceSelectorBounds.reduced(1, 1));
        macroAssignmentButton.setBounds(macroButtonBounds.reduced(1, 1));
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
