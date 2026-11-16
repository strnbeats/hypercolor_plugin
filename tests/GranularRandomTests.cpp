#include "dsp/granular/GranularRandom.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <type_traits>
#include <utility>

namespace
{
using hypercolor::dsp::granular::GranularRandom;

static_assert(noexcept(GranularRandom { 0 }));
static_assert(noexcept(std::declval<GranularRandom&>().reset(0)));
static_assert(noexcept(std::declval<GranularRandom&>().nextUInt32()));
static_assert(noexcept(std::declval<GranularRandom&>().nextNormalized()));
static_assert(noexcept(std::declval<GranularRandom&>().nextSignedNormalized()));
static_assert(std::is_trivially_copyable_v<GranularRandom>);

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

void testDeterminismAndReset(TestContext& context)
{
    GranularRandom first { 123456789ULL };
    GranularRandom second { 123456789ULL };
    GranularRandom different { 987654321ULL };
    bool foundDifference = false;

    std::array<std::uint32_t, 32> sequence {};
    for (auto& value : sequence)
    {
        value = first.nextUInt32();
        context.expect(
            value == second.nextUInt32(),
            "Equal seeds should produce equal sequences"
        );
        foundDifference = foundDifference || value != different.nextUInt32();
    }

    context.expect(foundDifference, "Different seeds should produce different sequences");

    first.reset(123456789ULL);
    for (const auto expected : sequence)
    {
        context.expect(
            first.nextUInt32() == expected,
            "Reset should reproduce the original sequence"
        );
    }
}

void testRangesAndDiversity(TestContext& context)
{
    GranularRandom normalizedGenerator { 0 };
    GranularRandom signedGenerator { 0 };
    GranularRandom integerGenerator { 0 };
    constexpr std::size_t sampleCount = 4096;
    std::array<std::uint32_t, sampleCount> integers {};
    bool anyNonZero = false;

    for (std::size_t index = 0; index < sampleCount; ++index)
    {
        const auto normalized = normalizedGenerator.nextNormalized();
        const auto signedNormalized = signedGenerator.nextSignedNormalized();
        integers[index] = integerGenerator.nextUInt32();
        anyNonZero = anyNonZero || integers[index] != 0;

        context.expect(
            std::isfinite(normalized) && normalized >= 0.0 && normalized < 1.0,
            "Normalized values should be finite and inside [0, 1)"
        );
        context.expect(
            std::isfinite(signedNormalized)
                && signedNormalized >= -1.0
                && signedNormalized < 1.0,
            "Signed values should be finite and inside [-1, 1)"
        );
    }

    std::sort(integers.begin(), integers.end());
    const auto uniqueEnd = std::unique(integers.begin(), integers.end());
    const auto uniqueCount = static_cast<std::size_t>(
        std::distance(integers.begin(), uniqueEnd)
    );

    context.expect(anyNonZero, "Zero seed should not produce a degenerate zero stream");
    context.expect(
        uniqueCount >= sampleCount - 8,
        "A large deterministic sample should contain sufficient diversity"
    );
}
}

int main()
{
    TestContext context;
    testDeterminismAndReset(context);
    testRangesAndDiversity(context);

    if (context.getFailures() == 0)
    {
        std::cout << "All GranularRandom tests passed.\n";
        return 0;
    }

    std::cerr << context.getFailures() << " test(s) failed.\n";
    return 1;
}
