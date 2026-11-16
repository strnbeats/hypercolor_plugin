#include "dsp/granular/GranularCharacter.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>

namespace
{
using hypercolor::dsp::granular::GranularCharacter;
using hypercolor::dsp::granular::GranularCharacterProfile;
using hypercolor::dsp::granular::getGranularCharacterProfile;

class TestContext
{
public:
    void expect(bool condition, const char* message)
    {
        if (condition)
            return;

        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }

    [[nodiscard]] int getFailures() const noexcept { return failures; }

private:
    int failures = 0;
};

bool isValidProfile(const GranularCharacterProfile& profile) noexcept
{
    const std::array values {
        profile.pitchRandomScale,
        profile.positionRandomScale,
        profile.timingJitterScale,
        profile.stereoSpreadScale,
        profile.panRandomScale
    };

    for (const auto value : values)
    {
        if (!std::isfinite(value) || value < 0.0f || value > 1.0f)
            return false;
    }

    return true;
}

void testProfiles(TestContext& context)
{
    const auto& chaos = getGranularCharacterProfile(
        GranularCharacter::ControlledChaos
    );
    const auto& musical = getGranularCharacterProfile(GranularCharacter::Musical);
    const auto& atmospheric = getGranularCharacterProfile(
        GranularCharacter::Atmospheric
    );

    context.expect(isValidProfile(chaos), "ControlledChaos profile should be valid");
    context.expect(isValidProfile(musical), "Musical profile should be valid");
    context.expect(isValidProfile(atmospheric), "Atmospheric profile should be valid");

    context.expect(chaos.pitchRandomScale >= 0.8f, "Chaos pitch should be high");
    context.expect(chaos.positionRandomScale >= 0.8f, "Chaos position should be high");
    context.expect(chaos.timingJitterScale >= 0.8f, "Chaos timing should be high");
    context.expect(chaos.stereoSpreadScale >= 0.8f, "Chaos spread should be high");
    context.expect(chaos.panRandomScale >= 0.8f, "Chaos pan should be high");

    context.expect(
        musical.timingJitterScale < chaos.timingJitterScale,
        "Musical timing jitter should be below ControlledChaos"
    );
    context.expect(
        musical.pitchRandomScale > 0.0f && musical.pitchRandomScale < 0.8f,
        "Musical pitch variation should be moderate"
    );
    context.expect(
        atmospheric.stereoSpreadScale >= 0.8f,
        "Atmospheric stereo spread should be high"
    );
    context.expect(
        atmospheric.timingJitterScale < musical.timingJitterScale,
        "Atmospheric timing jitter should be minimal"
    );

    context.expect(
        &getGranularCharacterProfile(GranularCharacter::Musical) == &musical,
        "Repeated profile lookup should be stable"
    );

    const auto invalidCharacter = static_cast<GranularCharacter>(255);
    context.expect(
        &getGranularCharacterProfile(invalidCharacter) == &musical,
        "Unknown character should fall back to Musical"
    );
}
}

int main()
{
    TestContext context;
    testProfiles(context);

    if (context.getFailures() == 0)
    {
        std::cout << "All GranularCharacter tests passed.\n";
        return 0;
    }

    std::cerr << context.getFailures() << " test(s) failed.\n";
    return 1;
}
