#include <cstdint>

#include "simd_test_machine.h"
#include "test_support.h"

namespace
{

using rex86::FaultKind;
using rex86::Gpr;
using rex86::StopReason;
using rex86::test::SimdMachine;

std::uint64_t Pair(std::uint32_t low, std::uint32_t high)
{
    return static_cast<std::uint64_t>(low) | (static_cast<std::uint64_t>(high) << 32);
}

// Runs `op xmm0, xmm1` and returns the machine for inspection.
struct Run
{
    SimdMachine m;

    Run(std::initializer_list<std::uint8_t> code, std::uint64_t a_low, std::uint64_t a_high,
        std::uint64_t b_low, std::uint64_t b_high, std::uint32_t mxcsr = 0x1F80)
        : m(code)
    {
        m.SetXmm(0, a_low, a_high);
        m.SetXmm(1, b_low, b_high);
        m.cpu.state().sse.mxcsr = mxcsr;
        event = m.Run();
    }

    rex86::Event event;

    std::uint32_t Lane(unsigned lane) const
    {
        return static_cast<std::uint32_t>(m.XmmHalf(0, lane / 2) >> (32 * (lane % 2)));
    }

    std::uint32_t Flags() const
    {
        return m.cpu.state().sse.mxcsr & 0x3Fu;
    }
};

void NaNAndFlagTests(rex86::test::Context& context)
{
    {
        // addps: QNaN+QNaN gives the first; QNaN+SNaN the first (QNaN);
        // SNaN+QNaN the first, quieted; den+QNaN the QNaN, no #D.
        Run r({0x0F, 0x58, 0xC1}, Pair(0x7FC00001, 0x7FC00001), Pair(0xFF800001, 0x00000001),
              Pair(0x7FC00002, 0x7F800002), Pair(0x7FC00003, 0x7FC00004));
        REX86_CHECK_EQ(context, r.Lane(0), 0x7FC00001u);
        REX86_CHECK_EQ(context, r.Lane(1), 0x7FC00001u);
        REX86_CHECK_EQ(context, r.Lane(2), 0xFFC00001u);
        REX86_CHECK_EQ(context, r.Lane(3), 0x7FC00004u);
        REX86_CHECK_EQ(context, r.Flags(), 0x01u);
    }
    {
        // Tininess before rounding: 0x007FFFFF.8 rounds to the smallest
        // normal and still raises #U with #P.
        Run r({0x0F, 0x59, 0xC1}, Pair(0x00FFFFFF, 0), 0, Pair(0x3F000000, 0), 0);
        REX86_CHECK_EQ(context, r.Lane(0), 0x00800000u);
        REX86_CHECK_EQ(context, r.Flags(), 0x30u);
    }
    {
        // An exact tiny result with #U masked raises nothing; with FTZ it is
        // flushed with #U and #P (and #D for the denormal operands).
        Run exact({0x0F, 0x58, 0xC1}, Pair(0x00000001, 0), 0, Pair(0x00000001, 0), 0);
        REX86_CHECK_EQ(context, exact.Lane(0), 0x00000002u);
        REX86_CHECK_EQ(context, exact.Flags(), 0x02u);
        Run ftz({0x0F, 0x58, 0xC1}, Pair(0x00000001, 0), 0, Pair(0x00000001, 0), 0, 0x9F80);
        REX86_CHECK_EQ(context, ftz.Lane(0), 0u);
        REX86_CHECK_EQ(context, ftz.Flags(), 0x32u);
    }
    {
        // A denormal over zero is #Z alone; sqrt of a negative denormal is
        // #I alone (precedence over #D).
        Run div({0x0F, 0x5E, 0xC1}, Pair(0x00000001, 0x3F800000), Pair(0x3F800000, 0x3F800000),
                Pair(0, 0x3F800000), Pair(0x3F800000, 0x3F800000));
        REX86_CHECK_EQ(context, div.Lane(0), 0x7F800000u);
        REX86_CHECK_EQ(context, div.Flags(), 0x04u);
        Run sqrt({0xF3, 0x0F, 0x51, 0xC1}, 0, 0, Pair(0x80000001, 0), 0);
        REX86_CHECK_EQ(context, sqrt.Lane(0), 0xFFC00000u);
        REX86_CHECK_EQ(context, sqrt.Flags(), 0x01u);
    }
}

void UnmaskedTests(rex86::test::Context& context)
{
    {
        // #O unmasked: lane0 inexact, lane1 denormal, lane3 overflow. #XM
        // with #D, #O and #P set and nothing written.
        Run r({0x0F, 0x58, 0xC1}, Pair(0x3F800000, 0x00000001), Pair(0x3F800000, 0x7F7FFFFF),
              Pair(0x33800000, 0x3F800000), Pair(0x3F800000, 0x7F7FFFFF), 0x1B80);
        REX86_CHECK(context, r.event.fault_kind == FaultKind::kSimdFloatingPoint);
        REX86_CHECK_EQ(context, r.Lane(0), 0x3F800000u);
        REX86_CHECK_EQ(context, r.Lane(3), 0x7F7FFFFFu);
        REX86_CHECK_EQ(context, r.Flags(), 0x2Au);
        REX86_CHECK_EQ(context, r.m.cpu.state().eip, SimdMachine::kCode);
    }
    {
        // #I unmasked: the pre-computation flags alone (#I from the SNaN,
        // #D from the denormal); the overflow lane is never computed.
        Run r({0x0F, 0x58, 0xC1}, Pair(0x7F800001, 0x00000001), Pair(0x3F800000, 0x7F7FFFFF),
              Pair(0x3F800000, 0x3F800000), Pair(0x33800000, 0x7F7FFFFF), 0x1F00);
        REX86_CHECK(context, r.event.fault_kind == FaultKind::kSimdFloatingPoint);
        REX86_CHECK_EQ(context, r.Flags(), 0x03u);
    }
    {
        // An exact tiny result with #U unmasked is #XM.
        Run r({0x0F, 0x59, 0xC1}, Pair(0x00800000, 0), 0, Pair(0x3F000000, 0), 0, 0x1780);
        REX86_CHECK(context, r.event.fault_kind == FaultKind::kSimdFloatingPoint);
        REX86_CHECK_EQ(context, r.Flags(), 0x10u);
    }
    {
        // A scalar instruction looks at lane 0 alone: SNaNs above it raise
        // nothing even with #I unmasked, and they are kept.
        Run r({0xF3, 0x0F, 0x58, 0xC1}, Pair(0x3F800000, 0x7F800001), Pair(0x7F800001, 0x7F800001),
              Pair(0x3F800000, 0x7F800001), Pair(0x7F800001, 0x7F800001), 0x1F00);
        REX86_CHECK(context, r.event.reason == StopReason::kHalted);
        REX86_CHECK_EQ(context, r.Lane(0), 0x40000000u);
        REX86_CHECK_EQ(context, r.Lane(1), 0x7F800001u);
    }
    {
        // A packed memory operand must be aligned.
        SimdMachine m({0x0F, 0x58, 0x43, 0x08});  // addps xmm0, [ebx+8]
        REX86_CHECK(context, m.Run().fault_kind == FaultKind::kGeneralProtection);
    }
}

void MinMaxCompareTests(rex86::test::Context& context)
{
    {
        // maxps: a NaN on either side or two zeros give the second operand.
        Run r({0x0F, 0x5F, 0xC1}, Pair(0x7FC00001, 0x3F800000), Pair(0x80000000, 0x00000000),
              Pair(0x3F800000, 0x7FC00002), Pair(0x00000000, 0x80000000));
        REX86_CHECK_EQ(context, r.Lane(0), 0x3F800000u);
        REX86_CHECK_EQ(context, r.Lane(1), 0x7FC00002u);
        REX86_CHECK_EQ(context, r.Lane(2), 0x00000000u);
        REX86_CHECK_EQ(context, r.Lane(3), 0x80000000u);
        REX86_CHECK_EQ(context, r.Flags(), 0x01u);
    }
    {
        // cmpltps: QNaN signals #I; the masks.
        Run r({0x0F, 0xC2, 0xC1, 0x01}, Pair(0x7FC00000, 0x00000001), Pair(0x3F800000, 0x40000000),
              Pair(0x3F800000, 0x3F800000), Pair(0x00000001, 0x40000000));
        REX86_CHECK_EQ(context, r.Lane(0), 0u);
        REX86_CHECK_EQ(context, r.Lane(1), 0xFFFFFFFFu);
        REX86_CHECK_EQ(context, r.Lane(2), 0u);
        REX86_CHECK_EQ(context, r.Lane(3), 0u);
        REX86_CHECK_EQ(context, r.Flags(), 0x03u);
    }
    {
        // comiss: less than sets CF and clears OF, SF, AF; unordered sets
        // ZF, PF, CF. UCOMISS raises nothing for a QNaN, COMISS #I.
        Run lt({0x0F, 0x2F, 0xC1}, Pair(0x3F800000, 0), 0, Pair(0x40000000, 0), 0);
        REX86_CHECK_EQ(context, lt.m.cpu.state().eflags & 0x8D5u, 0x001u);
        Run uq({0x0F, 0x2E, 0xC1}, Pair(0x7FC00000, 0), 0, Pair(0x40000000, 0), 0);
        REX86_CHECK_EQ(context, uq.m.cpu.state().eflags & 0x8D5u, 0x045u);
        REX86_CHECK_EQ(context, uq.Flags(), 0u);
        Run cq({0x0F, 0x2F, 0xC1}, Pair(0x7FC00000, 0), 0, Pair(0x40000000, 0), 0);
        REX86_CHECK_EQ(context, cq.Flags(), 0x01u);
    }
}

void ConversionTests(rex86::test::Context& context)
{
    {
        // cvtss2si eax, xmm1: NaN and out of range give 0x80000000 with #I;
        // rounding follows MXCSR.RC, CVTT truncates.
        const auto convert = [](std::initializer_list<std::uint8_t> code, std::uint32_t value,
                                std::uint32_t mxcsr) {
            SimdMachine m(code);
            m.SetXmm(1, value, 0);
            m.cpu.state().sse.mxcsr = mxcsr;
            m.Run();
            return m.cpu.state().Get(Gpr::kEax);
        };
        REX86_CHECK_EQ(context, convert({0xF3, 0x0F, 0x2D, 0xC1}, 0x7FC00000, 0x1F80), 0x80000000u);
        REX86_CHECK_EQ(context, convert({0xF3, 0x0F, 0x2D, 0xC1}, 0x4F000000, 0x1F80), 0x80000000u);
        REX86_CHECK_EQ(context, convert({0xF3, 0x0F, 0x2D, 0xC1}, 0x40200000, 0x1F80), 2u);   // 2.5
        REX86_CHECK_EQ(context, convert({0xF3, 0x0F, 0x2D, 0xC1}, 0x3FC00000, 0x3F80), 1u);   // 1.5 down
        REX86_CHECK_EQ(context, convert({0xF3, 0x0F, 0x2D, 0xC1}, 0xBFC00000, 0x5F80), 0xFFFFFFFFu);  // up
        REX86_CHECK_EQ(context, convert({0xF3, 0x0F, 0x2C, 0xC1}, 0xBFF00000, 0x1F80), 0xFFFFFFFFu);  // trunc -1.875
    }
    {
        // cvtpi2ps xmm0, mm1 is an MMX instruction (TOP 0, tags valid);
        // from memory it is not.
        SimdMachine m({0xD9, 0xE8, 0x0F, 0x2A, 0xC1});  // fld1; cvtpi2ps xmm0, mm1
        m.SetMm(1, Pair(3, 0xFFFFFFFF));
        m.Run();
        REX86_CHECK_EQ(context, m.XmmHalf(0, 0), Pair(0x40400000, 0xBF800000));
        REX86_CHECK_EQ(context, m.cpu.state().x87.tag_word, std::uint16_t{0});
        SimdMachine n({0xD9, 0xE8, 0x0F, 0x2A, 0x03});  // fld1; cvtpi2ps xmm0, [ebx]
        n.Put64(SimdMachine::kData, Pair(3, 0xFFFFFFFF));
        n.Run();
        REX86_CHECK_EQ(context, n.XmmHalf(0, 0), Pair(0x40400000, 0xBF800000));
        REX86_CHECK_EQ(context, n.cpu.state().x87.tag_word, std::uint16_t{0x3FFF});
    }
    {
        // cvtps2pi mm0, xmm1 writes MM0 with the MMX state.
        SimdMachine m({0x0F, 0x2D, 0xC1});
        m.SetXmm(1, Pair(0x40400000, 0xC0000000), 0);
        m.Run();
        REX86_CHECK_EQ(context, m.Mm(0), Pair(3, 0xFFFFFFFE));
        REX86_CHECK_EQ(context, m.cpu.state().x87.registers[0][9], std::uint8_t{0xFF});
    }
}

void ApproximationTests(rex86::test::Context& context)
{
    // The core's RCP/RSQRT model: the true value rounded; special values as
    // measured; no flags.
    Run rcp({0x0F, 0x53, 0xC1}, 0, 0, Pair(0x3F800000, 0x7E800000), Pair(0x00000001, 0x7E800001));
    REX86_CHECK_EQ(context, rcp.Lane(0), 0x3F800000u);  // 1/1
    REX86_CHECK_EQ(context, rcp.Lane(1), 0x00800000u);  // 1/2^126 = 2^-126
    REX86_CHECK_EQ(context, rcp.Lane(2), 0x7F800000u);  // denormal as zero
    REX86_CHECK_EQ(context, rcp.Lane(3), 0x00000000u);  // tiny, flushed
    REX86_CHECK_EQ(context, rcp.Flags(), 0u);
    Run rsqrt({0x0F, 0x52, 0xC1}, 0, 0, Pair(0x40800000, 0xBF800000), Pair(0x80000001, 0x7F800000));
    REX86_CHECK_EQ(context, rsqrt.Lane(0), 0x3F000000u);  // 1/sqrt(4)
    REX86_CHECK_EQ(context, rsqrt.Lane(1), 0xFFC00000u);  // negative
    REX86_CHECK_EQ(context, rsqrt.Lane(2), 0xFF800000u);  // -denormal as -0
    REX86_CHECK_EQ(context, rsqrt.Lane(3), 0x00000000u);  // +inf
    REX86_CHECK_EQ(context, rsqrt.Flags(), 0u);
}

}  // namespace

void RunSseFloatTests(rex86::test::Context& context)
{
    NaNAndFlagTests(context);
    UnmaskedTests(context);
    MinMaxCompareTests(context);
    ConversionTests(context);
    ApproximationTests(context);
}
