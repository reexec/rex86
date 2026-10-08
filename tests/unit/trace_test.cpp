// The trace format's round trip and a replayed case (design #22,
// decision 5).

#include <cstdint>
#include <vector>

#include "rex86/environment.h"
#include "rex86/guest_memory.h"
#include "test_support.h"
#include "trace/replay.h"
#include "trace/trace_format.h"

namespace
{

namespace trace = rex86::trace;

// ADD [ebx], eax at 0x1000 with EBX pointing into a data page.
trace::Case AddCase()
{
    trace::Case c;
    c.flags = trace::kCompareEip;
    c.features = trace::kFeatureX87 | trace::kFeatureCmov;
    c.regions = {{0x1000, 0x1000, static_cast<std::uint8_t>(rex86::kPageReadWriteExecute)},
                 {0x2000, 0x1000, static_cast<std::uint8_t>(rex86::kPageReadWrite)}};
    c.fill_seed = 0x1234u;
    c.patches = {{0x1000, {0x01, 0x03}}, {0x2010, {0x05, 0x00, 0x00, 0x00}}};
    c.selectors = {0x2B, 0x23, 0x2B, 0x2B, 0, 0x63};
    c.segment_present = 0x2F;
    c.input.gpr = {7, 0, 0, 0x2010, 0x2800, 0, 0, 0};
    c.input.eip = 0x1000;
    c.input.eflags = 0x202;
    c.reason = static_cast<std::uint8_t>(rex86::StopReason::kBudgetExhausted);
    c.expected.gpr = c.input.gpr;
    c.expected.eip = 0x1002;
    c.expected.eflags = 0x202 | (1u << 2);  // 7 + 5 = 12: PF
    c.diffs = {{0x2010, {0x0C}}};
    c.ignores = {{0x2F00, 16}};
    return c;
}

}  // namespace

void RunTraceTests(rex86::test::Context& context)
{
    // FillRegion is deterministic and depends on the base.
    {
        std::uint8_t a[16];
        std::uint8_t b[16];
        std::uint8_t c[16];
        trace::FillRegion(1, 0x1000, a, sizeof a);
        trace::FillRegion(1, 0x1000, b, sizeof b);
        trace::FillRegion(1, 0x2000, c, sizeof c);
        REX86_CHECK(context, std::vector<std::uint8_t>(a, a + 16) == std::vector<std::uint8_t>(b, b + 16));
        REX86_CHECK(context, std::vector<std::uint8_t>(a, a + 16) != std::vector<std::uint8_t>(c, c + 16));
    }
    // Encode then decode gives the same case back.
    {
        const trace::Case original = AddCase();
        std::vector<std::uint8_t> bytes;
        trace::EncodeCase(original, &bytes);
        trace::Case decoded;
        std::size_t consumed = 0;
        std::string error;
        REX86_CHECK(context, trace::DecodeCase(bytes.data(), bytes.size(), &decoded, &consumed, &error));
        REX86_CHECK_EQ(context, consumed, bytes.size());
        std::vector<std::uint8_t> again;
        trace::EncodeCase(decoded, &again);
        REX86_CHECK(context, again == bytes);
        REX86_CHECK_EQ(context, decoded.expected.eip, 0x1002u);
        REX86_CHECK_EQ(context, decoded.diffs.size(), std::size_t{1});
        // A truncated case is refused.
        REX86_CHECK(context, !trace::DecodeCase(bytes.data(), bytes.size() - 1, &decoded, &consumed, &error));
    }
    // Replay matches the right expectation and reports a wrong one.
    {
        trace::Case c = AddCase();
        const trace::ReplayResult good = trace::Replay(c);
        REX86_CHECK(context, good.matched);
        if (!good.matched)
        {
            context.Fail(good.difference, __FILE__, __LINE__);
        }
        c.diffs[0].bytes[0] = 0x0D;
        REX86_CHECK(context, !trace::Replay(c).matched);
        c = AddCase();
        c.expected.eflags ^= 1u;  // CF
        REX86_CHECK(context, !trace::Replay(c).matched);
        c.eflags_mask &= ~1u;
        REX86_CHECK(context, trace::Replay(c).matched);
    }
}
