#include "core/ProcessSpec.h"
#include "dsp/fx/EffectChain.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>

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
    context.expect(chain.prepare({ 44100.0, 64, 1 }), "mono prepare");
    module.setBypassed(true);
    std::array left { 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f };
    std::array<float, 8> right {};
    auto block = makeBlock(left, right, 1);
    context.expect(chain.process(block) == EffectChainResult::success, "bypass process");
    context.expect(left[0] == 2.0f && module.processCalls == 0, "bypass skips module");
    module.setBypassed(false);
    chain.reset();
    context.expect(chain.process(block) == EffectChainResult::success, "reset keeps prepared state");
    context.expect(left[0] == 8.0f && module.processCalls == 1, "reset is safe");

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

void testCapacityAndOversized(TestContext& context)
{
    EffectChain chain;
    std::array<GainModule, EffectChain::maximumModules> modules {
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
}

int main()
{
    TestContext context;
    testEmptyAndSequential(context);
    testBypassResetAndValidation(context);
    testCapacityAndOversized(context);
    if (context.getFailures() == 0)
    {
        std::cout << "All EffectChain tests passed.\n";
        return 0;
    }
    std::cerr << context.getFailures() << " test(s) failed.\n";
    return 1;
}
