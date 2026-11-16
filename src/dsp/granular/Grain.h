#pragma once

#include "dsp/granular/CircularAudioBuffer.h"

#include <cstdint>

namespace hypercolor::dsp::granular
{
enum class GrainState : std::uint8_t
{
    inactive,
    active,
    finished
};

enum class GrainWindow : std::uint8_t
{
    hann
};

struct GrainInit
{
    CircularAudioBuffer::SourceRegion sourceRegion;
    double startPosition = 0.0;
    double playbackRate = 1.0;
    std::uint32_t durationFrames = 0;
    GrainWindow window = GrainWindow::hann;
    double pan = 0.0;
};

enum class GrainInitResult : std::uint8_t
{
    success,
    invalidParameters,
    invalidSourceTrajectory
};

class Grain final
{
public:
    static constexpr std::uint32_t minimumDurationFrames = 3;

    Grain() = default;
    ~Grain() = default;

    Grain(const Grain&) = delete;
    Grain& operator=(const Grain&) = delete;
    Grain(Grain&&) = delete;
    Grain& operator=(Grain&&) = delete;

    [[nodiscard]] GrainInitResult initialise(
        const GrainInit& init,
        const CircularAudioBuffer& source
    ) noexcept;

    // Call exactly once after one complete output frame has been rendered.
    void advanceAfterRender() noexcept;
    void markFinished() noexcept;
    void reset() noexcept;

    [[nodiscard]] GrainState getState() const noexcept;
    [[nodiscard]] bool isActive() const noexcept;
    [[nodiscard]] bool isFinished() const noexcept;

    [[nodiscard]] const CircularAudioBuffer::SourceRegion&
        getSourceRegion() const noexcept;
    [[nodiscard]] double getReadPosition() const noexcept;
    [[nodiscard]] double getPlaybackRate() const noexcept;
    [[nodiscard]] std::uint32_t getDurationFrames() const noexcept;
    [[nodiscard]] std::uint32_t getRenderedFrames() const noexcept;
    [[nodiscard]] GrainWindow getWindow() const noexcept;
    [[nodiscard]] double getPan() const noexcept;

    // Returns the coefficient for the frame at the current read position.
    [[nodiscard]] float getWindowCoefficient() const noexcept;

private:
    CircularAudioBuffer::SourceRegion sourceRegion;
    double startPosition = 0.0;
    double readPosition = 0.0;
    double playbackRate = 1.0;
    std::uint32_t durationFrames = 0;
    std::uint32_t renderedFrames = 0;
    GrainWindow window = GrainWindow::hann;
    double pan = 0.0;
    GrainState state = GrainState::inactive;
};
}
