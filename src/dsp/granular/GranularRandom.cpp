#include "dsp/granular/GranularRandom.h"

namespace hypercolor::dsp::granular
{
GranularRandom::GranularRandom(std::uint64_t seed) noexcept
{
    reset(seed);
}

void GranularRandom::reset(std::uint64_t seed) noexcept
{
    state = 0;
    static_cast<void>(nextUInt32());
    state += seed;
    static_cast<void>(nextUInt32());
}

std::uint32_t GranularRandom::nextUInt32() noexcept
{
    const auto previousState = state;
    state = previousState * multiplier + increment;

    const auto xorshifted = static_cast<std::uint32_t>(
        ((previousState >> 18U) ^ previousState) >> 27U
    );
    const auto rotation = static_cast<std::uint32_t>(previousState >> 59U);

    return (xorshifted >> rotation)
        | (xorshifted << ((0U - rotation) & 31U));
}

double GranularRandom::nextNormalized() noexcept
{
    constexpr double inverseUInt32Range = 0x1.0p-32;
    return static_cast<double>(nextUInt32()) * inverseUInt32Range;
}

double GranularRandom::nextSignedNormalized() noexcept
{
    return nextNormalized() * 2.0 - 1.0;
}
}
