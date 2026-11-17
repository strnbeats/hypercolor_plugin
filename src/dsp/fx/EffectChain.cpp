#include "dsp/fx/EffectChain.h"

#include <algorithm>
#include <cmath>

namespace hypercolor::dsp::fx
{
bool EffectChain::prepare(const core::ProcessSpec& spec) noexcept
{
    prepared = false;
    preparedBlockSize = 0;

    if (!std::isfinite(spec.sampleRate)
        || spec.sampleRate <= 0.0
        || spec.maximumBlockSize == 0
        || (spec.numChannels != 1 && spec.numChannels != 2))
    {
        return false;
    }

    try
    {
        for (auto& dryBuffer : dryBuffers)
            dryBuffer.assign(spec.maximumBlockSize, 0.0f);
    }
    catch (...)
    {
        for (auto& dryBuffer : dryBuffers)
            dryBuffer.clear();
        return false;
    }

    for (std::size_t index = 0; index < moduleCount; ++index)
    {
        if (modules[index] == nullptr || !modules[index]->prepare(spec))
        {
            reset();
            return false;
        }
    }

    preparedBlockSize = spec.maximumBlockSize;
    bypassTransitionSamples = std::max<std::size_t>(
        1,
        static_cast<std::size_t>(std::llround(
            spec.sampleRate * bypassTransitionSeconds
        ))
    );

    for (std::size_t index = 0; index < moduleCount; ++index)
    {
        const auto target = modules[index]->isBypassed() ? 0.0f : 1.0f;
        enabledGains[index] = target;
        targetEnabledGains[index] = target;
        remainingRampSamples[index] = 0;
    }

    prepared = true;
    return true;
}

void EffectChain::reset() noexcept
{
    for (std::size_t index = 0; index < moduleCount; ++index)
    {
        if (modules[index] != nullptr)
            modules[index]->reset();

        const auto target = modules[index] != nullptr && modules[index]->isBypassed()
            ? 0.0f
            : 1.0f;
        enabledGains[index] = target;
        targetEnabledGains[index] = target;
        remainingRampSamples[index] = 0;
    }
}

bool EffectChain::addModule(EffectModule* module) noexcept
{
    if (module == nullptr || moduleCount >= maximumModules || prepared)
        return false;

    modules[moduleCount++] = module;
    return true;
}

std::size_t EffectChain::getModuleCount() const noexcept
{
    return moduleCount;
}

bool EffectChain::isPrepared() const noexcept
{
    return prepared;
}

EffectChainResult EffectChain::process(EffectAudioBlock& block) const noexcept
{
    if (!prepared
        || (block.numChannels != 1 && block.numChannels != 2))
    {
        return EffectChainResult::invalidConfiguration;
    }

    const auto blockSize = block.channels[0].size();

    if (blockSize > preparedBlockSize)
        return EffectChainResult::invalidConfiguration;

    for (std::uint32_t channel = 0; channel < block.numChannels; ++channel)
    {
        if (block.channels[channel].size() != blockSize
            || (blockSize > 0 && block.channels[channel].data() == nullptr))
        {
            return EffectChainResult::invalidConfiguration;
        }
    }

    for (std::size_t index = 0; index < moduleCount; ++index)
    {
        auto* module = modules[index];

        if (module == nullptr)
            continue;

        const auto target = module->isBypassed() ? 0.0f : 1.0f;
        if (std::abs(target - targetEnabledGains[index]) > 0.5f)
        {
            targetEnabledGains[index] = target;
            remainingRampSamples[index] = bypassTransitionSamples;
        }

        for (std::uint32_t channel = 0; channel < block.numChannels; ++channel)
            std::copy(
                block.channels[channel].begin(),
                block.channels[channel].end(),
                dryBuffers[channel].begin()
            );

        // Always process the module so feedback, delay lines and filter state
        // continue evolving while its audible contribution is bypassed.
        module->process(block);

        for (std::size_t sample = 0; sample < blockSize; ++sample)
        {
            auto& gain = enabledGains[index];
            const auto remaining = remainingRampSamples[index];
            if (remaining > 0)
            {
                gain += (targetEnabledGains[index] - gain)
                    / static_cast<float>(remaining);
                --remainingRampSamples[index];
            }

            for (std::uint32_t channel = 0; channel < block.numChannels; ++channel)
            {
                const auto dry = dryBuffers[channel][sample];
                const auto wet = block.channels[channel][sample];
                block.channels[channel][sample] = dry + gain * (wet - dry);
            }
        }
    }

    return EffectChainResult::success;
}
}
