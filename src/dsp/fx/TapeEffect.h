#pragma once

#include "core/ProcessSpec.h"
#include "dsp/fx/EffectModule.h"

#include <array>
#include <cstdint>
#include <vector>

namespace hypercolor::dsp::fx
{
struct TapeParameters
{
    float drive = 0.35f;
    float wow = 0.15f;
    float flutter = 0.10f;
    float wear = 0.15f;
    float mix = 1.0f;
    float outputDb = 0.0f;
};

class TapeEffect final : public EffectModule
{
public:
    [[nodiscard]] bool prepare(const core::ProcessSpec&) noexcept override;
    void reset() noexcept override;
    void process(EffectAudioBlock&) noexcept override;
    void setParameters(const TapeParameters&) noexcept;

private:
    [[nodiscard]] static TapeParameters sanitise(const TapeParameters&) noexcept;
    [[nodiscard]] float readFractional(std::size_t, double) const noexcept;
    static constexpr double maximumDelaySeconds = 0.02;
    static constexpr float smoothing = 0.001f;
    std::array<std::vector<float>, 2> buffer;
    std::array<float, 2> wearState {};
    std::size_t writeIndex = 0;
    std::size_t capacity = 0;
    double sampleRate = 0.0;
    double wowPhase = 0.0;
    double flutterPhase = 0.0;
    std::uint32_t numChannels = 0;
    TapeParameters current {}, target {};
    bool prepared = false;
};
}
