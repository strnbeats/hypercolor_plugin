#include "dsp/granular/CircularAudioBuffer.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <stdexcept>

namespace hypercolor::dsp::granular
{
namespace
{
constexpr auto maximumExactDoubleInteger = std::uint64_t { 1 }
    << std::numeric_limits<double>::digits;

bool durationToFrames(
    double sampleRate,
    double durationSeconds,
    std::size_t& result
) noexcept
{
    if (!std::isfinite(sampleRate)
        || !std::isfinite(durationSeconds)
        || sampleRate <= 0.0
        || durationSeconds <= 0.0)
    {
        return false;
    }

    const auto exactFrames = static_cast<long double>(sampleRate)
        * static_cast<long double>(durationSeconds);
    const auto roundedFrames = std::ceil(exactFrames);

    if (!std::isfinite(roundedFrames)
        || roundedFrames < 1.0L
        || roundedFrames
            > static_cast<long double>(
                std::numeric_limits<std::size_t>::max()
            ))
    {
        return false;
    }

    result = static_cast<std::size_t>(roundedFrames);
    return true;
}
}

bool CircularAudioBuffer::prepare(
    const core::ProcessSpec& spec,
    double requestedMaximumDurationSeconds,
    double requestedLogicalDurationSeconds
)
{
    std::size_t requestedCapacity = 0;
    std::size_t requestedLogicalLength = 0;

    const auto validSpec = std::isfinite(spec.sampleRate)
        && spec.sampleRate >= minimumSupportedSampleRate
        && spec.sampleRate <= maximumSupportedSampleRate
        && spec.maximumBlockSize > 0
        && (spec.numChannels == 1 || spec.numChannels == 2);

    const auto validDurations = std::isfinite(
            requestedMaximumDurationSeconds
        )
        && std::isfinite(requestedLogicalDurationSeconds)
        && requestedMaximumDurationSeconds > 0.0
        && requestedMaximumDurationSeconds
            <= productMaximumDurationSeconds
        && requestedLogicalDurationSeconds > 0.0
        && requestedLogicalDurationSeconds
            <= requestedMaximumDurationSeconds
        && durationToFrames(
            spec.sampleRate,
            requestedMaximumDurationSeconds,
            requestedCapacity
        )
        && durationToFrames(
            spec.sampleRate,
            requestedLogicalDurationSeconds,
            requestedLogicalLength
        );

    const auto channels = static_cast<std::size_t>(spec.numChannels);
    const auto capacityFitsAllocation = requestedCapacity > 0
        && channels > 0
        && requestedCapacity
            <= std::numeric_limits<std::size_t>::max() / channels;

    if (!validSpec
        || !validDurations
        || !capacityFitsAllocation
        || requestedLogicalLength > requestedCapacity)
    {
        invalidatePreparedState();
        return false;
    }

    const auto totalSamples = requestedCapacity * channels;

    try
    {
        samples.assign(totalSamples, 0.0f);
    }
    catch (const std::bad_alloc&)
    {
        invalidatePreparedState();
        return false;
    }
    catch (const std::length_error&)
    {
        invalidatePreparedState();
        return false;
    }

    sampleRate = spec.sampleRate;
    maximumDurationSeconds = requestedMaximumDurationSeconds;
    capacityFrames = requestedCapacity;
    logicalLengthFrames = requestedLogicalLength;
    numChannels = spec.numChannels;
    absoluteWritePosition = 0;
    validFrames = 0;
    advanceGeneration();
    prepared = true;
    return true;
}

void CircularAudioBuffer::reset() noexcept
{
    absoluteWritePosition = 0;
    validFrames = 0;
    advanceGeneration();
}

bool CircularAudioBuffer::setLogicalDurationSeconds(
    double durationSeconds
) noexcept
{
    std::size_t requestedLength = 0;

    if (!prepared
        || !std::isfinite(durationSeconds)
        || durationSeconds <= 0.0
        || durationSeconds > maximumDurationSeconds
        || !durationToFrames(sampleRate, durationSeconds, requestedLength)
        || requestedLength > capacityFrames)
    {
        return false;
    }

    logicalLengthFrames = requestedLength;
    return true;
}

bool CircularAudioBuffer::writeFrame(
    std::span<const float> channelSamples
) noexcept
{
    if (!prepared || channelSamples.size() != numChannels)
        return false;

    // Keeping absolute positions exactly representable by double makes the
    // fractional read contract deterministic. This rollover is effectively
    // unreachable in a real session (more than a millennium at 192 kHz).
    if (absoluteWritePosition >= maximumExactDoubleInteger)
        reset();

    const auto physicalFrame = static_cast<std::size_t>(
        absoluteWritePosition % capacityFrames
    );

    for (std::size_t channel = 0; channel < numChannels; ++channel)
    {
        samples[channel * capacityFrames + physicalFrame]
            = channelSamples[channel];
    }

    ++absoluteWritePosition;
    validFrames = std::min<AbsoluteFrame>(
        validFrames + 1,
        static_cast<AbsoluteFrame>(capacityFrames)
    );
    return true;
}

bool CircularAudioBuffer::tryReadLinear(
    const SourceRegion& sourceRegion,
    std::uint32_t channel,
    double absolutePosition,
    float& result
) const noexcept
{
    result = 0.0f;

    if (channel >= numChannels)
        return false;

    LinearTaps taps;

    if (!getLinearTaps(absolutePosition, taps)
        || !areTapsInsideSourceAndPhysicalRanges(sourceRegion, taps))
    {
        return false;
    }

    const auto firstPhysical = static_cast<std::size_t>(
        taps.first % capacityFrames
    );
    const auto secondPhysical = static_cast<std::size_t>(
        taps.second % capacityFrames
    );
    const auto channelOffset = static_cast<std::size_t>(channel)
        * capacityFrames;
    const auto firstSample = samples[channelOffset + firstPhysical];
    const auto secondSample = samples[channelOffset + secondPhysical];

    result = static_cast<float>(
        static_cast<double>(firstSample)
        + taps.fraction
            * static_cast<double>(secondSample - firstSample)
    );
    return true;
}

bool CircularAudioBuffer::isLinearReadPositionValid(
    const SourceRegion& sourceRegion,
    double absolutePosition
) const noexcept
{
    LinearTaps taps;
    return getLinearTaps(absolutePosition, taps)
        && areTapsInsideSourceAndPhysicalRanges(sourceRegion, taps);
}

bool CircularAudioBuffer::isLinearTrajectoryValid(
    const SourceRegion& sourceRegion,
    double startPosition,
    double playbackRate,
    std::uint32_t frameCount
) const noexcept
{
    if (!isSourceRegionCurrent(sourceRegion)
        || sourceRegion.isEmpty()
        || frameCount == 0
        || !std::isfinite(startPosition)
        || !std::isfinite(playbackRate))
    {
        return false;
    }

    const auto lastOffset = static_cast<AbsoluteFrame>(frameCount - 1);

    if (lastOffset > maximumExactDoubleInteger - absoluteWritePosition)
        return false;

    const auto start = static_cast<long double>(startPosition);
    const auto rate = static_cast<long double>(playbackRate);
    const auto end = start
        + rate * static_cast<long double>(lastOffset);

    if (!std::isfinite(start)
        || !std::isfinite(rate)
        || !std::isfinite(end)
        || !isTrajectoryPositionInsideSource(sourceRegion, start)
        || !isTrajectoryPositionInsideSource(sourceRegion, end))
    {
        return false;
    }

    // Source bounds are fixed and the trajectory is linear, so validating
    // both endpoints proves every intermediate position remains in the
    // region. Physical overwrite has one additional piecewise-linear
    // boundary: it is stationary until the buffer fills, then advances by
    // one frame per rendered sample. Endpoints of both pieces are sufficient.
    if (!isTrajectoryPositionPhysicallyValid(start, rate, 0)
        || !isTrajectoryPositionPhysicallyValid(
            start,
            rate,
            lastOffset
        ))
    {
        return false;
    }

    const auto capacity = static_cast<AbsoluteFrame>(capacityFrames);

    if (absoluteWritePosition < capacity)
    {
        const auto firstOverwriteOffset = capacity
            - absoluteWritePosition + 1;

        if (firstOverwriteOffset <= lastOffset
            && !isTrajectoryPositionPhysicallyValid(
                start,
                rate,
                firstOverwriteOffset
            ))
        {
            return false;
        }
    }

    return true;
}

CircularAudioBuffer::FrameRange
CircularAudioBuffer::getPhysicalFrameRange() const noexcept
{
    if (!prepared)
        return {};

    return {
        absoluteWritePosition - validFrames,
        absoluteWritePosition
    };
}

CircularAudioBuffer::FrameRange
CircularAudioBuffer::getLogicalFrameRange() const noexcept
{
    if (!prepared)
        return {};

    const auto logicalValidFrames = std::min<AbsoluteFrame>(
        validFrames,
        static_cast<AbsoluteFrame>(logicalLengthFrames)
    );

    return {
        absoluteWritePosition - logicalValidFrames,
        absoluteWritePosition
    };
}

CircularAudioBuffer::SourceRegion
CircularAudioBuffer::captureSourceRegion() const noexcept
{
    const auto range = getLogicalFrameRange();
    return { generation, range.oldest, range.newestExclusive };
}

bool CircularAudioBuffer::isSourceRegionCurrent(
    const SourceRegion& sourceRegion
) const noexcept
{
    return prepared
        && sourceRegion.generation != 0
        && sourceRegion.generation == generation
        && sourceRegion.oldest <= sourceRegion.newestExclusive
        && sourceRegion.newestExclusive <= absoluteWritePosition;
}

bool CircularAudioBuffer::isPrepared() const noexcept
{
    return prepared;
}

double CircularAudioBuffer::getSampleRate() const noexcept
{
    return sampleRate;
}

double CircularAudioBuffer::getMaximumDurationSeconds() const noexcept
{
    return maximumDurationSeconds;
}

double CircularAudioBuffer::getLogicalDurationSeconds() const noexcept
{
    if (!prepared || sampleRate <= 0.0)
        return 0.0;

    return static_cast<double>(logicalLengthFrames) / sampleRate;
}

std::size_t CircularAudioBuffer::getCapacityFrames() const noexcept
{
    return capacityFrames;
}

std::size_t CircularAudioBuffer::getLogicalLengthFrames() const noexcept
{
    return logicalLengthFrames;
}

std::uint32_t CircularAudioBuffer::getNumChannels() const noexcept
{
    return numChannels;
}

CircularAudioBuffer::AbsoluteFrame
CircularAudioBuffer::getAbsoluteWritePosition() const noexcept
{
    return absoluteWritePosition;
}

CircularAudioBuffer::Generation
CircularAudioBuffer::getGeneration() const noexcept
{
    return generation;
}

bool CircularAudioBuffer::getLinearTaps(
    double absolutePosition,
    LinearTaps& taps
) const noexcept
{
    if (!prepared
        || !std::isfinite(absolutePosition)
        || absolutePosition < 0.0
        || absolutePosition
            >= static_cast<double>(maximumExactDoubleInteger))
    {
        return false;
    }

    const auto firstAsDouble = std::floor(absolutePosition);
    const auto first = static_cast<AbsoluteFrame>(firstAsDouble);

    if (first == std::numeric_limits<AbsoluteFrame>::max())
        return false;

    taps.first = first;
    taps.second = first + 1;
    taps.fraction = absolutePosition - firstAsDouble;
    return taps.fraction >= 0.0 && taps.fraction < 1.0;
}

bool CircularAudioBuffer::areTapsInsideSourceAndPhysicalRanges(
    const SourceRegion& sourceRegion,
    const LinearTaps& taps
) const noexcept
{
    if (!isSourceRegionCurrent(sourceRegion))
        return false;

    const auto physical = getPhysicalFrameRange();
    return taps.first >= sourceRegion.oldest
        && taps.second < sourceRegion.newestExclusive
        && taps.first >= physical.oldest
        && taps.second < physical.newestExclusive;
}

bool CircularAudioBuffer::isTrajectoryPositionPhysicallyValid(
    long double startPosition,
    long double playbackRate,
    AbsoluteFrame offset
) const noexcept
{
    const auto position = startPosition
        + playbackRate * static_cast<long double>(offset);

    if (!std::isfinite(position)
        || position < 0.0L
        || position
            >= static_cast<long double>(maximumExactDoubleInteger))
    {
        return false;
    }

    const auto first = std::floor(position);
    const auto oldest = getPhysicalOldestAtFutureOffset(offset);
    return first >= static_cast<long double>(oldest);
}

bool CircularAudioBuffer::isTrajectoryPositionInsideSource(
    const SourceRegion& sourceRegion,
    long double position
) const noexcept
{
    if (!std::isfinite(position)
        || position < 0.0L
        || position
            >= static_cast<long double>(maximumExactDoubleInteger))
    {
        return false;
    }

    const auto first = std::floor(position);

    if (first < static_cast<long double>(sourceRegion.oldest))
        return false;

    // Linear interpolation always reserves both taps, including at integer
    // positions. This is the strict x < newestExclusive - 1 contract.
    return sourceRegion.newestExclusive > 0
        && first + 1.0L
            < static_cast<long double>(sourceRegion.newestExclusive);
}

CircularAudioBuffer::AbsoluteFrame
CircularAudioBuffer::getPhysicalOldestAtFutureOffset(
    AbsoluteFrame offset
) const noexcept
{
    const auto futureWritePosition = absoluteWritePosition + offset;
    const auto capacity = static_cast<AbsoluteFrame>(capacityFrames);
    return futureWritePosition > capacity
        ? futureWritePosition - capacity
        : 0;
}

void CircularAudioBuffer::invalidatePreparedState() noexcept
{
    samples.clear();
    sampleRate = 0.0;
    maximumDurationSeconds = 0.0;
    capacityFrames = 0;
    logicalLengthFrames = 0;
    numChannels = 0;
    absoluteWritePosition = 0;
    validFrames = 0;
    prepared = false;
    advanceGeneration();
}

void CircularAudioBuffer::advanceGeneration() noexcept
{
    if (generation == std::numeric_limits<Generation>::max())
        generation = 1;
    else
        ++generation;
}
}
