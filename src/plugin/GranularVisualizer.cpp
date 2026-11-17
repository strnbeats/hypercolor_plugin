#include "GranularVisualizer.h"
#include "HypercolorLookAndFeel.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace
{
constexpr int visualizerRefreshRateHz = 30;

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

    constexpr float cut = 18.0f;
    juce::Path fieldShape;
    fieldShape.startNewSubPath(bounds.getX(), bounds.getY());
    fieldShape.lineTo(bounds.getRight() - cut, bounds.getY());
    fieldShape.lineTo(bounds.getRight(), bounds.getY() + cut);
    fieldShape.lineTo(bounds.getRight(), bounds.getBottom());
    fieldShape.lineTo(bounds.getX() + cut, bounds.getBottom());
    fieldShape.lineTo(bounds.getX(), bounds.getBottom() - cut);
    fieldShape.closeSubPath();

    graphics.setColour(hypercolor::ui::palette::panel());
    graphics.fillPath(fieldShape);
    graphics.setColour(hypercolor::ui::palette::border());
    graphics.strokePath(fieldShape, juce::PathStrokeType(1.0f));
    graphics.setColour(hypercolor::ui::palette::cyan());
    graphics.fillRect(bounds.getX(), bounds.getY(), bounds.getWidth() * 0.64f, 2.0f);
    graphics.setColour(hypercolor::ui::palette::pink());
    graphics.fillRect(
        bounds.getX() + bounds.getWidth() * 0.64f,
        bounds.getY(),
        bounds.getWidth() * 0.23f,
        2.0f
    );
    graphics.setColour(hypercolor::ui::palette::lime());
    graphics.fillRect(
        bounds.getX() + bounds.getWidth() * 0.87f,
        bounds.getY(),
        bounds.getWidth() * 0.08f,
        2.0f
    );

    auto content = bounds.reduced(12.0f, 6.0f);
    auto header = content.removeFromTop(21.0f);
    content.removeFromTop(2.0f);
    auto footer = content.removeFromBottom(20.0f);
    content.removeFromBottom(3.0f);

    graphics.setFont(juce::FontOptions(11.0f).withStyle("Bold"));
    graphics.setColour(hypercolor::ui::palette::cyan());
    graphics.drawText(
        "00 / GRANULAR FIELD  //  BUFFER HISTORY",
        header,
        juce::Justification::centredLeft,
        false
    );

    graphics.setColour(
        hasSnapshot
            ? hypercolor::ui::palette::text()
            : hypercolor::ui::palette::mutedText()
    );
    const auto grainCount = hasSnapshot
        ? std::min<std::uint32_t>(
            latestSnapshot.activeGrainCount,
            static_cast<std::uint32_t>(latestSnapshot.grains.size())
        )
        : 0u;
    const auto status = hasSnapshot
        ? "SEQ " + juce::String(latestSnapshot.sequence).paddedLeft('0', 6)
            + "  //  " + juce::String(grainCount)
            + (grainCount == 1 ? " GRAIN" : " GRAINS")
        : juce::String("WAITING FOR AUDIO");
    graphics.drawText(
        status,
        header,
        juce::Justification::centredRight,
        false
    );

    // Keep the telemetry plot strictly separate from the header/footer.  In
    // particular, grain markers must never be allowed to paint over the
    // history labels or status text when the component is resized.
    auto plot = content;
    graphics.setColour(hypercolor::ui::palette::background().withAlpha(0.58f));
    graphics.fillRect(plot);

    graphics.setColour(hypercolor::ui::palette::grid());

    for (int division = 0; division <= 8; ++division)
    {
        const auto x = plot.getX()
            + plot.getWidth() * static_cast<float>(division) / 8.0f;
        graphics.drawVerticalLine(
            juce::roundToInt(x),
            plot.getY(),
            plot.getBottom()
        );
    }

    for (int division = 0; division <= 4; ++division)
    {
        const auto y = plot.getY()
            + plot.getHeight() * static_cast<float>(division) / 4.0f;
        graphics.drawHorizontalLine(
            juce::roundToInt(y),
            plot.getX(),
            plot.getRight()
        );
    }

    if (!hasSnapshot)
    {
        graphics.setColour(hypercolor::ui::palette::mutedText());
        graphics.setFont(juce::FontOptions(12.0f).withStyle("Bold"));
        graphics.drawText(
            "AWAITING LIVE AUDIO TELEMETRY",
            plot,
            juce::Justification::centred,
            false
        );
        return;
    }

    graphics.saveState();
    graphics.reduceClipRegion(plot.toNearestInt());
    drawWaveform(graphics, plot);
    drawGrains(graphics, plot);
    graphics.restoreState();

    graphics.setFont(juce::FontOptions(9.0f).withStyle("Bold"));
    graphics.setColour(hypercolor::ui::palette::mutedText());
    graphics.drawText("OLD", footer, juce::Justification::centredLeft, false);
    graphics.drawText("FRESH", footer, juce::Justification::centredRight, false);

    const auto sampleRateKhz = latestSnapshot.sampleRate / 1000.0;
    const auto info = juce::String(latestSnapshot.validDurationSeconds, 2)
        + " s HISTORY  //  " + juce::String(sampleRateKhz, 1) + " kHz  //  "
        + (latestSnapshot.numChannels == 2 ? "STEREO" : "MONO");
    graphics.setColour(hypercolor::ui::palette::lime());
    graphics.drawText(info, footer, juce::Justification::centred, false);
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

        const auto channelColour = channel == 0
            ? hypercolor::ui::palette::cyan()
            : hypercolor::ui::palette::pink();
        juce::Path envelope;

        for (std::size_t point = 0; point < pointCount; ++point)
        {
            const auto& telemetryPoint = latestSnapshot.waveformPoints[point];
            const auto x = pointCount > 1
                ? bounds.getX() + step * static_cast<float>(point)
                : bounds.getCentreX();
            const auto y = sampleToY(
                telemetryPoint.maximum[channel],
                centreY,
                amplitudeHeight
            );

            if (point == 0)
                envelope.startNewSubPath(x, y);
            else
                envelope.lineTo(x, y);
        }

        for (std::size_t point = pointCount; point-- > 0;)
        {
            const auto& telemetryPoint = latestSnapshot.waveformPoints[point];
            const auto x = pointCount > 1
                ? bounds.getX() + step * static_cast<float>(point)
                : bounds.getCentreX();
            envelope.lineTo(
                x,
                sampleToY(
                    telemetryPoint.minimum[channel],
                    centreY,
                    amplitudeHeight
                )
            );
        }

        envelope.closeSubPath();
        graphics.setColour(channelColour.withAlpha(0.13f));
        graphics.fillPath(envelope);
        graphics.setColour(channelColour.withAlpha(0.88f));

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

        graphics.setFont(juce::FontOptions(8.0f).withStyle("Bold"));
        graphics.setColour(channelColour.withAlpha(0.72f));
        graphics.drawText(
            channelCount == 1 ? "M" : (channel == 0 ? "L" : "R"),
            juce::Rectangle<float>(
                bounds.getX() + 5.0f,
                laneTop + 3.0f,
                14.0f,
                12.0f
            ),
            juce::Justification::centred,
            false
        );
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
        const auto markerColour = grain.playbackRate < 0.0
            ? hypercolor::ui::palette::pink()
            : hypercolor::ui::palette::cyan();
        const auto markerY = bounds.getY()
            + 7.0f
            + static_cast<float>(index % 8u) * 4.0f;

        graphics.setColour(markerColour.withAlpha(0.24f));
        graphics.drawVerticalLine(
            juce::roundToInt(x),
            bounds.getY(),
            bounds.getBottom()
        );
        juce::Path directionMarker;
        const auto direction = grain.playbackRate < 0.0 ? -1.0f : 1.0f;
        directionMarker.startNewSubPath(x + direction * 5.0f, markerY + 3.0f);
        directionMarker.lineTo(x - direction * 3.0f, markerY - 1.0f);
        directionMarker.lineTo(x - direction * 3.0f, markerY + 7.0f);
        directionMarker.closeSubPath();
        graphics.setColour(markerColour);
        graphics.fillPath(directionMarker);
    }
}
