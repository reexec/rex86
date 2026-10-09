#include <cstdint>
#include <string>

#include "test_support.h"
#include "tools/robust/robust_case.h"

void RunRobustTests(rex86::test::Context& context)
{
    using rex86::robust::CaseResult;
    using rex86::robust::Random;

    // A seed makes the same case: same digest twice, and the determinism
    // check passes.
    {
        Random a(42);
        Random b(42);
        const CaseResult first = rex86::robust::RunCase(a);
        const CaseResult second = rex86::robust::RunCase(b);
        REX86_CHECK(context, first.ok);
        REX86_CHECK_EQ(context, first.digest, second.digest);
        const CaseResult twice = rex86::robust::RunCaseTwice([] { return Random(42); });
        REX86_CHECK(context, twice.ok);
        Random c(43);
        REX86_CHECK(context, rex86::robust::RunCase(c).digest != first.digest);
    }
    // libFuzzer bytes drive the generator, then a generator seeded from them.
    {
        const std::uint8_t data[] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
        Random a(data, sizeof data);
        Random b(data, sizeof data);
        REX86_CHECK_EQ(context, a.Bits(8), std::uint64_t{1});
        REX86_CHECK_EQ(context, a.Bits(16), std::uint64_t{0x0302});
        b.Bits(8);
        b.Bits(16);
        for (int i = 0; i < 10; ++i)
        {
            REX86_CHECK_EQ(context, a.Bits(64), b.Bits(64));
        }
        REX86_CHECK(context, a.Below(10) < 10u);
        REX86_CHECK_EQ(context, a.Below(0), 0u);
    }
    // The checkers catch what they are for (design #31, verification):
    // a store into a write-protected page (I3), a write into the guard
    // zone (I2), an event over budget (I5).
    {
        const char* const expected[] = {"I3:", "I2:", "I5:"};
        for (int sabotage = 1; sabotage <= 3; ++sabotage)
        {
            Random random(7);
            const CaseResult result = rex86::robust::RunSabotagedCase(random, sabotage);
            REX86_CHECK(context, !result.ok);
            REX86_CHECK(context, result.failure.rfind(expected[sabotage - 1], 0) == 0);
        }
    }
    // A run of ordinary cases keeps every invariant.
    for (std::uint64_t seed = 1; seed <= 40; ++seed)
    {
        const CaseResult result = rex86::robust::RunCaseTwice([seed] { return Random(seed); });
        if (!result.ok)
        {
            context.Fail("seed " + std::to_string(seed) + ": " + result.failure, __FILE__, __LINE__);
        }
        REX86_CHECK(context, result.ok);
    }
    // The decoder round keeps its contract.
    {
        Random random(99);
        std::string failure;
        bool ok = true;
        for (int i = 0; i < 2000 && ok; ++i)
        {
            ok = rex86::robust::FuzzDecoder(random, &failure);
        }
        REX86_CHECK(context, ok);
    }
}
