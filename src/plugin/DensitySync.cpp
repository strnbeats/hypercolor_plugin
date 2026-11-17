#include "DensitySync.h"

#include <algorithm>
#include <cmath>

namespace hypercolor::density
{
namespace
{
[[nodiscard]] double clampFinite(
    double value,
    double lower,
    double upper
) noexcept
{
    if (!std::isfinite(value))
        return lower;

    return std::clamp(value, lower, upper);
}
}

double getDivisionMultiplier(Division division) noexcept
{
    switch (division)
    {
        case Division::quarter:               return 1.0;
        case Division::quarterTriplet:        return 1.5;
        case Division::eighth:                return 2.0;
        case Division::eighthTriplet:         return 3.0;
        case Division::sixteenth:             return 4.0;
        case Division::sixteenthTriplet:      return 6.0;
        case Division::thirtySecond:          return 8.0;
        case Division::thirtySecondTriplet:   return 12.0;
        case Division::sixtyFourth:           return 16.0;
        case Division::sixtyFourthTriplet:    return 24.0;
    }

    return 1.0;
}

double bpmToGrainsPerSecond(double bpm, Division division) noexcept
{
    if (!std::isfinite(bpm) || bpm <= 0.0)
        return minimumGrainsPerSecond;

    const auto constrainedBpm = std::clamp(bpm, minimumBpm, maximumBpm);
    const auto multiplier = getDivisionMultiplier(division);
    const auto grainsPerSecond = (constrainedBpm / 60.0) * multiplier;

    // This second clamp is intentionally defensive: it keeps this contract
    // safe if the division table is extended in a future phase.
    return clampFinite(
        grainsPerSecond,
        minimumGrainsPerSecond,
        maximumGrainsPerSecond
    );
}
}
