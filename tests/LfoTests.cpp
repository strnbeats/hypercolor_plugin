#include "dsp/modulation/Lfo.h"

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
