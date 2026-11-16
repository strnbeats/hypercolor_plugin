#pragma once

#include "core/ProcessSpec.h"

#include <array>
#include <cstdint>
#include <span>

namespace hypercolor::dsp::fx
{
struct EffectAudioBlock
{
    std::array<std::span<float>, 2> channels;
    std::uint32_t numChannels = 0;
};

class EffectModule
{
public:
    // The owner must keep each module alive and at the same address from
    // addModule() until the chain is no longer used by the audio thread.
    // EffectModule does not own the object and the chain composition is
    // immutable after prepare().
    virtual ~EffectModule() = default;

    [[nodiscard]] virtual bool prepare(
        const core::ProcessSpec& spec
    ) noexcept = 0;
    virtual void reset() noexcept = 0;
    virtual void process(EffectAudioBlock& block) noexcept = 0;

    void setBypassed(bool shouldBypass) noexcept { bypassed = shouldBypass; }
    [[nodiscard]] bool isBypassed() const noexcept { return bypassed; }

private:
    bool bypassed = false;
};
}
