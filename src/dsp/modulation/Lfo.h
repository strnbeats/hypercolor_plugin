#pragma once

#include <array>
#include <cstdint>

namespace hypercolor::dsp::modulation
{
enum class LfoWaveform : std::uint8_t
{
    sine, triangle, sawUp, sawDown, square, sampleAndHold, custom
};

class Lfo final
{
public:
    static constexpr double maximumFrequencyHz = 80.0;
    static constexpr std::size_t customWaveformSize = 256;
    using CustomWaveform = std::array<float, customWaveformSize>;
    Lfo() noexcept { reset(); }
    [[nodiscard]] bool prepare(double sampleRate) noexcept;
    void reset(std::uint32_t seed = 0x9e3779b9u) noexcept;
    void setFrequency(double frequencyHz) noexcept;
    void setPhase(double normalizedPhase) noexcept;
    [[nodiscard]] float nextSample(LfoWaveform waveform,
                                   float phaseOffset = 0.0f,
                                   const CustomWaveform* custom = nullptr) noexcept;
    [[nodiscard]] double getPhase() const noexcept { return phase; }
    [[nodiscard]] double getFrequency() const noexcept { return frequencyHz; }

    [[nodiscard]] static float evaluate(LfoWaveform waveform,
                                        float normalizedPhase,
                                        float heldValue = 0.0f,
                                        const CustomWaveform* custom = nullptr) noexcept;
    [[nodiscard]] static CustomWaveform makeDefaultCustomWaveform() noexcept;
    static void prepareCustomWaveform(const CustomWaveform& source, float smooth,
                                      CustomWaveform& destination) noexcept;
    static void drawCustomSegment(CustomWaveform& waveform, float x0, float y0,
                                  float x1, float y1) noexcept;
    static void clearCustomWaveform(CustomWaveform& waveform) noexcept;

private:
    [[nodiscard]] float randomBipolar() noexcept;

    double sampleRate = 44100.0;
    double frequencyHz = 1.0;
    double phase = 0.0;
    std::uint32_t randomState = 0x9e3779b9u;
    float heldValue = 0.0f;
    bool prepared = false;
};
}
