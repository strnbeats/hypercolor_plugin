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
inline constexpr auto grainTimingJitter = "grainTimingJitter";
inline constexpr auto grainStereoSpread = "grainStereoSpread";
inline constexpr auto grainPanRandom = "grainPanRandom";
inline constexpr auto grainEnvelope = "grainEnvelope";
inline constexpr auto reverbMix = "reverbMix";
inline constexpr auto reverbSize = "reverbSize";
inline constexpr auto reverbDecay = "reverbDecay";
inline constexpr auto reverbDamping = "reverbDamping";
inline constexpr auto reverbPreDelay = "reverbPreDelay";
inline constexpr auto reverbWidth = "reverbWidth";
inline constexpr auto modulationMode = "modulationMode";
inline constexpr auto modulationRate = "modulationRate";
inline constexpr auto modulationDepth = "modulationDepth";
inline constexpr auto modulationBaseDelay = "modulationBaseDelay";
inline constexpr auto modulationFeedback = "modulationFeedback";
inline constexpr auto modulationMix = "modulationMix";
inline constexpr auto modulationWidth = "modulationWidth";
}
