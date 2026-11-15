#include "dsp/granular/GrainPool.h"

namespace hypercolor::dsp::granular
{
GrainStartResult GrainPool::tryStart(
    const GrainInit& init,
    const CircularAudioBuffer& source
) noexcept
{
    if (traversalInProgress)
        return GrainStartResult::traversalInProgress;

    for (auto& grain : grains)
    {
        if (grain.getState() != GrainState::inactive)
            continue;

        return grain.initialise(init, source) == GrainInitResult::success
            ? GrainStartResult::started
            : GrainStartResult::invalidGrain;
    }

    return GrainStartResult::poolFull;
}

void GrainPool::reclaimFinished() noexcept
{
    if (traversalInProgress)
        return;

    for (auto& grain : grains)
    {
        if (grain.isFinished())
            grain.reset();
    }
}

void GrainPool::reset() noexcept
{
    if (traversalInProgress)
        return;

    for (auto& grain : grains)
        grain.reset();
}

std::size_t GrainPool::getActiveCount() const noexcept
{
    std::size_t count = 0;

    for (const auto& grain : grains)
    {
        if (grain.isActive())
            ++count;
    }

    return count;
}

std::size_t GrainPool::getOccupiedCount() const noexcept
{
    std::size_t count = 0;

    for (const auto& grain : grains)
    {
        if (grain.getState() != GrainState::inactive)
            ++count;
    }

    return count;
}

bool GrainPool::isFull() const noexcept
{
    return getOccupiedCount() == maximumGrains;
}

bool GrainPool::isTraversalInProgress() const noexcept
{
    return traversalInProgress;
}
}
