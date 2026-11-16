#pragma once

#include <cstdint>

namespace hypercolor::dsp::granular
{
enum class GranularCharacter : std::uint8_t
{
    // Numeric values are persisted by the APVTS Choice parameter. Append new
    // characters rather than reordering these entries.
    ControlledChaos = 0,
    Musical = 1,
    Atmospheric = 2
};

struct GranularCharacterProfile
{
    float pitchRandomScale = 0.0f;
    float positionRandomScale = 0.0f;
    float timingJitterScale = 0.0f;
    float stereoSpreadScale = 0.0f;
    float panRandomScale = 0.0f;
};

// Unknown values fall back to Musical, the least extreme general-purpose
// profile. The returned value is immutable from the caller's perspective.
[[nodiscard]] const GranularCharacterProfile& getGranularCharacterProfile(
    GranularCharacter character
) noexcept;
}
