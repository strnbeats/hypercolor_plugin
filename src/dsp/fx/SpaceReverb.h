#pragma once

#include "core/ProcessSpec.h"
#include "dsp/fx/EffectModule.h"

#include <array>
#include <cstddef>
#include <vector>

namespace hypercolor::dsp::fx
{
struct SpaceReverbParameters
{
    float mix = 0.30f;
    float size = 0.50f;
    float decaySeconds = 4.0f;
    float damping = 0.40f;
    float preDelayMilliseconds = 20.0f;
    float width = 1.0f;
};

class SpaceReverb final : public EffectModule
{
public:
    static constexpr std::size_t delayCount = 8;
    SpaceReverb() = default;
    ~SpaceReverb() override = default;

    [[nodiscard]] bool prepare(const core::ProcessSpec& spec) noexcept override;
    void reset() noexcept override;
    void process(EffectAudioBlock& block) noexcept override;

    void setParameters(const SpaceReverbParameters& parameters) noexcept;
    [[nodiscard]] const SpaceReverbParameters& getParameters() const noexcept;

private:
    static constexpr double maximumDelaySeconds = 0.12;
    static constexpr double maximumPreDelaySeconds = 0.2;

    [[nodiscard]] static SpaceReverbParameters sanitise(
        const SpaceReverbParameters& parameters
    ) noexcept;
    [[nodiscard]] float readDelay(
        std::size_t line,
        std::size_t delaySamples
    ) const noexcept;

    std::array<std::vector<float>, delayCount> delays;
    std::array<float, delayCount> dampingState {};
    std::array<std::size_t, delayCount> delayLengths {};
    std::array<std::vector<float>, 2> preDelay;
    std::size_t delayWriteIndex = 0;
    std::size_t preDelayWriteIndex = 0;
    std::size_t maximumDelaySamples = 0;
    std::size_t maximumPreDelaySamples = 0;
    double sampleRate = 0.0;
    std::uint32_t numChannels = 0;
    SpaceReverbParameters parameters {};
    SpaceReverbParameters targetParameters {};
    bool prepared = false;
};
}
