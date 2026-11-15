#pragma once

#include <cstdint>

namespace hypercolor::core
{
struct ProcessSpec
{
    double sampleRate = 0.0;
    std::uint32_t maximumBlockSize = 0;
    std::uint32_t numChannels = 0;
};
}
