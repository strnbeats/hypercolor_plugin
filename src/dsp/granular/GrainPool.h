#pragma once

#include "dsp/granular/Grain.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace hypercolor::dsp::granular
{
enum class GrainStartResult : std::uint8_t
{
    started,
    poolFull,
    invalidGrain,
    traversalInProgress
};

class GrainPool;

class GrainRenderAccess final
{
public:
    ~GrainRenderAccess() = default;

    GrainRenderAccess(const GrainRenderAccess&) = delete;
    GrainRenderAccess& operator=(const GrainRenderAccess&) = delete;
    GrainRenderAccess(GrainRenderAccess&&) = delete;
    GrainRenderAccess& operator=(GrainRenderAccess&&) = delete;

    [[nodiscard]] GrainState getState() const noexcept
    {
        return grain.getState();
    }

    [[nodiscard]] bool isActive() const noexcept
    {
        return grain.isActive();
    }

    [[nodiscard]] bool isFinished() const noexcept
    {
        return grain.isFinished();
    }

    [[nodiscard]] const CircularAudioBuffer::SourceRegion&
        getSourceRegion() const noexcept
    {
        return grain.getSourceRegion();
    }

    [[nodiscard]] double getReadPosition() const noexcept
    {
        return grain.getReadPosition();
    }

    [[nodiscard]] double getPlaybackRate() const noexcept
    {
        return grain.getPlaybackRate();
    }

    [[nodiscard]] std::uint32_t getDurationFrames() const noexcept
    {
        return grain.getDurationFrames();
    }

    [[nodiscard]] std::uint32_t getRenderedFrames() const noexcept
    {
        return grain.getRenderedFrames();
    }

    [[nodiscard]] GrainWindow getWindow() const noexcept
    {
        return grain.getWindow();
    }

    [[nodiscard]] double getPan() const noexcept
    {
        return grain.getPan();
    }

    [[nodiscard]] float getWindowCoefficient() const noexcept
    {
        return grain.getWindowCoefficient();
    }

    void advanceAfterRender() noexcept
    {
        grain.advanceAfterRender();
    }

    void markFinished() noexcept
    {
        grain.markFinished();
    }

private:
    friend class GrainPool;

    explicit GrainRenderAccess(Grain& grainToAccess) noexcept
        : grain(grainToAccess)
    {
    }

    Grain& grain;
};

class GrainPool final
{
public:
    static constexpr std::size_t maximumGrains = 64;

    GrainPool() = default;
    ~GrainPool() = default;

    GrainPool(const GrainPool&) = delete;
    GrainPool& operator=(const GrainPool&) = delete;
    GrainPool(GrainPool&&) = delete;
    GrainPool& operator=(GrainPool&&) = delete;

    [[nodiscard]] GrainStartResult tryStart(
        const GrainInit& init,
        const CircularAudioBuffer& source
    ) noexcept;

    // Visitors receive only the operations required to render the current
    // Grain. A runtime guard rejects structural GrainPool re-entry.
    template <typename Visitor>
    void forEachActive(Visitor&& visitor) noexcept
    {
        static_assert(
            std::is_nothrow_invocable_v<Visitor&, GrainRenderAccess&>,
            "GrainPool visitors must be noexcept"
        );

        if (traversalInProgress)
            return;

        traversalInProgress = true;

        for (auto& grain : grains)
        {
            if (grain.isActive())
            {
                GrainRenderAccess access { grain };
                visitor(access);
            }
        }

        traversalInProgress = false;
    }

    // Call after the active traversal. Do not start another grain until the
    // next sample's writeFrame -> tryStart -> traversal sequence.
    void reclaimFinished() noexcept;
    void reset() noexcept;

    [[nodiscard]] std::size_t getActiveCount() const noexcept;
    [[nodiscard]] std::size_t getOccupiedCount() const noexcept;
    [[nodiscard]] bool isFull() const noexcept;
    [[nodiscard]] bool isTraversalInProgress() const noexcept;

private:
    std::array<Grain, maximumGrains> grains;
    bool traversalInProgress = false;
};
}
