#pragma once

#include "dsp/granular/GranularEngine.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace hypercolor::dsp::granular
{
inline constexpr std::size_t granularTelemetryWaveformCapacity = 256;
inline constexpr std::size_t granularTelemetryGrainCapacity = 64;
inline constexpr std::size_t granularTelemetryQueueCapacity = 8;

static_assert(
    (granularTelemetryQueueCapacity
        & (granularTelemetryQueueCapacity - 1)) == 0,
    "The queue capacity must divide the uint32 counter range for wraparound"
);

struct WaveformTelemetryPoint
{
    std::array<float, 2> minimum {};
    std::array<float, 2> maximum {};
};

struct GrainTelemetry
{
    double readPosition = 0.0;
    double playbackRate = 0.0;
    std::uint64_t sourceOldest = 0;
    std::uint64_t sourceNewestExclusive = 0;
    std::uint32_t durationFrames = 0;
    std::uint32_t renderedFrames = 0;
};

struct GranularTelemetrySnapshot
{
    std::uint64_t sequence = 0;
    std::uint64_t generation = 0;
    std::uint64_t logicalOldest = 0;
    std::uint64_t logicalNewestExclusive = 0;
    std::uint64_t waveformOldest = 0;
    std::uint64_t waveformNewestExclusive = 0;
    double sampleRate = 0.0;
    double logicalDurationSeconds = 0.0;
    double validDurationSeconds = 0.0;
    std::uint32_t numChannels = 0;
    std::uint32_t waveformPointCount = 0;
    std::uint32_t activeGrainCount = 0;
    GranularEngineParameters appliedParameters {};
    std::array<WaveformTelemetryPoint, granularTelemetryWaveformCapacity>
        waveformPoints {};
    std::array<GrainTelemetry, granularTelemetryGrainCapacity> grains {};
};

static_assert(std::is_trivially_copyable_v<WaveformTelemetryPoint>);
static_assert(std::is_standard_layout_v<WaveformTelemetryPoint>);
static_assert(std::is_trivially_copyable_v<GrainTelemetry>);
static_assert(std::is_standard_layout_v<GrainTelemetry>);
static_assert(std::is_trivially_copyable_v<GranularTelemetrySnapshot>);
static_assert(std::is_standard_layout_v<GranularTelemetrySnapshot>);
static_assert(
    std::tuple_size_v<decltype(GranularTelemetrySnapshot::waveformPoints)>
    == granularTelemetryWaveformCapacity
);
static_assert(
    std::tuple_size_v<decltype(GranularTelemetrySnapshot::grains)>
    == granularTelemetryGrainCapacity
);
static_assert(std::atomic<std::uint32_t>::is_always_lock_free);

// Single-producer/single-consumer queue. The producer exclusively writes the
// slot at writeIndex and publishes it with release. The consumer acquires that
// publication before copying the payload, then releases the slot by advancing
// readIndex. reset() is intentionally omitted because resetting indices while
// either endpoint is active would violate the ownership protocol. Unsigned
// index subtraction remains valid across uint32 wraparound because occupancy
// is bounded by eight and the power-of-two capacity divides 2^32.
class GranularTelemetryQueue final
{
public:
    [[nodiscard]] bool tryPush(
        const GranularTelemetrySnapshot& snapshot
    ) noexcept
    {
        const auto write = writeIndex.load(std::memory_order_relaxed);
        const auto read = readIndex.load(std::memory_order_acquire);

        if (write - read >= granularTelemetryQueueCapacity)
            return false;

        snapshots[write % granularTelemetryQueueCapacity] = snapshot;
        writeIndex.store(write + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool tryPop(GranularTelemetrySnapshot& snapshot) noexcept
    {
        const auto read = readIndex.load(std::memory_order_relaxed);
        const auto write = writeIndex.load(std::memory_order_acquire);

        if (read == write)
            return false;

        snapshot = snapshots[read % granularTelemetryQueueCapacity];
        readIndex.store(read + 1, std::memory_order_release);
        return true;
    }

private:
    std::array<GranularTelemetrySnapshot, granularTelemetryQueueCapacity>
        snapshots {};
    alignas(64) std::atomic<std::uint32_t> writeIndex { 0 };
    alignas(64) std::atomic<std::uint32_t> readIndex { 0 };
};
}
