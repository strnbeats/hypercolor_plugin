#include "GranularVisualizer.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace
{
constexpr int visualizerRefreshRateHz = 30;

const auto background = juce::Colour::fromRGB(18, 22, 35);
const auto outline = juce::Colour::fromRGB(55, 64, 88);
const auto grid = juce::Colour::fromRGB(43, 50, 70);
const auto text = juce::Colour::fromRGB(184, 191, 212);
const auto mutedText = juce::Colour::fromRGB(112, 122, 148);
const auto teal = juce::Colour::fromRGB(115, 230, 210);
const auto pink = juce::Colour::fromRGB(230, 115, 198);

float sanitiseAmplitude(float value) noexcept
{
    return std::isfinite(value) ? juce::jlimit(-1.0f, 1.0f, value) : 0.0f;
}

float sampleToY(float sample, float centreY, float halfHeight) noexcept
{
    return centreY - sanitiseAmplitude(sample) * halfHeight;
}
}

GranularVisualizer::GranularVisualizer(
    HypercolorAudioProcessor& processorToObserve
)
    : audioProcessor(processorToObserve)
{
    setComponentID(componentID);
    setOpaque(false);

    // Snapshots retained while the editor was closed may be arbitrarily old
    // because a full queue drops new publications. Drain the complete fixed
    // capacity without displaying it. Publications racing with construction
    // may be discarded here or remain queued, but neither can become visible
    // except through a later timer callback, so no cross-thread barrier is
    // required for the initial waiting-state invariant.
    hypercolor::dsp::granular::GranularTelemetrySnapshot discardedSnapshot;

    for (std::size_t attempt = 0;
         attempt < hypercolor::dsp::granular::granularTelemetryQueueCapacity;
         ++attempt)
    {
        if (!audioProcessor.tryPopGranularTelemetry(discardedSnapshot))
            break;
    }

    startTimerHz(visualizerRefreshRateHz);
}

GranularVisualizer::~GranularVisualizer()
{
    stopTimer();
}

bool GranularVisualizer::consumePendingTelemetry() noexcept
{
    auto receivedSnapshot = false;

    for (std::size_t attempt = 0;
         attempt < hypercolor::dsp::granular::granularTelemetryQueueCapacity;
         ++attempt)
    {
        if (!audioProcessor.tryPopGranularTelemetry(latestSnapshot))
            break;

        hasSnapshot = true;
        receivedSnapshot = true;
    }

    return receivedSnapshot;
}

bool GranularVisualizer::copyLatestSnapshot(
    hypercolor::dsp::granular::GranularTelemetrySnapshot& destination
) const noexcept
{
    if (!hasSnapshot)
        return false;

    destination = latestSnapshot;
    return true;
}

void GranularVisualizer::timerCallback()
{
    if (consumePendingTelemetry())
        repaint();
}

void GranularVisualizer::paint(juce::Graphics& graphics)
{
    auto bounds = getLocalBounds().toFloat();

    graphics.setColour(background);
    graphics.fillRoundedRectangle(bounds, 8.0f);
    graphics.setColour(outline);
    graphics.drawRoundedRectangle(bounds.reduced(0.5f), 8.0f, 1.0f);

    auto content = bounds.reduced(12.0f, 8.0f);
    auto header = content.removeFromTop(20.0f);

    graphics.setFont(juce::FontOptions(11.0f).withStyle("Bold"));
    graphics.setColour(teal);
    graphics.drawText(
        "LIVE BUFFER",
        header,
        juce::Justification::centredLeft,
        false
    );

    graphics.setColour(hasSnapshot ? text : mutedText);
    const auto grainCount = hasSnapshot
        ? std::min<std::uint32_t>(
            latestSnapshot.activeGrainCount,
            static_cast<std::uint32_t>(latestSnapshot.grains.size())
        )
        : 0u;
    const auto status = hasSnapshot
        ? juce::String(grainCount) + (grainCount == 1 ? " GRAIN" : " GRAINS")
        : juce::String("WAITING FOR AUDIO");
    graphics.drawText(
        status,
        header,
        juce::Justification::centredRight,
        false
    );

    auto plot = content.reduced(0.0f, 3.0f);
    graphics.setColour(grid);
    graphics.drawHorizontalLine(
        juce::roundToInt(plot.getCentreY()),
        plot.getX(),
        plot.getRight()
    );

    for (int division = 1; division < 4; ++division)
    {
        const auto x = plot.getX()
            + plot.getWidth() * static_cast<float>(division) / 4.0f;
        graphics.drawVerticalLine(
            juce::roundToInt(x),
            plot.getY(),
            plot.getBottom()
        );
    }

    if (!hasSnapshot)
    {
        graphics.setColour(mutedText);
        graphics.setFont(juce::FontOptions(11.0f));
        graphics.drawText(
            "Telemetry appears when audio is running",
            plot,
            juce::Justification::centred,
            false
        );
        return;
    }

    drawWaveform(graphics, plot);
    drawGrains(graphics, plot);
}

