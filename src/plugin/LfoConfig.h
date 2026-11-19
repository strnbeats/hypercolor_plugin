#pragma once

#include "FxRackConfig.h"
#include "dsp/modulation/Lfo.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace hypercolor::plugin
{
inline constexpr std::size_t maximumLfoModules = 8;
inline constexpr std::size_t maximumLfoAssignments = 128;

enum class LfoRateMode : std::uint8_t { free, bpmSync };
enum class LfoDivision : std::uint8_t
{
    whole, wholeTriplet, half, halfTriplet, quarter, quarterTriplet, eighth,
    eighthTriplet, sixteenth, sixteenthTriplet, thirtySecond,
    thirtySecondTriplet
};

inline constexpr std::array<const char*, 12> lfoDivisionLabels {
    "1/1", "1/1T", "1/2", "1/2T", "1/4", "1/4T", "1/8", "1/8T",
    "1/16", "1/16T", "1/32", "1/32T"
};
inline constexpr std::array<float, 12> lfoDivisionCyclesPerQuarter {
    0.25f, 0.375f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f,
    4.0f, 6.0f, 8.0f, 12.0f
};

inline double lfoFrequencyHz(LfoRateMode mode, float freeRateHz,
                             LfoDivision division, double bpm) noexcept
{
    if (mode == LfoRateMode::free)
        return std::isfinite(freeRateHz)
            ? std::clamp(static_cast<double>(freeRateHz), 0.0, 40.0) : 0.0;
    const auto index = static_cast<std::size_t>(division);
    if (index >= lfoDivisionCyclesPerQuarter.size() || !std::isfinite(bpm))
        return 0.0;
    return std::clamp(bpm, 20.0, 300.0) / 60.0
        * static_cast<double>(lfoDivisionCyclesPerQuarter[index]);
}

struct LfoModuleConfig
{
    std::uint64_t stableIdentity = 0;
    bool enabled = true;
    hypercolor::dsp::modulation::LfoWaveform waveform
        = hypercolor::dsp::modulation::LfoWaveform::sine;
    LfoRateMode rateMode = LfoRateMode::free;
    float rateHz = 1.0f;
    LfoDivision division = LfoDivision::quarter;
    float depth = 1.0f;
    float phase = 0.0f;
    float offset = 0.0f;
    float smooth = 0.0f;
    hypercolor::dsp::modulation::Lfo::CustomWaveform customWaveform
        = hypercolor::dsp::modulation::Lfo::makeDefaultCustomWaveform();
};

struct LfoParameterAssignment
{
    std::uint64_t lfoIdentity = 0;
    std::uint64_t fxInstanceIdentity = 0;
    juce::String parameterID;
    float amount = 1.0f;
    bool bipolar = true;
    bool inverted = false;
};

struct LfoConfiguration
{
    std::array<LfoModuleConfig, maximumLfoModules> modules {};
    std::uint8_t moduleCount = 0;
    std::array<LfoParameterAssignment, maximumLfoAssignments> assignments {};
    std::uint16_t assignmentCount = 0;
};

inline constexpr const char* lfoWaveformLabel(
    hypercolor::dsp::modulation::LfoWaveform waveform) noexcept
{
    using W = hypercolor::dsp::modulation::LfoWaveform;
    switch (waveform)
    {
        case W::sine: return "Sine";
        case W::triangle: return "Triangle";
        case W::sawUp: return "Saw Up";
        case W::sawDown: return "Saw Down";
        case W::square: return "Square";
        case W::sampleAndHold: return "Random / S&H";
        case W::custom: return "Custom";
    }
    return "Sine";
}
}
