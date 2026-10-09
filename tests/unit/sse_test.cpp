#include <cstdint>

#include "simd_test_machine.h"
#include "test_support.h"

namespace
{

using rex86::FaultKind;
using rex86::Gpr;
using rex86::StopReason;
using rex86::test::SimdMachine;

constexpr std::uint64_t kLowA = 0x1111111100000000ull;
constexpr std::uint64_t kHighA = 0x3333333322222222ull;
constexpr std::uint64_t kLowB = 0x5555555544444444ull;
constexpr std::uint64_t kHighB = 0x7777777766666666ull;

void MoveTests(rex86::test::Context& context)
{
    {
        // movaps xmm0, [ebx]; movaps [ebx+16], xmm0.
        SimdMachine m({0x0F, 0x28, 0x03, 0x0F, 0x29, 0x43, 0x10});
        m.Put64(SimdMachine::kData, kLowA);
        m.Put64(SimdMachine::kData + 8, kHighA);
        m.Run();
        REX86_CHECK_EQ(context, m.XmmHalf(0, 0), kLowA);
        REX86_CHECK_EQ(context, m.XmmHalf(0, 1), kHighA);
        REX86_CHECK_EQ(context, m.Get64(SimdMachine::kData + 16), kLowA);
        REX86_CHECK_EQ(context, m.Get64(SimdMachine::kData + 24), kHighA);
    }
    {
        // A misaligned MOVAPS is #GP before any access; MOVUPS is fine.
        SimdMachine m({0x0F, 0x28, 0x43, 0x04});  // movaps xmm0, [ebx+4]
        m.SetXmm(0, 1, 2);
        const rex86::Event event = m.Run();
        REX86_CHECK(context, event.fault_kind == FaultKind::kGeneralProtection);
        REX86_CHECK_EQ(context, m.XmmHalf(0, 0), 1ull);
        REX86_CHECK_EQ(context, m.cpu.state().eip, SimdMachine::kCode);
        SimdMachine u({0x0F, 0x10, 0x43, 0x04});  // movups xmm0, [ebx+4]
        u.Put64(SimdMachine::kData + 4, kLowB);
        REX86_CHECK(context, u.Run().reason == StopReason::kHalted);
        REX86_CHECK_EQ(context, u.XmmHalf(0, 0), kLowB);
    }
    {
        // movss xmm0, xmm1 keeps the upper lanes; movss xmm2, [ebx]
        // zeroes them; movss [ebx+8], xmm1 stores one dword.
        SimdMachine m({0xF3, 0x0F, 0x10, 0xC1,
                       0xF3, 0x0F, 0x10, 0x13,
                       0xF3, 0x0F, 0x11, 0x4B, 0x08});
        m.SetXmm(0, kLowA, kHighA);
        m.SetXmm(1, kLowB, kHighB);
        m.SetXmm(2, ~0ull, ~0ull);
        m.Put64(SimdMachine::kData, 0x99999999ABCDEF01ull);
        m.Put64(SimdMachine::kData + 8, 0xEEEEEEEEEEEEEEEEull);
        m.Run();
        REX86_CHECK_EQ(context, m.XmmHalf(0, 0), 0x1111111144444444ull);
        REX86_CHECK_EQ(context, m.XmmHalf(0, 1), kHighA);
        REX86_CHECK_EQ(context, m.XmmHalf(2, 0), 0x00000000ABCDEF01ull);
        REX86_CHECK_EQ(context, m.XmmHalf(2, 1), 0ull);
        REX86_CHECK_EQ(context, m.Get64(SimdMachine::kData + 8), 0xEEEEEEEE44444444ull);
    }
    {
        // movlps xmm0, [ebx]; movhps [ebx+8], xmm1; movhlps xmm2, xmm1;
        // movlhps xmm3, xmm1.
        SimdMachine m({0x0F, 0x12, 0x03,
                       0x0F, 0x17, 0x4B, 0x08,
                       0x0F, 0x12, 0xD1,
                       0x0F, 0x16, 0xD9});
        m.SetXmm(0, kLowA, kHighA);
        m.SetXmm(1, kLowB, kHighB);
        m.SetXmm(2, kLowA, kHighA);
        m.SetXmm(3, kLowA, kHighA);
        m.Put64(SimdMachine::kData, 0x0123456789ABCDEFull);
        m.Run();
        REX86_CHECK_EQ(context, m.XmmHalf(0, 0), 0x0123456789ABCDEFull);
        REX86_CHECK_EQ(context, m.XmmHalf(0, 1), kHighA);
        REX86_CHECK_EQ(context, m.Get64(SimdMachine::kData + 8), kHighB);
        REX86_CHECK_EQ(context, m.XmmHalf(2, 0), kHighB);
        REX86_CHECK_EQ(context, m.XmmHalf(2, 1), kHighA);
        REX86_CHECK_EQ(context, m.XmmHalf(3, 0), kLowA);
        REX86_CHECK_EQ(context, m.XmmHalf(3, 1), kLowB);
    }
}

void ShuffleLogicTests(rex86::test::Context& context)
{
    // Lanes of xmm0: 00000000 11111111 22222222 33333333; xmm1: 44.. 77..
    {
        SimdMachine m({0x0F, 0xC6, 0xC1, 0x1B});  // shufps xmm0, xmm1, 0x1B
        m.SetXmm(0, kLowA, kHighA);
        m.SetXmm(1, kLowB, kHighB);
        m.Run();
        // lane0 = a[3], lane1 = a[2], lane2 = b[1], lane3 = b[0].
        REX86_CHECK_EQ(context, m.XmmHalf(0, 0), 0x2222222233333333ull);
        REX86_CHECK_EQ(context, m.XmmHalf(0, 1), 0x4444444455555555ull);
    }
    {
        SimdMachine m({0x0F, 0x14, 0xC1, 0x0F, 0x15, 0xD3});  // unpcklps xmm0,xmm1; unpckhps xmm2,xmm3
        m.SetXmm(0, kLowA, kHighA);
        m.SetXmm(1, kLowB, kHighB);
        m.SetXmm(2, kLowA, kHighA);
        m.SetXmm(3, kLowB, kHighB);
        m.Run();
        REX86_CHECK_EQ(context, m.XmmHalf(0, 0), 0x4444444400000000ull);
        REX86_CHECK_EQ(context, m.XmmHalf(0, 1), 0x5555555511111111ull);
        REX86_CHECK_EQ(context, m.XmmHalf(2, 0), 0x6666666622222222ull);
        REX86_CHECK_EQ(context, m.XmmHalf(2, 1), 0x7777777733333333ull);
    }
    {
        // andnps xmm0, xmm1 is ~xmm0 & xmm1; movmskps eax, xmm1.
        SimdMachine m({0x0F, 0x55, 0xC1, 0x0F, 0x50, 0xC1});
        m.SetXmm(0, 0xFFFFFFFF00000000ull, 0x0F0F0F0FF0F0F0F0ull);
        m.SetXmm(1, 0x8000000180000000ull, 0x7FFFFFFFFFFFFFFFull);
        m.Run();
        REX86_CHECK_EQ(context, m.XmmHalf(0, 0), 0x0000000080000000ull);
        REX86_CHECK_EQ(context, m.XmmHalf(0, 1), 0x70F0F0F00F0F0F0Full);
        REX86_CHECK_EQ(context, m.cpu.state().Get(Gpr::kEax), 0x7u);
    }
    {
        // A packed logic op with a misaligned memory operand is #GP.
        SimdMachine m({0x0F, 0x57, 0x43, 0x08});  // xorps xmm0, [ebx+8]
        REX86_CHECK(context, m.Run().fault_kind == FaultKind::kGeneralProtection);
    }
    {
        // PREFETCHNTA of an unmapped address never faults; SFENCE is a no-op.
        SimdMachine m({0x0F, 0x18, 0x03, 0x0F, 0xAE, 0xF8});
        m.cpu.state().Set(Gpr::kEbx, 0xFFFF0000u);
        REX86_CHECK(context, m.Run().reason == StopReason::kHalted);
    }
}

void MxcsrTests(rex86::test::Context& context)
{
    {
        // ldmxcsr [ebx]; stmxcsr [ebx+4].
        SimdMachine m({0x0F, 0xAE, 0x13, 0x0F, 0xAE, 0x5B, 0x04});
        m.Put64(SimdMachine::kData, 0x0000000000009FBFull & 0xFFFFFFFFull);
        m.Run();
        REX86_CHECK_EQ(context, m.cpu.state().sse.mxcsr, 0x9FBFu);
        REX86_CHECK_EQ(context, m.Get32(SimdMachine::kData + 4), 0x9FBFu);
    }
    {
        // DAZ (bit 6) and bits 16 and up are reserved on the Pentium III.
        for (const std::uint32_t value : {0x1FC0u, 0x11F80u})
        {
            SimdMachine m({0x0F, 0xAE, 0x13});
            m.Put64(SimdMachine::kData, value);
            const rex86::Event event = m.Run();
            REX86_CHECK(context, event.fault_kind == FaultKind::kGeneralProtection);
            REX86_CHECK_EQ(context, m.cpu.state().sse.mxcsr, 0x1F80u);
        }
    }
}

void FxsaveTests(rex86::test::Context& context)
{
    {
        // fninit; fld1; fldz; fxsave [ebx].
        SimdMachine m({0xDB, 0xE3, 0xD9, 0xE8, 0xD9, 0xEE, 0x0F, 0xAE, 0x03});
        for (unsigned i = 0; i < 512; ++i)
        {
            m.buffer[SimdMachine::kData + i] = 0xAA;
        }
        m.SetXmm(5, kLowA, kHighA);
        m.cpu.state().sse.mxcsr = 0x1FBF;
        m.Run();
        const std::uint32_t at = SimdMachine::kData;
        REX86_CHECK_EQ(context, m.Get32(at) & 0xFFFFu, 0x037Fu);           // FCW
        REX86_CHECK_EQ(context, (m.Get32(at) >> 16) & 0x3800u, 0x3000u);  // TOP = 6
        REX86_CHECK_EQ(context, m.buffer[at + 4], std::uint8_t{0xC0});    // physical 6, 7
        REX86_CHECK_EQ(context, m.buffer[at + 5], std::uint8_t{0});
        REX86_CHECK_EQ(context, m.Get32(at + 24), 0x1FBFu);               // MXCSR
        REX86_CHECK_EQ(context, m.Get32(at + 28), 0xFFBFu);               // MXCSR_MASK
        // ST0 = +0.0, ST1 = 1.0 (0x3FFF, 0x8000000000000000), slots zero-padded.
        REX86_CHECK_EQ(context, m.Get64(at + 32), 0ull);
        REX86_CHECK_EQ(context, m.Get64(at + 48), 0x8000000000000000ull);
        REX86_CHECK_EQ(context, m.Get64(at + 56) & 0xFFFFFFFFFFFFull, 0x3FFFull);
        REX86_CHECK_EQ(context, m.Get64(at + 160 + 5 * 16), kLowA);
        REX86_CHECK_EQ(context, m.Get64(at + 160 + 5 * 16 + 8), kHighA);
        REX86_CHECK_EQ(context, m.buffer[at + 288], std::uint8_t{0xAA});  // untouched
    }
    {
        // FXRSTOR from an edited image: XMM, MXCSR, and tags rebuilt from
        // contents (the restored zero tags as zero, the 1.0 as valid).
        SimdMachine m({0xDB, 0xE3, 0xD9, 0xE8, 0xD9, 0xEE,
                       0x0F, 0xAE, 0x03,     // fxsave [ebx]
                       0x0F, 0xAE, 0x0B});   // fxrstor [ebx]
        m.Run();
        m.cpu.state().eip = SimdMachine::kCode + 9;
        m.Put64(SimdMachine::kData + 160 + 16, kLowB);
        m.buffer[SimdMachine::kData + 24] = 0x80;  // MXCSR 0x1F80 -> low byte 80
        m.cpu.state().sse.mxcsr = 0;
        m.cpu.state().x87.tag_word = 0xFFFF;
        REX86_CHECK(context, m.Run().reason == StopReason::kHalted);
        REX86_CHECK_EQ(context, m.XmmHalf(1, 0), kLowB);
        REX86_CHECK_EQ(context, m.cpu.state().sse.mxcsr, 0x1F80u);
        // TOP = 6: physical 6 holds 0.0 (zero tag 01), physical 7 holds 1.0.
        REX86_CHECK_EQ(context, m.cpu.state().x87.tag_word, std::uint16_t{0x1FFF});
    }
    {
        // FXRSTOR with a reserved MXCSR bit is #GP with nothing restored.
        SimdMachine m({0x0F, 0xAE, 0x0B});
        m.buffer[SimdMachine::kData + 24] = 0xC0;  // DAZ
        m.cpu.state().x87.tag_word = 0x1234;
        REX86_CHECK(context, m.Run().fault_kind == FaultKind::kGeneralProtection);
        REX86_CHECK_EQ(context, m.cpu.state().x87.tag_word, std::uint16_t{0x1234});
    }
    {
        // A misaligned FXSAVE is #GP.
        SimdMachine m({0x0F, 0xAE, 0x43, 0x08});
        REX86_CHECK(context, m.Run().fault_kind == FaultKind::kGeneralProtection);
    }
    {
        // Without SSE (a Mendocino) FXSAVE leaves MXCSR and XMM alone.
        rex86::Features mk3;
        mk3.sse = false;
        SimdMachine m({0x0F, 0xAE, 0x03}, mk3);
        for (unsigned i = 0; i < 512; ++i)
        {
            m.buffer[SimdMachine::kData + i] = 0xAA;
        }
        m.Run();
        REX86_CHECK_EQ(context, m.Get32(SimdMachine::kData + 24), 0xAAAAAAAAu);
        REX86_CHECK_EQ(context, m.Get32(SimdMachine::kData + 28), 0xAAAAAAAAu);
        REX86_CHECK_EQ(context, m.Get64(SimdMachine::kData + 160), 0xAAAAAAAAAAAAAAAAull);
        REX86_CHECK_EQ(context, m.Get32(SimdMachine::kData) & 0xFFFFu, 0x037Fu);
    }
    {
        // Without SSE, XMM instructions and LDMXCSR are #UD.
        rex86::Features mk3;
        mk3.sse = false;
        SimdMachine m({0x0F, 0x28, 0xC1}, mk3);
        REX86_CHECK(context, m.Run().fault_kind == FaultKind::kIllegalInstruction);
        SimdMachine n({0x0F, 0xAE, 0x13}, mk3);
        REX86_CHECK(context, n.Run().fault_kind == FaultKind::kIllegalInstruction);
    }
}

}  // namespace

void RunSseTests(rex86::test::Context& context)
{
    MoveTests(context);
    ShuffleLogicTests(context);
    MxcsrTests(context);
    FxsaveTests(context);
}
