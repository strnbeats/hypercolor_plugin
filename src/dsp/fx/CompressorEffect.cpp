#include "dsp/fx/CompressorEffect.h"

#include <algorithm>
#include <cmath>

namespace hypercolor::dsp::fx
{
CompressorParameters CompressorEffect::sanitise(const CompressorParameters& input) noexcept
{
    CompressorParameters result;
    result.thresholdDb = std::isfinite(input.thresholdDb) ? std::clamp(input.thresholdDb, -60.0f, 0.0f) : -18.0f;
    result.ratio = std::isfinite(input.ratio) ? std::clamp(input.ratio, 1.0f, 20.0f) : 4.0f;
    result.attackMs = std::isfinite(input.attackMs) ? std::clamp(input.attackMs, 0.1f, 100.0f) : 10.0f;
    result.releaseMs = std::isfinite(input.releaseMs) ? std::clamp(input.releaseMs, 10.0f, 1000.0f) : 100.0f;
    result.kneeDb = std::isfinite(input.kneeDb) ? std::clamp(input.kneeDb, 0.0f, 24.0f) : 6.0f;
    result.makeupDb = std::isfinite(input.makeupDb) ? std::clamp(input.makeupDb, 0.0f, 24.0f) : 0.0f;
    result.mix = std::isfinite(input.mix) ? std::clamp(input.mix, 0.0f, 1.0f) : 1.0f;
    result.stereoLink = std::isfinite(input.stereoLink) ? std::clamp(input.stereoLink, 0.0f, 1.0f) : 1.0f;
    return result;
}

bool CompressorEffect::prepare(const core::ProcessSpec& spec) noexcept
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

void CompressorEffect::reset() noexcept
{
    envelopeDb.fill(-120.0f);
    current = sanitise(target);
}

void CompressorEffect::setParameters(const CompressorParameters& parameters) noexcept
{
    target = sanitise(parameters);
}

float CompressorEffect::gainReductionDb(float levelDb, const CompressorParameters& p) noexcept
{
    if (p.ratio <= 1.0f) return 0.0f;
    const auto threshold = p.thresholdDb;
    if (p.kneeDb <= 0.0f)
        return levelDb > threshold ? (threshold + (levelDb - threshold) / p.ratio - levelDb) : 0.0f;
    const auto half = 0.5f * p.kneeDb;
    const auto distance = levelDb - threshold;
    if (distance <= -half) return 0.0f;
    if (distance >= half) return threshold + (levelDb - threshold) / p.ratio - levelDb;
    const auto compressed = distance + (1.0f / p.ratio - 1.0f) * (distance + half) * (distance + half) / (2.0f * p.kneeDb);
    return compressed - distance;
}

void CompressorEffect::process(EffectAudioBlock& block) noexcept
{
    if (!prepared || block.numChannels != numChannels || block.channels[0].empty()
        || (numChannels == 2 && block.channels[1].size() != block.channels[0].size()))
        return;
    for (std::size_t sample = 0; sample < block.channels[0].size(); ++sample)
    {
        current.thresholdDb += smoothing * (target.thresholdDb - current.thresholdDb);
        current.ratio += smoothing * (target.ratio - current.ratio);
        current.attackMs += smoothing * (target.attackMs - current.attackMs);
        current.releaseMs += smoothing * (target.releaseMs - current.releaseMs);
        current.kneeDb += smoothing * (target.kneeDb - current.kneeDb);
        current.makeupDb += smoothing * (target.makeupDb - current.makeupDb);
        current.mix += smoothing * (target.mix - current.mix);
        current.stereoLink += smoothing * (target.stereoLink - current.stereoLink);
        const auto left = std::isfinite(block.channels[0][sample]) ? block.channels[0][sample] : 0.0f;
        const auto right = numChannels == 2 && std::isfinite(block.channels[1][sample]) ? block.channels[1][sample] : left;
        const auto leftDb = 20.0f * std::log10(std::max(std::abs(left), 1.0e-12f));
        const auto rightDb = 20.0f * std::log10(std::max(std::abs(right), 1.0e-12f));
        const auto linkedDb = std::max(leftDb, rightDb);
        const auto detectorLeft = leftDb + current.stereoLink * (linkedDb - leftDb);
        const auto detectorRight = rightDb + current.stereoLink * (linkedDb - rightDb);
        const auto attackCoeff = std::exp(-1.0 / (0.001 * current.attackMs * sampleRate));
        const auto releaseCoeff = std::exp(-1.0 / (0.001 * current.releaseMs * sampleRate));
        const auto update = [&](std::size_t channel, float detector) noexcept
        {
            const auto coeff = detector > envelopeDb[channel] ? attackCoeff : releaseCoeff;
            envelopeDb[channel] = static_cast<float>(coeff * envelopeDb[channel] + (1.0 - coeff) * detector);
            const auto reduction = gainReductionDb(envelopeDb[channel], current);
            const auto gain = std::pow(10.0f, (reduction + current.makeupDb) / 20.0f);
            return gain;
        };
        const auto gainLeft = update(0, detectorLeft);
        const auto gainRight = numChannels == 2 ? update(1, detectorRight) : gainLeft;
        const auto effectiveMix = target.mix <= 0.0f ? 0.0f : current.mix;
        const auto outputLeft = left + effectiveMix * (left * gainLeft - left);
        const auto outputRight = right + effectiveMix * (right * gainRight - right);
        block.channels[0][sample] = std::isfinite(outputLeft) ? outputLeft : 0.0f;
        if (numChannels == 2) block.channels[1][sample] = std::isfinite(outputRight) ? outputRight : 0.0f;
    }
}
}
