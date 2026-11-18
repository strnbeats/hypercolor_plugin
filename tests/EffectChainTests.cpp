#include "core/ProcessSpec.h"
#include "dsp/fx/EffectChain.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <new>

namespace allocationProbe
{
std::atomic<bool> enabled { false };
std::atomic<std::size_t> count { 0 };
}

void* operator new(std::size_t size)
{
    if (allocationProbe::enabled.load(std::memory_order_relaxed))
        allocationProbe::count.fetch_add(1, std::memory_order_relaxed);

    if (auto* memory = std::malloc(size))
        return memory;

    throw std::bad_alloc {};
}

void* operator new[](std::size_t size)
{
    return ::operator new(size);
}

void operator delete(void* memory) noexcept
{
    std::free(memory);
}

void operator delete[](void* memory) noexcept
{
    ::operator delete(memory);
}

void operator delete(void* memory, std::size_t) noexcept
{
    ::operator delete(memory);
}

void operator delete[](void* memory, std::size_t) noexcept
{
    ::operator delete[](memory);
}

namespace
{
using hypercolor::core::ProcessSpec;
using hypercolor::dsp::fx::EffectAudioBlock;
using hypercolor::dsp::fx::EffectChain;
using hypercolor::dsp::fx::EffectChainResult;
using hypercolor::dsp::fx::EffectModule;

class TestContext
{
public:
    void expect(bool value, const char* message)
    {
        if (!value)
        {
            ++failures;
            std::cerr << "FAIL: " << message << '\n';
        }
    }
    [[nodiscard]] int getFailures() const noexcept { return failures; }
private:
    int failures = 0;
};

class GainModule final : public EffectModule
{
public:
    explicit GainModule(float gainToUse) noexcept : gain(gainToUse) {}

    bool prepare(const ProcessSpec& spec) noexcept override
    {
        prepared = spec.numChannels == 1 || spec.numChannels == 2;
        return prepared;
    }

    void reset() noexcept override { processCalls = 0; }

    void process(EffectAudioBlock& block) noexcept override
    {
        ++processCalls;
        for (std::uint32_t channel = 0; channel < block.numChannels; ++channel)
            for (auto& sample : block.channels[channel])
                sample *= gain;
    }

