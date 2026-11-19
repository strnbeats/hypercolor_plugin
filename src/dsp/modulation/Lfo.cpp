#include "dsp/modulation/Lfo.h"

#include <algorithm>
#include <cmath>

namespace hypercolor::dsp::modulation
{
bool Lfo::prepare(double requestedSampleRate) noexcept
{
    if (!std::isfinite(requestedSampleRate) || requestedSampleRate <= 0.0)
    {
        prepared = false;
        return false;
    }
    sampleRate = requestedSampleRate;
    prepared = true;
    return true;
}

void Lfo::reset(std::uint32_t seed) noexcept
{
    phase = 0.0;
    randomState = seed == 0 ? 0x9e3779b9u : seed;
    heldValue = randomBipolar();
}

void Lfo::setFrequency(double requestedFrequency) noexcept
{
    frequencyHz = std::isfinite(requestedFrequency)
        ? std::clamp(requestedFrequency, 0.0, maximumFrequencyHz) : 0.0;
}

void Lfo::setPhase(double normalizedPhase) noexcept
{
    if (!std::isfinite(normalizedPhase))
        return;
    phase = normalizedPhase - std::floor(normalizedPhase);
}

float Lfo::evaluate(LfoWaveform waveform, float normalizedPhase,
                    float held) noexcept
{
    if (!std::isfinite(normalizedPhase))
        return 0.0f;
    const auto p = normalizedPhase - std::floor(normalizedPhase);
    switch (waveform)
    {
        case LfoWaveform::sine:
            return std::sin(6.2831853071795864769f * p);
        case LfoWaveform::triangle:
            return 1.0f - 4.0f * std::abs(p - 0.5f);
        case LfoWaveform::sawUp:
            return 2.0f * p - 1.0f;
        case LfoWaveform::sawDown:
            return 1.0f - 2.0f * p;
        case LfoWaveform::square:
            return p < 0.5f ? 1.0f : -1.0f;
        case LfoWaveform::sampleAndHold:
            return std::isfinite(held) ? std::clamp(held, -1.0f, 1.0f) : 0.0f;
    }
    return 0.0f;
}

float Lfo::nextSample(LfoWaveform waveform, float phaseOffset) noexcept
{
    const auto offset = std::isfinite(phaseOffset) ? phaseOffset : 0.0f;
    const auto output = evaluate(waveform,
        static_cast<float>(phase) + offset, heldValue);
    if (prepared && frequencyHz > 0.0)
    {
        phase += frequencyHz / sampleRate;
        if (phase >= 1.0)
        {
            phase -= std::floor(phase);
            if (waveform == LfoWaveform::sampleAndHold)
                heldValue = randomBipolar();
        }
    }
    return output;
}

float Lfo::randomBipolar() noexcept
{
    auto value = randomState;
    value ^= value << 13u;
    value ^= value >> 17u;
    value ^= value << 5u;
    randomState = value == 0 ? 0x9e3779b9u : value;
    const auto unit = static_cast<float>(randomState >> 8u) / 16777215.0f;
    return 2.0f * unit - 1.0f;
}
}
