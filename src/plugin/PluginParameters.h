#pragma once

namespace hypercolor::ParameterIDs
{
// These IDs are part of the host automation and state compatibility contract.
// Changing them requires an explicit state/automation migration.
inline constexpr auto dryWet = "master.dryWet";
inline constexpr auto outputGain = "master.outputGain";
}
