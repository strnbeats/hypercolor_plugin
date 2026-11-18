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
        case GrainWindow::triangle:
        case GrainWindow::smooth:
        case GrainWindow::exponential:
            return true;
    }

    return false;
}

bool isKnownSourceKind(CausalSourceKind kind) noexcept
{
    switch (kind)
    {
        case CausalSourceKind::history:
        case CausalSourceKind::causalLive:
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
        || !isKnownWindow(init.window)
        || !isKnownSourceKind(init.sourceKind))
    {
        return GrainInitResult::invalidParameters;
    }

    const auto generationForInit = init.sourceGeneration != 0
        ? init.sourceGeneration
        : init.sourceRegion.generation;
    const auto validHistory = init.sourceKind == CausalSourceKind::history
        && !init.sourceRegion.isEmpty()
        && source.isSourceRegionCurrent(init.sourceRegion)
        && source.isLinearTrajectoryValid(
            init.sourceRegion,
            init.startPosition,
            init.playbackRate,
            init.durationFrames
        );
    const auto validLive = init.sourceKind == CausalSourceKind::causalLive
        && generationForInit != 0
        && generationForInit == source.getGeneration()
        && source.isCausalLiveTrajectoryValid(
            generationForInit,
            init.startPosition,
            init.playbackRate,
            init.durationFrames,
            init.causalMarginSamples
        );
    if (!validHistory && !validLive)
    {
        return GrainInitResult::invalidSourceTrajectory;
    }

    // Commit only after every check succeeds, keeping failed initialisation
    // transactional and leaving this grain inactive.
    sourceRegion = init.sourceKind == CausalSourceKind::history
        ? init.sourceRegion
        : CircularAudioBuffer::SourceRegion {};
    sourceKind = init.sourceKind;
    this->sourceGeneration = generationForInit;
    causalMarginSamples = init.causalMarginSamples;
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
    sourceKind = CausalSourceKind::history;
    sourceGeneration = 0;
    causalMarginSamples = CausalReadPlanBuilder::defaultCausalMarginSamples;
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

CausalSourceKind Grain::getSourceKind() const noexcept
{
    return sourceKind;
}

CircularAudioBuffer::Generation Grain::getSourceGeneration() const noexcept
{
    return sourceGeneration;
}

std::uint32_t Grain::getCausalMarginSamples() const noexcept
{
    return causalMarginSamples;
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
        case GrainWindow::triangle:
            return static_cast<float>(1.0 - std::abs(2.0 * phase - 1.0));
        case GrainWindow::smooth:
        {
            const auto squared = phase * phase
                * (1.0 - phase) * (1.0 - phase);
            return static_cast<float>(16.0 * squared);
        }
        case GrainWindow::exponential:
        {
            constexpr auto curvature = 4.0;
            const auto distanceFromCentre = std::abs(2.0 * phase - 1.0);
            const auto numerator = std::expm1(
                curvature * (1.0 - distanceFromCentre)
            );
            const auto denominator = std::expm1(curvature);
            return static_cast<float>(numerator / denominator);
        }
    }

    return 0.0f;
}
}
