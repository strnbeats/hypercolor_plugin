#pragma once

#include "dsp/fx/EffectModule.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

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
    static constexpr std::size_t maximumModules = 24;
    static constexpr std::size_t maximumActiveModules = maximumModules;

    [[nodiscard]] bool prepare(const core::ProcessSpec& spec) noexcept;
    void reset() noexcept;

    [[nodiscard]] bool addModule(EffectModule* module) noexcept;
    // Indices refer to stable registration order; publish only on audio thread.
    [[nodiscard]] bool setActiveOrder(
        std::span<const std::uint8_t> moduleIndices
    ) noexcept;
    [[nodiscard]] std::size_t getModuleCount() const noexcept;
    [[nodiscard]] bool isPrepared() const noexcept;

    [[nodiscard]] EffectChainResult process(
        EffectAudioBlock& block
    ) const noexcept;

private:
    static constexpr double bypassTransitionSeconds = 0.015;

    std::array<EffectModule*, maximumModules> modules {};
    // One reusable dry pair is sufficient: each module is copied, processed,
    // and mixed before the next module reuses the same scratch storage.
    mutable std::array<std::vector<float>, 2> dryBuffers;
    mutable std::array<float, maximumModules> enabledGains { 1.0f };
    mutable std::array<float, maximumModules> targetEnabledGains { 1.0f };
    mutable std::array<std::size_t, maximumModules> remainingRampSamples {};
    mutable std::array<std::uint8_t, maximumActiveModules> activeOrder {};
    std::array<std::uint8_t, maximumActiveModules> requestedOrder {};
    mutable std::size_t activeCount = 0;
    std::size_t requestedCount = 0;
    mutable float chainGain = 1.0f;
    mutable float chainGainTarget = 1.0f;
    mutable std::size_t chainRampRemaining = 0;
    mutable bool swapOrderWhenSilent = false;
    bool activeOrderWasSet = false;
    std::size_t moduleCount = 0;
    std::size_t preparedBlockSize = 0;
    std::size_t bypassTransitionSamples = 1;
    bool prepared = false;
};
}