void GranularVisualizer::drawWaveform(
    juce::Graphics& graphics,
    juce::Rectangle<float> bounds
) const
{
    const auto pointCount = std::min<std::size_t>(
        latestSnapshot.waveformPointCount,
        latestSnapshot.waveformPoints.size()
    );
    const auto channelCount = std::min<std::uint32_t>(
        latestSnapshot.numChannels,
        2u
    );

    if (pointCount == 0 || channelCount == 0 || bounds.isEmpty())
        return;

    const auto step = pointCount > 1
        ? bounds.getWidth() / static_cast<float>(pointCount - 1)
        : 0.0f;

    for (std::uint32_t channel = 0; channel < channelCount; ++channel)
    {
        const auto stereo = channelCount == 2;
        const auto laneHeight = stereo
            ? bounds.getHeight() * 0.5f
            : bounds.getHeight();
        const auto laneTop = bounds.getY()
            + (stereo ? laneHeight * static_cast<float>(channel) : 0.0f);
        const auto centreY = laneTop + laneHeight * 0.5f;
        const auto amplitudeHeight = juce::jmax(1.0f, laneHeight * 0.43f);

        graphics.setColour((channel == 0 ? teal : pink).withAlpha(0.82f));

        for (std::size_t point = 0; point < pointCount; ++point)
        {
            const auto& telemetryPoint = latestSnapshot.waveformPoints[point];
            const auto x = pointCount > 1
                ? bounds.getX() + step * static_cast<float>(point)
                : bounds.getCentreX();
            const auto minimumY = sampleToY(
                telemetryPoint.minimum[channel],
                centreY,
                amplitudeHeight
            );
            const auto maximumY = sampleToY(
                telemetryPoint.maximum[channel],
                centreY,
                amplitudeHeight
            );

            graphics.drawVerticalLine(
                juce::roundToInt(x),
                std::min(minimumY, maximumY),
                std::max(minimumY, maximumY)
            );
        }
    }
}

void GranularVisualizer::drawGrains(
    juce::Graphics& graphics,
    juce::Rectangle<float> bounds
) const
{
    const auto grainCount = std::min<std::size_t>(
        latestSnapshot.activeGrainCount,
        latestSnapshot.grains.size()
    );
    const auto historyLength = latestSnapshot.logicalNewestExclusive
        >= latestSnapshot.logicalOldest
        ? latestSnapshot.logicalNewestExclusive
            - latestSnapshot.logicalOldest
        : 0u;

    if (grainCount == 0 || historyLength == 0 || bounds.isEmpty())
        return;

    const auto oldest = static_cast<double>(latestSnapshot.logicalOldest);
    const auto length = static_cast<double>(historyLength);

    for (std::size_t index = 0; index < grainCount; ++index)
    {
        const auto& grain = latestSnapshot.grains[index];

        if (!std::isfinite(grain.readPosition)
            || !std::isfinite(grain.playbackRate))
        {
            continue;
        }

        const auto normalised = juce::jlimit(
            0.0,
            1.0,
            (grain.readPosition - oldest) / length
        );
        const auto x = bounds.getX()
            + static_cast<float>(normalised) * bounds.getWidth();
        const auto markerColour = grain.playbackRate < 0.0 ? pink : teal;
        const auto markerY = bounds.getY()
            + 4.0f
            + static_cast<float>(index % 4u) * 3.0f;

        graphics.setColour(markerColour.withAlpha(0.22f));
        graphics.drawVerticalLine(
            juce::roundToInt(x),
            bounds.getY(),
            bounds.getBottom()
        );
        graphics.setColour(markerColour);
        graphics.fillEllipse(x - 2.5f, markerY, 5.0f, 5.0f);
    }
}
