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
                    float held, const CustomWaveform* custom) noexcept
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
        case LfoWaveform::custom:
            if (custom == nullptr)
                return 0.0f;
            {
                // The final table value is the loop seam and is kept equal to
                // the first by the non-audio preparation path.
                const auto position = p * static_cast<float>(customWaveformSize - 1);
                const auto index = static_cast<std::size_t>(position);
                const auto next = std::min(index + 1, customWaveformSize - 1);
                const auto fraction = position - static_cast<float>(index);
                const auto a = std::isfinite((*custom)[index])
                    ? std::clamp((*custom)[index], -1.0f, 1.0f) : 0.0f;
                const auto b = std::isfinite((*custom)[next])
                    ? std::clamp((*custom)[next], -1.0f, 1.0f) : 0.0f;
                return a + (b - a) * fraction;
            }
    }
    return 0.0f;
}

float Lfo::nextSample(LfoWaveform waveform, float phaseOffset,
                      const CustomWaveform* custom) noexcept
{
    const auto offset = std::isfinite(phaseOffset) ? phaseOffset : 0.0f;
    const auto output = evaluate(waveform,
        static_cast<float>(phase) + offset, heldValue, custom);
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

Lfo::CustomWaveform Lfo::makeDefaultCustomWaveform() noexcept
{
    CustomWaveform result {};
    constexpr auto uniquePoints = customWaveformSize - 1;
    for (std::size_t i = 0; i < uniquePoints; ++i)
        result[i] = std::sin(6.2831853071795864769f * static_cast<float>(i)
                             / static_cast<float>(uniquePoints));
    result.back() = result.front();
    return result;
}

void Lfo::prepareCustomWaveform(const CustomWaveform& source, float smooth,
                                CustomWaveform& destination) noexcept
{
    constexpr auto uniquePoints = customWaveformSize - 1;
    std::array<float, uniquePoints> current {};
    std::array<float, uniquePoints> next {};
    for (std::size_t i = 0; i < uniquePoints; ++i)
        current[i] = std::isfinite(source[i])
            ? std::clamp(source[i], -1.0f, 1.0f) : 0.0f;
    const auto amount = std::isfinite(smooth) ? std::clamp(smooth, 0.0f, 1.0f) : 0.0f;
    const auto passes = static_cast<int>(std::lround(amount * 24.0f));
    for (int pass = 0; pass < passes; ++pass)
    {
        for (std::size_t i = 0; i < uniquePoints; ++i)
        {
            const auto previous = (i + uniquePoints - 1) % uniquePoints;
            const auto following = (i + 1) % uniquePoints;
            next[i] = (current[previous] + 2.0f * current[i] + current[following]) * 0.25f;
        }
        current = next;
    }
    for (std::size_t i = 0; i < uniquePoints; ++i)
        destination[i] = current[i];
    destination.back() = destination.front();
}

void Lfo::drawCustomSegment(CustomWaveform& waveform, float x0, float y0,
                            float x1, float y1) noexcept
{
    constexpr auto seamIndex = customWaveformSize - 1;
    constexpr auto lastUniqueIndex = seamIndex - 1;
    const auto toIndex = [](float x) noexcept
    {
        const auto bounded = std::isfinite(x) ? std::clamp(x, 0.0f, 1.0f) : 0.0f;
        return std::min(static_cast<int>(lastUniqueIndex), static_cast<int>(std::lround(
            bounded * static_cast<float>(seamIndex))));
    };
    const auto first = toIndex(x0), last = toIndex(x1);
    const auto from = std::isfinite(y0) ? std::clamp(y0, -1.0f, 1.0f) : 0.0f;
    const auto to = std::isfinite(y1) ? std::clamp(y1, -1.0f, 1.0f) : 0.0f;
    if (first == last)
        waveform[static_cast<std::size_t>(first)] = to;
    else
        for (int i = std::min(first, last); i <= std::max(first, last); ++i)
        {
            const auto t = static_cast<float>(i - first) / static_cast<float>(last - first);
            waveform[static_cast<std::size_t>(i)] = from + (to - from) * t;
        }
    if (std::isfinite(x0) && std::clamp(x0, 0.0f, 1.0f) >= 1.0f)
        waveform[0] = from;
    if (std::isfinite(x1) && std::clamp(x1, 0.0f, 1.0f) >= 1.0f)
        waveform[0] = to;
    waveform[seamIndex] = waveform[0];
}

void Lfo::clearCustomWaveform(CustomWaveform& waveform) noexcept
{
    waveform.fill(0.0f);
}
}
