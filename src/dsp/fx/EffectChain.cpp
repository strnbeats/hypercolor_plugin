#include "dsp/fx/EffectChain.h"

#include <cmath>

namespace hypercolor::dsp::fx
{
bool EffectChain::prepare(const core::ProcessSpec& spec) noexcept
{
    prepared = false;

    if (!std::isfinite(spec.sampleRate)
        || spec.sampleRate <= 0.0
        || spec.maximumBlockSize == 0
        || (spec.numChannels != 1 && spec.numChannels != 2))
    {
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

    prepared = true;
    return true;
}

void EffectChain::reset() noexcept
{
    for (std::size_t index = 0; index < moduleCount; ++index)
    {
        if (modules[index] != nullptr)
            modules[index]->reset();
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

EffectChainResult EffectChain::process(EffectAudioBlock& block) const noexcept
{
    if (!prepared
        || (block.numChannels != 1 && block.numChannels != 2))
    {
        return EffectChainResult::invalidConfiguration;
    }

    const auto blockSize = block.channels[0].size();

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

        if (module != nullptr && !module->isBypassed())
            module->process(block);
    }

    return EffectChainResult::success;
}
}
