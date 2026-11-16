#pragma once

#include "core/ProcessSpec.h"
#include "dsp/granular/CircularAudioBuffer.h"
#include "dsp/granular/GrainRenderer.h"
#include "dsp/granular/GrainScheduler.h"
#include "dsp/granular/GranularTelemetry.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace hypercolor::dsp::granular
{
struct GranularAudioBlockView
{
    std::array<std::span<const float>, 2> inputChannels;
    std::array<std::span<float>, 2> outputChannels;
    std::uint32_t numChannels = 0;
};

enum class GranularEngineProcessResult : std::uint8_t
{
    success,
    degraded,
    engineNotPrepared,
    invalidConfiguration
};

class GranularEngine final
{
public:
    GranularEngine() = default;
    ~GranularEngine() = default;

    GranularEngine(const GranularEngine&) = delete;
    GranularEngine& operator=(const GranularEngine&) = delete;
    GranularEngine(GranularEngine&&) = delete;
    GranularEngine& operator=(GranularEngine&&) = delete;

    [[nodiscard]] bool prepare(const core::ProcessSpec& spec);
    void reset() noexcept;

    // Audio-thread-owned configuration. Call at an audio block boundary.
    // Existing DSP history and captured SourceRegions remain valid; only the
    // waveform envelope is restarted with the new temporal resolution.
    [[nodiscard]] bool setLogicalDurationSeconds(
        double durationSeconds
    ) noexcept;

    // Input and output may alias channel-by-channel for in-place processing.
    // Arbitrary cross-channel or partially overlapping spans are unsupported.
    [[nodiscard]] GranularEngineProcessResult processBlock(
        const GranularAudioBlockView& block,
        const GranularEngineParameters& parameters
    ) noexcept;

    [[nodiscard]] bool isPrepared() const noexcept;

    // Copies a coherent fixed-size envelope into caller-owned storage. This
    // method is not a cross-thread synchronisation primitive; future telemetry
    // publication must call it from the audio thread before queueing a snapshot.
    [[nodiscard]] bool copyWaveformTelemetry(
        GranularTelemetrySnapshot& destination
    ) const noexcept;

    // Copies waveform and the most recently captured post-render active-grain
    // state. The destination is either left unchanged (when unprepared) or
    // replaced with one internally consistent snapshot.
    [[nodiscard]] bool copyTelemetrySnapshot(
        GranularTelemetrySnapshot& destination
    ) const noexcept;

private:
    static constexpr std::size_t waveformPointCapacity = 256;

    [[nodiscard]] bool isBlockConfigurationValid(
        const GranularAudioBlockView& block
    ) const noexcept;

    void configureWaveformAccumulator() noexcept;
    void resetWaveformAccumulator() noexcept;
    void accumulateWaveformFrame(
        std::span<const float> inputFrame
    ) noexcept;
    void discardExpiredWaveformPoints() noexcept;
    void captureActiveGrainTelemetry(
        const GranularEngineParameters& parameters
    ) noexcept;

    CircularAudioBuffer circularBuffer;
    GrainScheduler scheduler;
    GrainPool grainPool;
    GrainRenderer renderer;
    std::array<std::array<float, 2>, waveformPointCapacity> waveformMinimum {};
    std::array<std::array<float, 2>, waveformPointCapacity> waveformMaximum {};
    std::array<float, 2> currentWaveformMinimum {};
    std::array<float, 2> currentWaveformMaximum {};
    std::size_t waveformWriteIndex = 0;
    std::size_t waveformPointCount = 0;
    std::size_t waveformMaximumPointCount = 0;
    std::size_t waveformFramesPerPoint = 1;
    std::size_t waveformFramesInCurrentPoint = 0;
    CircularAudioBuffer::AbsoluteFrame waveformNewestExclusive = 0;
    CircularAudioBuffer::Generation waveformGeneration = 0;
    std::array<GrainTelemetry, granularTelemetryGrainCapacity>
        activeGrainTelemetry {};
    GranularEngineParameters telemetryParameters {};
    std::size_t activeGrainTelemetryCount = 0;
    std::uint64_t telemetrySequence = 0;
    bool prepared = false;
};
}
