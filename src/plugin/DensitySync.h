#pragma once

#include <cstdint>

namespace hypercolor::density
{
// Musical density divisions supported by BPM Sync mode.
enum class Division : std::uint8_t
{
    quarter = 0,
    quarterTriplet,
    eighth,
    eighthTriplet,
    sixteenth,
    sixteenthTriplet,
    thirtySecond,
    thirtySecondTriplet,
    sixtyFourth,
    sixtyFourthTriplet
};

inline constexpr double minimumBpm = 20.0;
inline constexpr double maximumBpm = 300.0;
inline constexpr double minimumGrainsPerSecond = 0.0;
inline constexpr double maximumGrainsPerSecond = 120.0;

// Returns the grains-per-beat multiplier for a musical division. Unknown enum
// values use the quarter-note multiplier as a deterministic safe fallback.
[[nodiscard]] double getDivisionMultiplier(Division division) noexcept;

// Converts BPM and a musical division to grains per second. Invalid BPM
// values return zero; valid BPM values are constrained to [20, 300].
[[nodiscard]] double bpmToGrainsPerSecond(
    double bpm,
    Division division
) noexcept;
}
