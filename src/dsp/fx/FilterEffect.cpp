#include "dsp/fx/FilterEffect.h"

#include <algorithm>
#include <cmath>

namespace hypercolor::dsp::fx
{
FilterParameters FilterEffect::sanitise(const FilterParameters& input) noexcept
{
    FilterParameters result;
    result.mode = input.mode == FilterMode::highPass ? FilterMode::highPass
        : (input.mode == FilterMode::bandPass ? FilterMode::bandPass
        : (input.mode == FilterMode::notch ? FilterMode::notch : FilterMode::lowPass));
    result.cutoffHz = std::isfinite(input.cutoffHz) ? std::clamp(input.cutoffHz, 20.0f, 20000.0f) : 10000.0f;
    result.resonance = std::isfinite(input.resonance) ? std::clamp(input.resonance, 0.0f, 1.0f) : 0.2f;
    result.drive = std::isfinite(input.drive) ? std::clamp(input.drive, 0.0f, 1.0f) : 0.0f;
    result.mix = std::isfinite(input.mix) ? std::clamp(input.mix, 0.0f, 1.0f) : 1.0f;
    result.outputDb = std::isfinite(input.outputDb) ? std::clamp(input.outputDb, -24.0f, 12.0f) : 0.0f;
    return result;
}

bool FilterEffect::prepare(const core::ProcessSpec& spec) noexcept
{
    prepared = false;
    if (!std::isfinite(spec.sampleRate) || spec.sampleRate <= 0.0
        || spec.maximumBlockSize == 0 || (spec.numChannels != 1 && spec.numChannels != 2))
        return false;
    sampleRate = spec.sampleRate;
    numChannels = spec.numChannels;
    reset();
    prepared = true;
    return true;
}

void FilterEffect::reset() noexcept
{
    lowState.fill(0.0f);
    bandState.fill(0.0f);
    current = sanitise(target);
}

void FilterEffect::setParameters(const FilterParameters& parameters) noexcept
{
    target = sanitise(parameters);
}

void FilterEffect::process(EffectAudioBlock& block) noexcept
{
    if (!prepared || block.numChannels != numChannels || block.channels[0].empty()
        || (numChannels == 2 && block.channels[1].size() != block.channels[0].size()))
        return;

    for (std::size_t sample = 0; sample < block.channels[0].size(); ++sample)
    {
        current.cutoffHz += smoothing * (target.cutoffHz - current.cutoffHz);
        current.resonance += smoothing * (target.resonance - current.resonance);
        current.drive += smoothing * (target.drive - current.drive);
        current.mix += smoothing * (target.mix - current.mix);
        current.outputDb += smoothing * (target.outputDb - current.outputDb);
        current.mode = target.mode;
        const auto cutoff = std::min(current.cutoffHz, 0.48f * static_cast<float>(sampleRate));
        const auto g = std::tan(3.14159265358979323846f * cutoff / static_cast<float>(sampleRate));
        const auto k = 1.95f - 1.85f * current.resonance;
        const auto outputGain = std::pow(10.0f, current.outputDb / 20.0f);
        const auto processChannel = [&](std::size_t channel) noexcept
        {
            const auto input = std::isfinite(block.channels[channel][sample]) ? block.channels[channel][sample] : 0.0f;
            const auto high = input - lowState[channel] - k * bandState[channel];
            const auto newBand = bandState[channel] + g * high;
            const auto newLow = lowState[channel] + g * newBand;
            bandState[channel] = newBand;
            lowState[channel] = newLow;
            const auto highOut = input - newLow - k * newBand;
            const auto notchOut = highOut + newLow;
            float filtered = newLow;
            if (current.mode == FilterMode::highPass) filtered = highOut;
            else if (current.mode == FilterMode::bandPass) filtered = newBand;
            else if (current.mode == FilterMode::notch) filtered = notchOut;
            const auto driven = std::tanh(filtered * (1.0f + 5.0f * current.drive));
            const auto effectiveMix = target.mix <= 0.0f ? 0.0f : current.mix;
            const auto output = input + effectiveMix * (driven * outputGain - input);
            block.channels[channel][sample] = std::isfinite(output) ? output : 0.0f;
        };
        processChannel(0);
        if (numChannels == 2) processChannel(1);
    }
}
}
