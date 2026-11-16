#pragma once

#include "dsp/fx/EffectModule.h"

#include <array>
#include <cstddef>

namespace hypercolor::dsp::fx
{
enum class EffectChainResult : std::uint8_t
{
    success,
    invalidConfiguration
};

class EffectChain final
{
public:
    static constexpr std::size_t maximumModules = 8;

    [[nodiscard]] bool prepare(const core::ProcessSpec& spec) noexcept;
    void reset() noexcept;

    [[nodiscard]] bool addModule(EffectModule* module) noexcept;
    [[nodiscard]] std::size_t getModuleCount() const noexcept;
    [[nodiscard]] bool isPrepared() const noexcept;

    [[nodiscard]] EffectChainResult process(
        EffectAudioBlock& block
    ) const noexcept;

private:
    std::array<EffectModule*, maximumModules> modules {};
    std::size_t moduleCount = 0;
    bool prepared = false;
};
}
