#pragma once

#include "core/ProcessSpec.h"

#include <array>
#include <atomic>
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
    // EffectModule does not own the object. Registration is immutable after
    // prepare(); EffectChain may change its active order between blocks.
    virtual ~EffectModule() = default;

    [[nodiscard]] virtual bool prepare(
        const core::ProcessSpec& spec
    ) noexcept = 0;
    virtual void reset() noexcept = 0;
    virtual void process(EffectAudioBlock& block) noexcept = 0;

    // The flag is intentionally atomic so the legacy module-level API remains
    // race-free while the chain samples it once per audio block.  The actual
    // transition is performed by EffectChain on the audio thread.
    void setBypassed(bool shouldBypass) noexcept
    {
        bypassed.store(shouldBypass, std::memory_order_relaxed);
    }

    [[nodiscard]] bool isBypassed() const noexcept
    {
        return bypassed.load(std::memory_order_relaxed);
    }

private:
    std::atomic<bool> bypassed { false };
};
}
