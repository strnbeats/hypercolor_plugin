#pragma once

#include "core/ProcessSpec.h"
#include "dsp/fx/EffectModule.h"

#include <array>
#include <cstdint>

namespace hypercolor::dsp::fx
{
enum class BitcrushMode : std::uint8_t { round, truncate };

struct BitcrushParameters
{
    float bits = 16.0f;
    float rateHz = 22050.0f;
    BitcrushMode mode = BitcrushMode::round;
    float mix = 1.0f;
    float outputDb = 0.0f;
};

class BitcrushEffect final : public EffectModule
{
public:
    [[nodiscard]] bool prepare(const core::ProcessSpec&) noexcept override;
    void reset() noexcept override;
    void process(EffectAudioBlock&) noexcept override;
    void setParameters(const BitcrushParameters&) noexcept;

private:
    [[nodiscard]] static BitcrushParameters sanitise(const BitcrushParameters&) noexcept;
    [[nodiscard]] static float quantise(float, float, BitcrushMode) noexcept;
    static constexpr float smoothing = 0.001f;
    double sampleRate = 0.0;
    double phase = 0.0;
    std::uint32_t numChannels = 0;
    std::array<float, 2> held {};
    BitcrushParameters current {}, target {};
    bool prepared = false;
};
}
