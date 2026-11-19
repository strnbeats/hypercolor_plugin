#pragma once

#include <array>
#include <cmath>
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
    std::uint64_t stableIdentity = 0;

    friend constexpr bool operator==(FxInstanceId, FxInstanceId) = default;
};

inline constexpr std::size_t maximumFxInstancesPerType = 3;
inline constexpr std::size_t maximumFxChainModules = 12;
inline constexpr std::size_t fxTypeCount = 8;
inline constexpr std::size_t maximumFxMacroAssignments = 32;

struct FxChainConfiguration
{
    std::array<FxInstanceId, maximumFxChainModules> order {};
    std::uint8_t count = 0;
};

constexpr std::uint8_t toIndex(FxType type) noexcept
{
    return static_cast<std::uint8_t>(type);
}

constexpr std::uint64_t legacyFxIdentity(FxType type, std::uint8_t slot) noexcept
{
    return 1u + static_cast<std::uint64_t>(toIndex(type))
        * maximumFxInstancesPerType + slot;
}

enum class MacroAxis : std::uint8_t { x, y };

struct FxMacroAssignment
{
    MacroAxis axis = MacroAxis::x;
    std::uint64_t instanceIdentity = 0;
    juce::String parameterID;
    float minimum = 0.0f;
    float maximum = 1.0f;
};

struct FxMacroAssignmentSet
{
    std::array<FxMacroAssignment, maximumFxMacroAssignments> assignments {};
    std::uint8_t count = 0;
};

inline float mapMacroRange(float macroValue, float minimum, float maximum) noexcept
{
    if (!std::isfinite(macroValue) || !std::isfinite(minimum)
        || !std::isfinite(maximum))
        return 0.0f;
    const auto normalized = juce::jlimit(0.0f, 1.0f, macroValue);
    return minimum + normalized * (maximum - minimum);
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
