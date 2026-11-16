#pragma once

#include "core/ProcessSpec.h"
#include "dsp/fx/EffectModule.h"

#include <array>
#include <cstdint>

namespace hypercolor::dsp::fx
{
struct CompressorParameters
{
    float thresholdDb = -18.0f;
    float ratio = 4.0f;
    float attackMs = 10.0f;
    float releaseMs = 100.0f;
    float kneeDb = 6.0f;
    float makeupDb = 0.0f;
    float mix = 1.0f;
    float stereoLink = 1.0f;
};

class CompressorEffect final : public EffectModule
{
public:
    [[nodiscard]] bool prepare(const core::ProcessSpec&) noexcept override;
    void reset() noexcept override;
    void process(EffectAudioBlock&) noexcept override;
    void setParameters(const CompressorParameters&) noexcept;

private:
    [[nodiscard]] static CompressorParameters sanitise(const CompressorParameters&) noexcept;
    [[nodiscard]] static float gainReductionDb(float, const CompressorParameters&) noexcept;
    static constexpr float smoothing = 0.001f;
    double sampleRate = 0.0;
    std::uint32_t numChannels = 0;
    std::array<float, 2> envelopeDb {};
    CompressorParameters current {}, target {};
    bool prepared = false;
};
}
