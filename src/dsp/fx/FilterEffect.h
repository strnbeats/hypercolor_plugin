#pragma once

#include "core/ProcessSpec.h"
#include "dsp/fx/EffectModule.h"

#include <array>
#include <cstdint>

namespace hypercolor::dsp::fx
{
enum class FilterMode : std::uint8_t { lowPass, highPass, bandPass, notch };

struct FilterParameters
{
    FilterMode mode = FilterMode::lowPass;
    float cutoffHz = 10000.0f;
    float resonance = 0.2f;
    float drive = 0.0f;
    float mix = 1.0f;
    float outputDb = 0.0f;
};

class FilterEffect final : public EffectModule
{
public:
    [[nodiscard]] bool prepare(const core::ProcessSpec&) noexcept override;
    void reset() noexcept override;
    void process(EffectAudioBlock&) noexcept override;
    void setParameters(const FilterParameters&) noexcept;

private:
    [[nodiscard]] static FilterParameters sanitise(const FilterParameters&) noexcept;
    static constexpr float smoothing = 0.001f;
    double sampleRate = 0.0;
    std::uint32_t numChannels = 0;
    std::array<float, 2> lowState {};
    std::array<float, 2> bandState {};
    FilterParameters current {}, target {};
    bool prepared = false;
};
}
