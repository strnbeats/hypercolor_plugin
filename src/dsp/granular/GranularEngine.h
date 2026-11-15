#pragma once

#include "core/ProcessSpec.h"
#include "dsp/granular/CircularAudioBuffer.h"
#include "dsp/granular/GrainRenderer.h"
#include "dsp/granular/GrainScheduler.h"

#include <array>
#include <cstdint>
#include <span>

namespace hypercolor::dsp::granular
{
using GranularEngineParameters = GrainSchedulerParameters;

struct GranularAudioBlockView
{
    std::array<std::span<const float>, 2> inputChannels;
    std::array<std::span<float>, 2> outputChannels;
    std::uint32_t numChannels = 0;
};

enum class GranularEngineProcessResult : std::uint8_t
{
    success,
    degraded,
    engineNotPrepared,
    invalidConfiguration
};

class GranularEngine final
{
public:
    GranularEngine() = default;
    ~GranularEngine() = default;

    GranularEngine(const GranularEngine&) = delete;
    GranularEngine& operator=(const GranularEngine&) = delete;
    GranularEngine(GranularEngine&&) = delete;
    GranularEngine& operator=(GranularEngine&&) = delete;

    [[nodiscard]] bool prepare(const core::ProcessSpec& spec);
    void reset() noexcept;

    // Input and output may alias channel-by-channel for in-place processing.
    // Arbitrary cross-channel or partially overlapping spans are unsupported.
    [[nodiscard]] GranularEngineProcessResult processBlock(
        const GranularAudioBlockView& block,
        const GranularEngineParameters& parameters
    ) noexcept;

    [[nodiscard]] bool isPrepared() const noexcept;

private:
    [[nodiscard]] bool isBlockConfigurationValid(
        const GranularAudioBlockView& block
    ) const noexcept;

    CircularAudioBuffer circularBuffer;
    GrainScheduler scheduler;
    GrainPool grainPool;
    GrainRenderer renderer;
    bool prepared = false;
};
}
