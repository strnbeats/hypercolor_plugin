#pragma once

#include "core/ProcessSpec.h"
#include "dsp/fx/EffectModule.h"

#include <array>
#include <cstdint>

namespace hypercolor::dsp::fx
{
enum class DriveMode : std::uint8_t
{
    softClip,
    hardClip,
    tube
};

struct DriveParameters
{
    DriveMode mode = DriveMode::softClip;
    float driveDb = 0.0f;
    float tone = 0.5f;
    float mix = 1.0f;
    float outputDb = 0.0f;
};

class DriveEffect final : public EffectModule
{
public:
    [[nodiscard]] bool prepare(const core::ProcessSpec& spec) noexcept override;
    void reset() noexcept override;
    void process(EffectAudioBlock& block) noexcept override;
    void setParameters(const DriveParameters& parameters) noexcept;

private:
    [[nodiscard]] static DriveParameters sanitise(const DriveParameters&) noexcept;
    [[nodiscard]] static float shape(float input, DriveMode mode) noexcept;

    static constexpr float smoothing = 0.001f;
    static constexpr float dcPole = 0.995f;
    double sampleRate = 0.0;
    std::uint32_t numChannels = 0;
    DriveParameters current {};
    DriveParameters target {};
    std::array<float, 2> toneState {};
    std::array<float, 2> dcInputState {};
    std::array<float, 2> dcOutputState {};
    bool prepared = false;
};
}
