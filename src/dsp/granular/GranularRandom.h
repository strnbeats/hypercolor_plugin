#pragma once

#include <cstdint>

namespace hypercolor::dsp::granular
{
// Small single-owner PCG32 generator. Unsigned integer overflow is an
// intentional and defined part of the state transition.
class GranularRandom final
{
public:
    explicit GranularRandom(std::uint64_t seed = 0) noexcept;

    void reset(std::uint64_t seed) noexcept;

    [[nodiscard]] std::uint32_t nextUInt32() noexcept;
    [[nodiscard]] double nextNormalized() noexcept;
    [[nodiscard]] double nextSignedNormalized() noexcept;

private:
    static constexpr std::uint64_t multiplier = 6364136223846793005ULL;
    static constexpr std::uint64_t increment = 1442695040888963407ULL;

    std::uint64_t state = 0;
};
}
