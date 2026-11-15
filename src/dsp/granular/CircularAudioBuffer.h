#pragma once

#include "core/ProcessSpec.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace hypercolor::dsp::granular
{
class CircularAudioBuffer final
{
public:
    using AbsoluteFrame = std::uint64_t;
    using Generation = std::uint64_t;

    struct FrameRange
    {
        AbsoluteFrame oldest = 0;
        AbsoluteFrame newestExclusive = 0;

        [[nodiscard]] bool isEmpty() const noexcept
        {
            return oldest >= newestExclusive;
        }

        [[nodiscard]] AbsoluteFrame size() const noexcept
        {
            return isEmpty() ? 0 : newestExclusive - oldest;
        }
    };

    struct SourceRegion
    {
        Generation generation = 0;
        AbsoluteFrame oldest = 0;
        AbsoluteFrame newestExclusive = 0;

        [[nodiscard]] bool isEmpty() const noexcept
        {
            return oldest >= newestExclusive;
        }
    };

    static constexpr double minimumSupportedSampleRate = 44100.0;
    static constexpr double maximumSupportedSampleRate = 192000.0;
    static constexpr double productMaximumDurationSeconds = 10.0;
    static constexpr double defaultLogicalDurationSeconds = 4.0;

    CircularAudioBuffer() = default;
    ~CircularAudioBuffer() = default;

    CircularAudioBuffer(const CircularAudioBuffer&) = delete;
    CircularAudioBuffer& operator=(const CircularAudioBuffer&) = delete;
    CircularAudioBuffer(CircularAudioBuffer&&) = delete;
    CircularAudioBuffer& operator=(CircularAudioBuffer&&) = delete;

    [[nodiscard]] bool prepare(
        const core::ProcessSpec& spec,
        double maximumDurationSeconds,
        double logicalDurationSeconds = defaultLogicalDurationSeconds
    );

    // Invalidates every existing SourceRegion without clearing sample memory.
    void reset() noexcept;

    // Audio-thread-owned configuration. Call at an audio block boundary.
    [[nodiscard]] bool setLogicalDurationSeconds(
        double durationSeconds
    ) noexcept;

    // Writes one simultaneous mono/stereo frame. No state changes occur when
    // the span size does not match the prepared channel count.
    [[nodiscard]] bool writeFrame(
        std::span<const float> channelSamples
    ) noexcept;

    // Reads only from the material latched in sourceRegion. Logical duration
    // changes after capture do not affect this operation.
    [[nodiscard]] bool tryReadLinear(
        const SourceRegion& sourceRegion,
        std::uint32_t channel,
        double absolutePosition,
        float& result
    ) const noexcept;

    [[nodiscard]] bool isLinearReadPositionValid(
        const SourceRegion& sourceRegion,
        double absolutePosition
    ) const noexcept;

    // Validates x(k) = startPosition + playbackRate * k for k in
    // [0, frameCount). The first read occurs before the next writeFrame();
    // one new input frame is assumed before each subsequent read.
    [[nodiscard]] bool isLinearTrajectoryValid(
        const SourceRegion& sourceRegion,
        double startPosition,
        double playbackRate,
        std::uint32_t frameCount
    ) const noexcept;

    [[nodiscard]] FrameRange getPhysicalFrameRange() const noexcept;
    [[nodiscard]] FrameRange getLogicalFrameRange() const noexcept;
    [[nodiscard]] SourceRegion captureSourceRegion() const noexcept;
    [[nodiscard]] bool isSourceRegionCurrent(
        const SourceRegion& sourceRegion
    ) const noexcept;

    [[nodiscard]] bool isPrepared() const noexcept;
    [[nodiscard]] double getSampleRate() const noexcept;
    [[nodiscard]] double getMaximumDurationSeconds() const noexcept;
    [[nodiscard]] double getLogicalDurationSeconds() const noexcept;
    [[nodiscard]] std::size_t getCapacityFrames() const noexcept;
    [[nodiscard]] std::size_t getLogicalLengthFrames() const noexcept;
    [[nodiscard]] std::uint32_t getNumChannels() const noexcept;
    [[nodiscard]] AbsoluteFrame getAbsoluteWritePosition() const noexcept;
    [[nodiscard]] Generation getGeneration() const noexcept;

private:
    struct LinearTaps
    {
        AbsoluteFrame first = 0;
        AbsoluteFrame second = 0;
        double fraction = 0.0;
    };

    [[nodiscard]] bool getLinearTaps(
        double absolutePosition,
        LinearTaps& taps
    ) const noexcept;
    [[nodiscard]] bool areTapsInsideSourceAndPhysicalRanges(
        const SourceRegion& sourceRegion,
        const LinearTaps& taps
    ) const noexcept;
    [[nodiscard]] bool isTrajectoryPositionPhysicallyValid(
        long double startPosition,
        long double playbackRate,
        AbsoluteFrame offset
    ) const noexcept;
    [[nodiscard]] bool isTrajectoryPositionInsideSource(
        const SourceRegion& sourceRegion,
        long double position
    ) const noexcept;
    [[nodiscard]] AbsoluteFrame getPhysicalOldestAtFutureOffset(
        AbsoluteFrame offset
    ) const noexcept;

    void invalidatePreparedState() noexcept;
    void advanceGeneration() noexcept;

    std::vector<float> samples;
    double sampleRate = 0.0;
    double maximumDurationSeconds = 0.0;
    std::size_t capacityFrames = 0;
    std::size_t logicalLengthFrames = 0;
    std::uint32_t numChannels = 0;
    AbsoluteFrame absoluteWritePosition = 0;
    AbsoluteFrame validFrames = 0;
    Generation generation = 0;
    bool prepared = false;
};
}