    int processCalls = 0;
    bool prepared = false;
private:
    float gain;
};

class AffineModule final : public EffectModule
{
public:
    AffineModule(float multiplierToUse, float offsetToUse) noexcept
        : multiplier(multiplierToUse), offset(offsetToUse) {}
    bool prepare(const ProcessSpec&) noexcept override { return true; }
    void reset() noexcept override {}
    void process(EffectAudioBlock& block) noexcept override
    {
        for (std::uint32_t channel = 0; channel < block.numChannels; ++channel)
            for (auto& sample : block.channels[channel])
                sample = sample * multiplier + offset;
    }
private:
    float multiplier;
    float offset;
};

class FailingModule final : public EffectModule
{
public:
    bool prepare(const ProcessSpec&) noexcept override { return false; }
    void reset() noexcept override { ++resetCalls; }
    void process(EffectAudioBlock&) noexcept override { ++processCalls; }
    int resetCalls = 0;
    int processCalls = 0;
};

EffectAudioBlock makeBlock(
    std::array<float, 8>& left,
    std::array<float, 8>& right,
    std::uint32_t channels
)
{
    EffectAudioBlock block;
    block.numChannels = channels;
    block.channels[0] = left;
    block.channels[1] = right;
    return block;
}

void testEmptyAndSequential(TestContext& context)
{
    EffectChain chain;
    context.expect(chain.prepare({ 48000.0, 1024, 2 }), "empty chain prepare");
    context.expect(chain.isPrepared(), "prepared state should be observable");
    std::array left { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f };
    std::array right = left;
    auto block = makeBlock(left, right, 2);
    context.expect(chain.process(block) == EffectChainResult::success, "empty process");
    context.expect(left[0] == 1.0f && right[7] == 8.0f, "empty chain preserves signal");

    EffectChain sequential;
    GainModule first { 2.0f };
    GainModule second { 3.0f };
    context.expect(sequential.addModule(&first), "first module add");
    context.expect(sequential.addModule(&second), "second module add");
    context.expect(sequential.prepare({ 48000.0, 1024, 2 }), "sequential prepare");
    context.expect(!sequential.addModule(&first), "composition is immutable after prepare");
    left.fill(1.0f);
    right.fill(1.0f);
    block = makeBlock(left, right, 2);
    context.expect(sequential.process(block) == EffectChainResult::success, "sequential process");
    context.expect(left[0] == 6.0f && right[7] == 6.0f, "modules execute in order");
}

void testBypassResetAndValidation(TestContext& context)
{
    EffectChain chain;
    GainModule module { 4.0f };
    context.expect(chain.addModule(&module), "bypass module add");
    context.expect(chain.prepare({ 1000.0, 64, 1 }), "mono prepare");
    module.setBypassed(true);
    std::array left { 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f };
    std::array<float, 8> right {};
    auto block = makeBlock(left, right, 1);
    context.expect(chain.process(block) == EffectChainResult::success, "bypass process");
    context.expect(
        left[0] < 8.0f && left[0] > 2.0f && module.processCalls == 1,
        "bypass processes module and starts a smooth transition"
    );

    for (int blockIndex = 0; blockIndex < 3; ++blockIndex)
    {
        left.fill(2.0f);
        context.expect(
            chain.process(block) == EffectChainResult::success,
            "bypass settling process"
        );
    }
    context.expect(
        std::abs(left[0] - 2.0f) < 0.001f && module.processCalls == 4,
        "fully bypassed output should equal dry while module keeps processing"
    );

    module.setBypassed(false);
    chain.reset();
    left.fill(2.0f);
    context.expect(chain.process(block) == EffectChainResult::success, "reset keeps prepared state");
    context.expect(left[0] == 8.0f && module.processCalls == 1, "reset restores enabled state safely");

    EffectChain invalid;
    context.expect(!invalid.prepare({ 0.0, 64, 1 }), "invalid spec rejected");
    context.expect(
        invalid.process(block) == EffectChainResult::invalidConfiguration,
        "unprepared chain rejects processing"
    );
    auto invalidBlock = makeBlock(left, right, 3);
    context.expect(
        chain.process(invalidBlock) == EffectChainResult::invalidConfiguration,
        "invalid channel count rejected"
    );

    EffectChain failing;
    FailingModule failingModule;
    context.expect(failing.addModule(&failingModule), "failing module add");
    context.expect(!failing.prepare({ 44100.0, 64, 1 }), "module prepare failure propagates");
    context.expect(!failing.isPrepared(), "failed prepare leaves chain unprepared");
    context.expect(failingModule.resetCalls == 1, "failed prepare resets modules");
}

void testSmoothTransitionsAndStereo(TestContext& context)
{
    EffectChain chain;
    GainModule module { 2.0f };
    context.expect(chain.addModule(&module), "smooth module add");
    context.expect(chain.prepare({ 1000.0, 8, 2 }), "stereo smooth prepare");

    std::array<float, 8> left;
    std::array<float, 8> right;
    auto block = makeBlock(left, right, 2);

    left.fill(1.0f);
    right.fill(1.0f);
    context.expect(chain.process(block) == EffectChainResult::success, "enabled stereo process");
    context.expect(left[0] == 2.0f && right[0] == 2.0f, "enabled output should be fully wet");

    module.setBypassed(true);
    float previous = left[7];
    for (int blockIndex = 0; blockIndex < 4; ++blockIndex)
    {
        left.fill(1.0f);
        right.fill(1.0f);
        context.expect(chain.process(block) == EffectChainResult::success, "stereo bypass process");
        for (std::size_t sample = 0; sample < left.size(); ++sample)
        {
            context.expect(
                std::isfinite(left[sample]) && std::isfinite(right[sample]),
                "smooth bypass output should remain finite"
            );
            context.expect(
                std::abs(left[sample] - right[sample]) < 0.001f,
                "stereo channels should receive the same smoothed gain"
            );
            context.expect(
                left[sample] <= previous + 0.001f,
                "bypass transition should not jump upward"
            );
            previous = left[sample];
        }
    }
    context.expect(std::abs(left[7] - 1.0f) < 0.001f, "stereo bypass should settle to dry");

    module.setBypassed(false);
    left.fill(1.0f);
    right.fill(1.0f);
    context.expect(
        chain.process(block) == EffectChainResult::success,
        "re-enable settling process"
    );
    context.expect(left[0] > 1.0f && left[0] < 2.0f, "re-enable should ramp from dry");

    // Retargeting several times must remain finite and bounded by dry/wet.
    for (int toggle = 0; toggle < 8; ++toggle)
    {
        module.setBypassed(toggle % 2 == 0);
        left.fill(1.0f);
        right.fill(1.0f);
        context.expect(chain.process(block) == EffectChainResult::success, "rapid retarget process");
        for (const auto sample : left)
            context.expect(
                std::isfinite(sample) && sample >= 0.999f && sample <= 2.001f,
                "rapid bypass retarget should stay within dry/wet bounds"
            );
    }
}

void testDifferentBlockSizes(TestContext& context)
{
    for (const auto blockSize : { std::size_t { 1 }, std::size_t { 7 },
                                  std::size_t { 32 }, std::size_t { 64 } })
    {
        EffectChain chain;
        GainModule module { 1.5f };
        context.expect(chain.addModule(&module), "block-size module add");
        context.expect(
            chain.prepare({
                48000.0,
                static_cast<std::uint32_t>(blockSize),
                1
            }),
            "block-size prepare"
        );

        std::array<float, 64> samples {};
        EffectAudioBlock block;
        block.channels[0] = std::span<float>(samples.data(), blockSize);
        block.numChannels = 1;

        samples.fill(1.0f);
        context.expect(
            chain.process(block) == EffectChainResult::success,
            "block-size enabled process"
        );
        context.expect(
            std::abs(samples[0] - 1.5f) < 0.001f,
            "block-size enabled result"
        );

        module.setBypassed(true);
        const auto blocksToSettle = static_cast<int>(
            (720 + blockSize - 1) / blockSize + 2
        );
        for (int blockIndex = 0; blockIndex < blocksToSettle; ++blockIndex)
        {
            samples.fill(1.0f);
            context.expect(
                chain.process(block) == EffectChainResult::success,
                "block-size bypass process"
            );
        }
        context.expect(
            std::abs(samples[blockSize - 1] - 1.0f) < 0.001f,
            "block-size bypass should settle to dry"
        );
    }
}

void testProcessPathDoesNotAllocate(TestContext& context)
{
    EffectChain chain;
    GainModule module { 1.25f };
    context.expect(chain.addModule(&module), "allocation probe module add");
    context.expect(chain.prepare({ 48000.0, 64, 2 }), "allocation probe prepare");

    std::array<float, 8> left;
    std::array<float, 8> right;
    auto block = makeBlock(left, right, 2);
    allocationProbe::count.store(0, std::memory_order_relaxed);
    allocationProbe::enabled.store(true, std::memory_order_relaxed);
    for (int iteration = 0; iteration < 8; ++iteration)
    {
        left.fill(1.0f);
        right.fill(1.0f);
        context.expect(
            chain.process(block) == EffectChainResult::success,
            "allocation probe process"
        );
    }
    allocationProbe::enabled.store(false, std::memory_order_relaxed);
    context.expect(
        allocationProbe::count.load(std::memory_order_relaxed) == 0,
        "EffectChain process should not allocate"
    );
}

void testCapacityAndOversized(TestContext& context)
{
    EffectChain chain;
    std::array<GainModule, EffectChain::maximumModules> modules {
            GainModule { 1.0f }, GainModule { 1.0f }, GainModule { 1.0f }, GainModule { 1.0f },
            GainModule { 1.0f }, GainModule { 1.0f }, GainModule { 1.0f }, GainModule { 1.0f },
            GainModule { 1.0f }, GainModule { 1.0f }, GainModule { 1.0f }, GainModule { 1.0f },
            GainModule { 1.0f }, GainModule { 1.0f }, GainModule { 1.0f }, GainModule { 1.0f },
            GainModule { 1.0f }, GainModule { 1.0f }, GainModule { 1.0f }, GainModule { 1.0f },
            GainModule { 1.0f }, GainModule { 1.0f }, GainModule { 1.0f }, GainModule { 1.0f }
    };
    for (auto& module : modules)
        context.expect(chain.addModule(&module), "capacity module add");
    GainModule extra { 1.0f };
    context.expect(!chain.addModule(&extra), "full chain rejects module");
    context.expect(chain.prepare({ 96000.0, 4097, 2 }), "oversized prepare");
    std::array left { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
    std::array right = left;
    auto block = makeBlock(left, right, 2);
    context.expect(chain.process(block) == EffectChainResult::success, "oversized-compatible process");
}

void testDynamicOrderAndSmoothConfigurationChange(TestContext& context)
{
    EffectChain chain;
    AffineModule first { 2.0f, 1.0f };
    AffineModule second { 3.0f, 4.0f };
    context.expect(chain.addModule(&first) && chain.addModule(&second), "dynamic order modules register");
    const std::array<std::uint8_t, 2> initialOrder { 0, 1 };
    context.expect(chain.setActiveOrder(initialOrder), "initial order can be set before prepare");
    context.expect(chain.prepare({ 1000.0, 32, 1 }), "dynamic order chain prepares");

    std::array<float, 8> left {};
    std::array<float, 8> right {};
    left.fill(1.0f);
    right.fill(1.0f);
    auto block = makeBlock(left, right, 1);
    context.expect(chain.process(block) == EffectChainResult::success, "initial dynamic order processes");
    context.expect(std::abs(left.back() - 13.0f) < 1.0e-4f, "initial order executes registered sequence");

    const std::array<std::uint8_t, 2> reversedOrder { 1, 0 };
    context.expect(chain.setActiveOrder(reversedOrder), "reorder request accepted");
    left.fill(1.0f);
    block = makeBlock(left, right, 1);
    context.expect(chain.process(block) == EffectChainResult::success, "old chain continues fading during reconfiguration");
    left.fill(1.0f);
    block = makeBlock(left, right, 1);
    context.expect(chain.process(block) == EffectChainResult::success, "old chain reaches the silent swap boundary");
    context.expect(std::abs(left.back()) < 1.0e-4f, "chain reaches silence before order swap");
    left.fill(1.0f);
    block = makeBlock(left, right, 1);
    context.expect(chain.process(block) == EffectChainResult::success, "new order fades back in");
    left.fill(1.0f);
    block = makeBlock(left, right, 1);
    context.expect(chain.process(block) == EffectChainResult::success, "new order completes its fade in");
    context.expect(std::abs(left.back() - 15.0f) < 1.0e-3f, "new order becomes active after safe handoff");
}
}

int main()
{
    TestContext context;
    testEmptyAndSequential(context);
    testBypassResetAndValidation(context);
    testSmoothTransitionsAndStereo(context);
    testDifferentBlockSizes(context);
    testProcessPathDoesNotAllocate(context);
    testCapacityAndOversized(context);
    testDynamicOrderAndSmoothConfigurationChange(context);
    if (context.getFailures() == 0)
    {
        std::cout << "All EffectChain tests passed.\n";
        return 0;
    }
    std::cerr << context.getFailures() << " test(s) failed.\n";
    return 1;
}
