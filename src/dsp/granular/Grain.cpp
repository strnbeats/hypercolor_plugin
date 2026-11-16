#include "dsp/granular/Grain.h"

#include <cmath>
#include <numbers>

namespace hypercolor::dsp::granular
{
namespace
{
bool isKnownWindow(GrainWindow window) noexcept
{
    switch (window)
    {
        case GrainWindow::hann:
            return true;
    }

    return false;
}
}

GrainInitResult Grain::initialise(
    const GrainInit& init,
    const CircularAudioBuffer& source
) noexcept
{
    if (state != GrainState::inactive
        || !std::isfinite(init.startPosition)
        || !std::isfinite(init.playbackRate)
        || !std::isfinite(init.pan)
        || init.pan < -1.0
        || init.pan > 1.0
        || init.durationFrames < minimumDurationFrames
        || !isKnownWindow(init.window))
    {
        return GrainInitResult::invalidParameters;
    }

    if (init.sourceRegion.isEmpty()
        || !source.isSourceRegionCurrent(init.sourceRegion)
        || !source.isLinearTrajectoryValid(
            init.sourceRegion,
            init.startPosition,
            init.playbackRate,
            init.durationFrames
        ))
    {
        return GrainInitResult::invalidSourceTrajectory;
    }

    // Commit only after every check succeeds, keeping failed initialisation
    // transactional and leaving this grain inactive.
    sourceRegion = init.sourceRegion;
    startPosition = init.startPosition;
    readPosition = init.startPosition;
    playbackRate = init.playbackRate;
    durationFrames = init.durationFrames;
    renderedFrames = 0;
    window = init.window;
    pan = init.pan;
    state = GrainState::active;
    return GrainInitResult::success;
}

void Grain::advanceAfterRender() noexcept
{
    if (state != GrainState::active)
        return;

    ++renderedFrames;

    if (renderedFrames >= durationFrames)
    {
        renderedFrames = durationFrames;
        state = GrainState::finished;
        return;
    }

    // Derive every position from immutable initial state. This exactly matches
    // the arithmetic contract used by CircularAudioBuffer trajectory checks.
    readPosition = CircularAudioBuffer::calculateLinearTrajectoryPosition(
        startPosition,
        playbackRate,
        renderedFrames
    );
}

void Grain::markFinished() noexcept
{
    if (state == GrainState::active)
        state = GrainState::finished;
}

void Grain::reset() noexcept
{
    sourceRegion = {};
    startPosition = 0.0;
    readPosition = 0.0;
    playbackRate = 1.0;
    durationFrames = 0;
    renderedFrames = 0;
    window = GrainWindow::hann;
    pan = 0.0;
    state = GrainState::inactive;
}

GrainState Grain::getState() const noexcept
{
    return state;
}

bool Grain::isActive() const noexcept
{
    return state == GrainState::active;
}

bool Grain::isFinished() const noexcept
{
    return state == GrainState::finished;
}

const CircularAudioBuffer::SourceRegion&
Grain::getSourceRegion() const noexcept
{
    return sourceRegion;
}

double Grain::getReadPosition() const noexcept
{
    return readPosition;
}

double Grain::getPlaybackRate() const noexcept
{
    return playbackRate;
}

std::uint32_t Grain::getDurationFrames() const noexcept
{
    return durationFrames;
}

std::uint32_t Grain::getRenderedFrames() const noexcept
{
    return renderedFrames;
}

GrainWindow Grain::getWindow() const noexcept
{
    return window;
}

double Grain::getPan() const noexcept
{
    return pan;
}

float Grain::getWindowCoefficient() const noexcept
{
    if (state != GrainState::active
        || durationFrames < minimumDurationFrames
        || renderedFrames >= durationFrames)
    {
        return 0.0f;
    }

    if (renderedFrames == 0 || renderedFrames == durationFrames - 1)
        return 0.0f;

    const auto phase = static_cast<double>(renderedFrames)
        / static_cast<double>(durationFrames - 1);

    switch (window)
    {
        case GrainWindow::hann:
            return static_cast<float>(
                0.5
                - 0.5 * std::cos(2.0 * std::numbers::pi * phase)
            );
    }

    return 0.0f;
}
}
