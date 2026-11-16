#include "dsp/granular/GranularCharacter.h"

namespace hypercolor::dsp::granular
{
namespace
{
constexpr GranularCharacterProfile controlledChaosProfile {
    0.90f,
    0.90f,
    0.85f,
    0.90f,
    0.85f
};

constexpr GranularCharacterProfile musicalProfile {
    0.35f,
    0.35f,
    0.15f,
    0.40f,
    0.25f
};

constexpr GranularCharacterProfile atmosphericProfile {
    0.20f,
    0.45f,
    0.05f,
    0.90f,
    0.25f
};
}

const GranularCharacterProfile& getGranularCharacterProfile(
    GranularCharacter character
) noexcept
{
    switch (character)
    {
        case GranularCharacter::ControlledChaos:
            return controlledChaosProfile;
        case GranularCharacter::Musical:
            return musicalProfile;
        case GranularCharacter::Atmospheric:
            return atmosphericProfile;
        default:
            return musicalProfile;
    }
}
}
