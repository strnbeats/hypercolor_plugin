#include "dsp/modulation/Lfo.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>

using hypercolor::dsp::modulation::Lfo;
using hypercolor::dsp::modulation::LfoWaveform;

int main()
{
    int failures = 0;
    const auto expect = [&failures](bool condition, const char* message)
    {
        if (!condition)
        {
            ++failures;
            std::cerr << "FAIL: " << message << '\n';
        }
    };

    expect(std::abs(Lfo::evaluate(LfoWaveform::sine, 0.25f) - 1.0f) < 1.0e-6f,
           "sine quarter-cycle peak");
    expect(std::abs(Lfo::evaluate(LfoWaveform::triangle, 0.25f)) < 1.0e-6f,
           "triangle quarter-cycle zero crossing");
    expect(std::abs(Lfo::evaluate(LfoWaveform::sawUp, 0.0f) + 1.0f) < 1.0e-7f,
           "saw up starts at negative endpoint");
    expect(std::abs(Lfo::evaluate(LfoWaveform::sawDown, 0.0f) - 1.0f) < 1.0e-7f,
           "saw down starts at positive endpoint");
    expect(std::abs(Lfo::evaluate(LfoWaveform::square, 0.49f) - 1.0f) < 1.0e-7f
               && std::abs(Lfo::evaluate(LfoWaveform::square, 0.5f) + 1.0f) < 1.0e-7f,
           "square transition is deterministic at half-cycle");
    expect(std::abs(Lfo::evaluate(LfoWaveform::sampleAndHold, 0.2f, -0.35f) + 0.35f) < 1.0e-7f,
           "sample and hold returns its held value");

    Lfo::CustomWaveform custom {};
    custom[0] = -1.0f; custom[64] = -0.25f; custom[128] = 0.0f;
    custom[192] = 0.75f; custom.back() = custom.front();
    expect(std::abs(Lfo::evaluate(LfoWaveform::custom, 64.0f / 255.0f, 0.0f, &custom)
                    + 0.25f) < 1.0e-6f,
           "custom waveform reads the selected table sample");
    custom[10] = -0.5f; custom[11] = 0.5f;
    expect(std::abs(Lfo::evaluate(LfoWaveform::custom, 10.5f / 255.0f, 0.0f, &custom)) < 1.0e-6f,
           "custom waveform linearly interpolates adjacent samples");
    expect(std::abs(Lfo::evaluate(LfoWaveform::custom, 0.0f, 0.0f, &custom)
                    - Lfo::evaluate(LfoWaveform::custom, 1.0f, 0.0f, &custom)) < 1.0e-6f,
           "custom waveform wraps continuously at the loop boundary");

    Lfo::CustomWaveform drawn {};
    Lfo::drawCustomSegment(drawn, -1.0f, -2.0f, 0.5f, 0.5f);
    expect(std::abs(drawn.front() + 1.0f) < 1.0e-7f
               && drawn[64] > -0.3f && drawn[64] < -0.2f
               && std::abs(drawn[128] - 0.5f) < 1.0e-6f,
           "drawing clamps coordinates and fills skipped points between mouse events");
    expect(std::abs(drawn.back() - drawn.front()) < 1.0e-7f,
           "drawing keeps the stored cycle seam closed");
    auto raw = Lfo::makeDefaultCustomWaveform();
    raw[40] = 1.0f; raw[41] = -1.0f; raw.back() = raw.front();
    Lfo::CustomWaveform sharp {}, smoothed {};
    Lfo::prepareCustomWaveform(raw, 0.0f, sharp);
    Lfo::prepareCustomWaveform(raw, 1.0f, smoothed);
    expect(std::abs(smoothed[40] - smoothed[41]) < std::abs(sharp[40] - sharp[41]),
           "smooth preparation reduces sharp sample transitions without audio-thread work");
    expect(std::abs(smoothed[40]) > 0.2f,
           "maximum smooth preserves meaningful waveform amplitude");
    Lfo::clearCustomWaveform(drawn);
    expect(std::all_of(drawn.begin(), drawn.end(), [](float value) { return value == 0.0f; }),
           "clear produces a flat zero waveform");

    Lfo customOscillator;
    expect(customOscillator.prepare(100.0), "custom oscillator prepares");
    customOscillator.setFrequency(25.0);
    customOscillator.setPhase(64.0 / 255.0);
    expect(std::abs(customOscillator.nextSample(LfoWaveform::custom, 0.0f, &custom) + 0.25f) < 1.0e-5f,
           "custom waveform is consumed by the regular LFO phase/rate path");

    constexpr std::array<double, 4> rates { 44100.0, 48000.0, 88200.0, 96000.0 };
    for (const auto rate : rates)
    {
        Lfo oscillator;
        expect(oscillator.prepare(rate), "valid sample rate prepares");
        oscillator.setFrequency(1.0);
        for (int i = 0; i < static_cast<int>(rate); ++i)
            (void) oscillator.nextSample(LfoWaveform::sine);
        expect(std::abs(oscillator.getPhase()) < 1.0e-9
                   || std::abs(oscillator.getPhase() - 1.0) < 1.0e-9,
               "one second at 1 Hz completes one cycle at each rate");
        oscillator.setFrequency(1000.0);
        expect(std::abs(oscillator.getFrequency() - Lfo::maximumFrequencyHz) < 1.0e-9,
               "frequency is bounded to the supported range");
    }

    Lfo randomA, randomB;
    randomA.reset(1234);
    randomB.reset(1234);
    expect(std::abs(randomA.nextSample(LfoWaveform::sampleAndHold)
               - randomB.nextSample(LfoWaveform::sampleAndHold)) < 1.0e-7f,
           "sample and hold is deterministic for a fixed seed");

    if (failures == 0)
    {
        std::cout << "All LFO tests passed.\n";
        return 0;
    }
    return 1;
}
