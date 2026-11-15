#pragma once

#include "dsp/granular/GrainPool.h"

#include <cstdint>
#include <span>

namespace hypercolor::dsp::granular
{
enum class GrainRendererResult : std::uint8_t
{
    success,
    degraded,
    invalidConfiguration
};

class GrainRenderer final
{
public:
    GrainRenderer() = default;
    ~GrainRenderer() = default;

    // Renders a wet-only mono/stereo frame. On invalid configuration the
    // output span and GrainPool are left unchanged. Individual failed grains
    // are marked finished, while valid grains continue to render.
    [[nodiscard]] GrainRendererResult renderSample(
        const CircularAudioBuffer& source,
        GrainPool& pool,
        std::span<float> wetOutputFrame
    ) const noexcept;
};
}
