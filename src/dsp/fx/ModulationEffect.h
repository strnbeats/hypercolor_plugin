#pragma once

#include "core/ProcessSpec.h"
#include "dsp/fx/EffectModule.h"

#include <array>
#include <vector>

namespace hypercolor::dsp::fx
{
enum class ModulationMode : std::uint8_t { chorus, flanger };

struct ModulationParameters
{
    ModulationMode mode = ModulationMode::chorus;
    float rateHz = 0.3f;
    float depth = 0.5f;
    float baseDelayMilliseconds = 7.0f;
    float feedback = 0.0f;
    float mix = 0.3f;
    float width = 1.0f;
};

class ModulationEffect final : public EffectModule
{
public:
    [[nodiscard]] bool prepare(const core::ProcessSpec& spec) noexcept override;
    void reset() noexcept override;
    void process(EffectAudioBlock& block) noexcept override;

    void setParameters(const ModulationParameters& parameters) noexcept;

private:
    static constexpr double maximumDelaySeconds = 0.04;
    static constexpr float smoothing = 0.001f;
    [[nodiscard]] static ModulationParameters sanitise(
        const ModulationParameters& input
    ) noexcept;
    [[nodiscard]] float readFractional(
        std::size_t channel,
        double delaySamples
    ) const noexcept;

    std::array<std::vector<float>, 2> buffer;
    std::size_t writeIndex = 0;
    std::size_t capacity = 0;
    double sampleRate = 0.0;
    std::uint32_t numChannels = 0;
    double phase = 0.0;
    ModulationParameters current {};
    ModulationParameters target {};
    float currentModeBlend = 0.0f;
    float targetModeBlend = 0.0f;
    bool prepared = false;
};
}
