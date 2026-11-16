#pragma once

#include "core/ProcessSpec.h"
#include "dsp/fx/EffectModule.h"

#include <array>
#include <cstdint>
#include <vector>

namespace hypercolor::dsp::fx
{
enum class DelayMode : std::uint8_t { stereo, pingPong };
enum class DelayDivision : std::uint8_t
{
    whole, half, quarter, eighth, sixteenth,
    dottedQuarter, dottedEighth, tripletQuarter, tripletEighth
};

struct DelayParameters
{
    DelayMode mode = DelayMode::stereo;
    bool sync = false;
    float timeMilliseconds = 350.0f;
    DelayDivision division = DelayDivision::quarter;
    float feedback = 0.35f;
    float lowCutHz = 100.0f;
    float highCutHz = 12000.0f;
    float mix = 0.3f;
    float width = 1.0f;
    float bpm = 120.0f;
};

class DelayEffect final : public EffectModule
{
public:
    [[nodiscard]] bool prepare(const core::ProcessSpec&) noexcept override;
    void reset() noexcept override;
    void process(EffectAudioBlock&) noexcept override;
    void setParameters(const DelayParameters&) noexcept;

private:
    [[nodiscard]] static DelayParameters sanitise(const DelayParameters&) noexcept;
    [[nodiscard]] float readFractional(std::size_t, double) const noexcept;
    [[nodiscard]] static double divisionMultiplier(DelayDivision) noexcept;

    static constexpr double maximumDelaySeconds = 2.0;
    static constexpr float smoothing = 0.001f;
    std::array<std::vector<float>, 2> buffer;
    std::array<float, 2> lowState {};
    std::array<float, 2> highState {};
    std::array<float, 2> previousInput {};
    std::size_t writeIndex = 0;
    std::size_t capacity = 0;
    double sampleRate = 0.0;
    std::uint32_t numChannels = 0;
    DelayParameters current {}, target {};
    bool prepared = false;
};
}
