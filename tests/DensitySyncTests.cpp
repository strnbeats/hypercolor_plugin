#include "plugin/DensitySync.h"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <type_traits>

namespace
{
using hypercolor::density::Division;
using hypercolor::density::bpmToGrainsPerSecond;
using hypercolor::density::getDivisionMultiplier;

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

    void expectNear(double actual, double expected, const char* message)
    {
        expect(
            std::isfinite(actual) && std::abs(actual - expected) <= 1.0e-12,
            message
        );
    }

    [[nodiscard]] int getFailures() const noexcept { return failures; }

private:
    int failures = 0;
};

constexpr std::array divisions {
    std::pair { Division::quarter, 1.0 },
    std::pair { Division::quarterTriplet, 1.5 },
    std::pair { Division::eighth, 2.0 },
    std::pair { Division::eighthTriplet, 3.0 },
    std::pair { Division::sixteenth, 4.0 },
    std::pair { Division::sixteenthTriplet, 6.0 },
    std::pair { Division::thirtySecond, 8.0 },
    std::pair { Division::thirtySecondTriplet, 12.0 },
    std::pair { Division::sixtyFourth, 16.0 },
    std::pair { Division::sixtyFourthTriplet, 24.0 }
};

void testDivisionMultipliers(TestContext& context)
{
    for (const auto [division, multiplier] : divisions)
        context.expectNear(
            getDivisionMultiplier(division),
            multiplier,
            "Each division should expose its contract multiplier"
        );

    const auto invalid = static_cast<Division>(255);
    context.expectNear(
        getDivisionMultiplier(invalid),
        1.0,
        "An invalid division should use a safe quarter-note fallback"
    );
}

void testBpmConversion(TestContext& context)
{
    constexpr std::array bpms { 60.0, 90.0, 120.0, 140.0, 180.0, 240.0 };

    for (const auto bpm : bpms)
    {
        for (const auto [division, multiplier] : divisions)
        {
            context.expectNear(
                bpmToGrainsPerSecond( bpm, division),
                (bpm / 60.0) * multiplier,
                "BPM conversion should follow the documented formula"
            );
        }
    }
}

void testBpmBoundsAndResultSafety(TestContext& context)
{
    for (const auto [division, multiplier] : divisions)
    {
        context.expectNear(
            bpmToGrainsPerSecond(20.0, division),
            (20.0 / 60.0) * multiplier,
            "The lower valid BPM boundary should be accepted"
        );

        const auto maximumResult = bpmToGrainsPerSecond(300.0, division);
        context.expectNear(
            maximumResult,
            (300.0 / 60.0) * multiplier,
            "The upper valid BPM boundary should be accepted"
        );
        context.expect(
            maximumResult >= 0.0 && maximumResult <= 120.0,
            "Results should remain inside the documented 0-120 range"
        );

        // Values above the supported BPM range are constrained before the
        // conversion, so they cannot produce an oversized result.
        context.expectNear(
            bpmToGrainsPerSecond(100000.0, division),
            maximumResult,
            "BPM above the supported range should be clamped"
        );
    }
}

void testInvalidBpm(TestContext& context)
{
    constexpr std::array invalidBpms {
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
        0.0,
        -1.0,
        -120.0
    };

    for (const auto bpm : invalidBpms)
    {
        for (const auto [division, unusedMultiplier] : divisions)
        {
            static_cast<void>(unusedMultiplier);
            const auto result = bpmToGrainsPerSecond(bpm, division);
            context.expectNear(
                result,
                0.0,
                "Invalid BPM should produce a finite zero result"
            );
        }
    }

    const auto invalidDivisionResult = bpmToGrainsPerSecond(
        120.0,
        static_cast<Division>(255)
    );
    context.expectNear(
        invalidDivisionResult,
        2.0,
        "An invalid division should remain safe during BPM conversion"
    );
    context.expect(
        std::isfinite(invalidDivisionResult),
        "Invalid division conversion should never produce NaN or infinity"
    );
}
}

int main()
{
    static_assert(std::is_enum_v<Division>);
    TestContext context;
    testDivisionMultipliers(context);
    testBpmConversion(context);
    testBpmBoundsAndResultSafety(context);
    testInvalidBpm(context);

    if (context.getFailures() == 0)
    {
        std::cout << "All DensitySync tests passed.\n";
        return 0;
    }

    std::cerr << context.getFailures() << " test(s) failed.\n";
    return 1;
}
