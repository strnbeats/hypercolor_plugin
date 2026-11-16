#pragma once

namespace hypercolor::ParameterIDs
{
// These IDs are part of the host automation and state compatibility contract.
// Changing them requires an explicit state/automation migration.
inline constexpr auto dryWet = "master.dryWet";
inline constexpr auto outputGain = "master.outputGain";
inline constexpr auto grainDensity = "grainDensity";
inline constexpr auto grainSize = "grainSize";
inline constexpr auto grainPosition = "grainPosition";
inline constexpr auto grainPitch = "grainPitch";
inline constexpr auto grainReverse = "grainReverse";
inline constexpr auto grainCharacter = "grainCharacter";
inline constexpr auto grainPitchRandom = "grainPitchRandom";
inline constexpr auto grainPositionRandom = "grainPositionRandom";
}
