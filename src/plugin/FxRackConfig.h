#pragma once

#include <array>
#include <cstdint>
#include <JuceHeader.h>

namespace hypercolor::plugin
{
enum class FxType : std::uint8_t
{
    space, modulation, drive, tape, bitcrush, filter, dynamics, delay
};

struct FxInstanceId
{
    FxType type = FxType::space;
    std::uint8_t slot = 0;

    friend constexpr bool operator==(FxInstanceId, FxInstanceId) = default;
};

inline constexpr std::size_t maximumFxInstancesPerType = 3;
inline constexpr std::size_t maximumFxChainModules = 12;
inline constexpr std::size_t fxTypeCount = 8;

struct FxChainConfiguration
{
    std::array<FxInstanceId, maximumFxChainModules> order {};
    std::uint8_t count = 0;
};

constexpr std::uint8_t toIndex(FxType type) noexcept
{
    return static_cast<std::uint8_t>(type);
}

inline constexpr std::array<const char*, fxTypeCount> fxTypeNames {
    "space", "modulation", "drive", "tape", "bitcrush", "filter",
    "dynamics", "delay"
};

inline juce::String fxParameterID(
    FxType type, std::uint8_t slot, const char* legacyID
)
{
    if (slot == 0)
        return legacyID;
    return "fx." + juce::String(fxTypeNames[toIndex(type)]) + "."
        + juce::String(static_cast<int>(slot + 1)) + "." + legacyID;
}

inline juce::String fxEnabledParameterID(FxType type, std::uint8_t slot,
                                         const char* legacyID)
{
    return slot == 0 ? juce::String(legacyID)
        : "fx." + juce::String(fxTypeNames[toIndex(type)]) + "."
            + juce::String(static_cast<int>(slot + 1)) + ".enabled";
}
}
