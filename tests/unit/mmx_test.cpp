#include <cstdint>

#include "simd_test_machine.h"
#include "test_support.h"

namespace
{

using rex86::FaultKind;
using rex86::Gpr;
using rex86::StopReason;
using rex86::test::SimdMachine;

// Runs `op mm0, mm1` on two values and returns MM0.
std::uint64_t Binary(std::initializer_list<std::uint8_t> code, std::uint64_t a, std::uint64_t b)
{
    SimdMachine m(code);
    m.SetMm(0, a);
    m.SetMm(1, b);
    m.Run();
    return m.Mm(0);
}

void ArithmeticTests(rex86::test::Context& context)
{
    // PADDB wraps per byte; PADDSB and PADDUSB saturate.
    REX86_CHECK_EQ(context, Binary({0x0F, 0xFC, 0xC1}, 0x7F80FF0102030405ull, 0x0180010101010101ull),
                   0x8000000203040506ull);
    REX86_CHECK_EQ(context, Binary({0x0F, 0xEC, 0xC1}, 0x7F80FF0102030405ull, 0x01FF010101010101ull),
                   0x7F80000203040506ull);
    REX86_CHECK_EQ(context, Binary({0x0F, 0xDC, 0xC1}, 0x7F80FF0102030405ull, 0x0180010101010101ull),
                   0x80FFFF0203040506ull);
    // PSUBUSW clamps at zero; PSUBSB at the signed range.
    REX86_CHECK_EQ(context, Binary({0x0F, 0xD9, 0xC1}, 0x0001000500100000ull, 0x0002000300100001ull),
                   0x0000000200000000ull);
    REX86_CHECK_EQ(context, Binary({0x0F, 0xE8, 0xC1}, 0x8000000000000000ull, 0x0100000000000000ull),
                   0x8000000000000000ull);
    // PCMPGTW is signed; PCMPEQD compares dwords.
    REX86_CHECK_EQ(context, Binary({0x0F, 0x65, 0xC1}, 0x0001FFFF80007FFFull, 0x0000000000007FFEull),
                   0xFFFF00000000FFFFull);
    REX86_CHECK_EQ(context, Binary({0x0F, 0x76, 0xC1}, 0x1234567800000001ull, 0x1234567800000002ull),
                   0xFFFFFFFF00000000ull);
    // PMADDWD: 0x8000 * 0x8000 twice wraps to 0x80000000.
    REX86_CHECK_EQ(context, Binary({0x0F, 0xF5, 0xC1}, 0x8000800000020003ull, 0x80008000FFFF0004ull),
                   0x800000000000000Aull);
    // PMULHW signed, PMULHUW unsigned, PMULLW low.
    REX86_CHECK_EQ(context, Binary({0x0F, 0xE5, 0xC1}, 0xFFFF000000000000ull, 0x0002000000000000ull),
                   0xFFFF000000000000ull);
    REX86_CHECK_EQ(context, Binary({0x0F, 0xE4, 0xC1}, 0xFFFF000000000000ull, 0x0002000000000000ull),
                   0x0001000000000000ull);
    REX86_CHECK_EQ(context, Binary({0x0F, 0xD5, 0xC1}, 0xFFFF000000000003ull, 0x0002000000000005ull),
                   0xFFFE00000000000Full);
    // PAVGB rounds up; PSADBW sums absolute byte differences into word 0.
    REX86_CHECK_EQ(context, Binary({0x0F, 0xE0, 0xC1}, 0x00FF000000000001ull, 0x01FF000000000002ull),
                   0x01FF000000000002ull);
    REX86_CHECK_EQ(context, Binary({0x0F, 0xF6, 0xC1}, 0x00FF000000000010ull, 0xFF00000000000001ull),
                   0x000000000000020Dull);
    // PMINSW signed, PMAXUB unsigned.
    REX86_CHECK_EQ(context, Binary({0x0F, 0xEA, 0xC1}, 0x8000000100000000ull, 0x7FFF000200000001ull),
                   0x8000000100000000ull);
    REX86_CHECK_EQ(context, Binary({0x0F, 0xDE, 0xC1}, 0x80000000000000FFull, 0x7F00000000000001ull),
                   0x80000000000000FFull);
    // PANDN is ~dst & src.
    REX86_CHECK_EQ(context, Binary({0x0F, 0xDF, 0xC1}, 0xFF00FF00FF00FF00ull, 0xFFFFFFFF00000000ull),
                   0x00FF00FF00000000ull);
}

void PackShiftTests(rex86::test::Context& context)
{
    // PACKSSWB: dst words then src words, signed saturation.
    REX86_CHECK_EQ(context, Binary({0x0F, 0x63, 0xC1}, 0x7FFF8000FF800005ull, 0x0000000100020003ull),
                   0x000102037F808005ull);
    // PACKUSWB: negative words to 0, large ones to 0xFF.
    REX86_CHECK_EQ(context, Binary({0x0F, 0x67, 0xC1}, 0x0100FFFF00800005ull, 0ull),
                   0x00000000FF008005ull);
    // PACKSSDW.
    REX86_CHECK_EQ(context, Binary({0x0F, 0x6B, 0xC1}, 0x8000000000012345ull, 0x00007FFF00000001ull),
                   0x7FFF000180007FFFull);
    // PUNPCKLBW interleaves the low bytes; PUNPCKHDQ the high dwords.
    REX86_CHECK_EQ(context, Binary({0x0F, 0x60, 0xC1}, 0x0000000003020100ull, 0x0000000013121110ull),
                   0x1303120211011000ull);
    REX86_CHECK_EQ(context, Binary({0x0F, 0x6A, 0xC1}, 0xAAAAAAAA00000000ull, 0xBBBBBBBB11111111ull),
                   0xBBBBBBBBAAAAAAAAull);
    // Shift counts past the width: PSRAW fills with the sign, PSLLQ clears.
    REX86_CHECK_EQ(context, Binary({0x0F, 0xE1, 0xC1}, 0x8000400000010000ull, 20ull),
                   0xFFFF000000000000ull);
    REX86_CHECK_EQ(context, Binary({0x0F, 0xF3, 0xC1}, 0xFFFFFFFFFFFFFFFFull, 64ull), 0ull);
    // The whole 64-bit count matters: 2^32 + 1 is not a count of 1.
    REX86_CHECK_EQ(context, Binary({0x0F, 0xF1, 0xC1}, 0x0001000100010001ull, 0x0000000100000001ull),
                   0ull);
    {
        // psrlw mm0, 4 (immediate form).
        SimdMachine m({0x0F, 0x71, 0xD0, 0x04});
        m.SetMm(0, 0xF000F00080000010ull);
        m.Run();
        REX86_CHECK_EQ(context, m.Mm(0), 0x0F000F0008000001ull);
    }
    {
        // psllq mm0, 4.
        SimdMachine m({0x0F, 0x73, 0xF0, 0x04});
        m.SetMm(0, 0x0123456789ABCDEFull);
        m.Run();
        REX86_CHECK_EQ(context, m.Mm(0), 0x123456789ABCDEF0ull);
    }
}

void MoveTests(rex86::test::Context& context)
{
    {
        // movd mm1, eax zero-extends; movd ecx, mm1 takes the low dword;
        // movq [ebx], mm1; movd mm2, [ebx+4].
        SimdMachine m({0x0F, 0x6E, 0xC8,        // movd mm1, eax
                       0x0F, 0x7E, 0xC9,        // movd ecx, mm1
                       0x0F, 0x7F, 0x0B,        // movq [ebx], mm1
                       0x0F, 0x6E, 0x53, 0x04}); // movd mm2, [ebx+4]
        m.SetMm(1, ~0ull);
        m.SetMm(2, ~0ull);
        m.cpu.state().Set(Gpr::kEax, 0x89ABCDEFu);
        m.Run();
        REX86_CHECK_EQ(context, m.Mm(1), 0x0000000089ABCDEFull);
        REX86_CHECK_EQ(context, m.cpu.state().Get(Gpr::kEcx), 0x89ABCDEFu);
        REX86_CHECK_EQ(context, m.Get64(SimdMachine::kData), 0x0000000089ABCDEFull);
        REX86_CHECK_EQ(context, m.Mm(2), 0ull);
    }
    {
        // pshufw mm0, mm1, 0x1B reverses the words; pextrw eax, mm0, 3;
        // pinsrw mm0, ecx, 0; pmovmskb edx, mm1.
        SimdMachine m({0x0F, 0x70, 0xC1, 0x1B,
                       0x0F, 0xC5, 0xC0, 0x03,
                       0x0F, 0xC4, 0xC1, 0x00,
                       0x0F, 0xD7, 0xD1});
        m.SetMm(1, 0x80017F0200030004ull);
        m.cpu.state().Set(Gpr::kEcx, 0xFFFF5555u);
        m.Run();
        REX86_CHECK_EQ(context, m.Mm(0), 0x000400037F025555ull);
        REX86_CHECK_EQ(context, m.cpu.state().Get(Gpr::kEax), 0x0004u);
        REX86_CHECK_EQ(context, m.cpu.state().Get(Gpr::kEdx), 0x80u);
    }
    {
        // maskmovq mm0, mm1 stores the selected bytes at DS:EDI only.
        SimdMachine m({0x0F, 0xF7, 0xC1});
        m.SetMm(0, 0x8877665544332211ull);
        m.SetMm(1, 0x8000800000800080ull);
        m.cpu.state().Set(Gpr::kEdi, SimdMachine::kData);
        m.Put64(SimdMachine::kData, 0xAAAAAAAAAAAAAAAAull);
        m.Run();
        REX86_CHECK_EQ(context, m.Get64(SimdMachine::kData), 0x88AA66AAAA33AA11ull);
    }
}

void StateTests(rex86::test::Context& context)
{
    {
        // fld1 leaves TOP = 7 and one valid tag; pxor mm2, mm2 sets TOP = 0,
        // every tag valid and MM2's bits 79:64 to FFFF; EMMS empties.
        SimdMachine m({0xD9, 0xE8,             // fld1
                       0x0F, 0xEF, 0xD2});     // pxor mm2, mm2
        m.Run();
        const rex86::X87State& x87 = m.cpu.state().x87;
        REX86_CHECK_EQ(context, (x87.status_word >> 11) & 7u, 0u);
        REX86_CHECK_EQ(context, x87.tag_word, std::uint16_t{0});
        REX86_CHECK_EQ(context, x87.registers[2][8], std::uint8_t{0xFF});
        REX86_CHECK_EQ(context, x87.registers[2][9], std::uint8_t{0xFF});
        REX86_CHECK_EQ(context, m.Mm(2), 0ull);
    }
    {
        // fld1; emms: every tag empty and TOP back to 0 (SDM Vol. 1 table
        // 9-2, measured on Zen 3).
        SimdMachine m({0xD9, 0xE8, 0x0F, 0x77});
        m.Run();
        REX86_CHECK_EQ(context, m.cpu.state().x87.tag_word, std::uint16_t{0xFFFF});
        REX86_CHECK_EQ(context, (m.cpu.state().x87.status_word >> 11) & 7u, 0u);
    }
    {
        // A pending unmasked x87 exception: #MF before the MMX instruction,
        // with nothing changed. EMMS faults the same way.
        for (const std::uint8_t second : {std::uint8_t{0xEF}, std::uint8_t{0x77}})
        {
            SimdMachine m(second == 0xEF ? std::initializer_list<std::uint8_t>{0x0F, 0xEF, 0xD2}
                                         : std::initializer_list<std::uint8_t>{0x0F, 0x77});
            rex86::X87State& x87 = m.cpu.state().x87;
            x87.control_word = 0x037E;   // #IE unmasked
            x87.status_word = 0x3881;    // TOP = 7, ES, IE
            x87.tag_word = 0x3FFF;
            m.SetMm(2, 0x1234ull);
            const rex86::Event event = m.Run();
            REX86_CHECK(context, event.reason == StopReason::kFault);
            REX86_CHECK(context, event.fault_kind == FaultKind::kFloatingPoint);
            REX86_CHECK_EQ(context, x87.status_word, std::uint16_t{0x3881});
            REX86_CHECK_EQ(context, x87.tag_word, std::uint16_t{0x3FFF});
            REX86_CHECK_EQ(context, m.Mm(2), 0x1234ull);
            REX86_CHECK_EQ(context, m.cpu.state().eip, SimdMachine::kCode);
        }
    }
    {
        // A faulting memory source leaves the x87 state alone.
        SimdMachine m({0x0F, 0x6F, 0x03});  // movq mm0, [ebx]
        m.cpu.state().Set(Gpr::kEbx, 0xFFFF0000u);
        m.cpu.state().x87.tag_word = 0xFFFF;
        const rex86::Event event = m.Run();
        REX86_CHECK(context, event.fault_kind == FaultKind::kAccessViolation);
        REX86_CHECK_EQ(context, m.cpu.state().x87.tag_word, std::uint16_t{0xFFFF});
    }
}

void FeatureTests(rex86::test::Context& context)
{
    const auto run = [](std::initializer_list<std::uint8_t> code, const rex86::Features& features) {
        SimdMachine m(code, features);
        return m.Run();
    };
    rex86::Features mk3;  // Mendocino: MMX and FXSR, no SSE
    mk3.sse = false;
    rex86::Features k62;  // K6-2: MMX, no FXSR, no CMOV, no SSE
    k62.sse = false;
    k62.fxsr = false;
    k62.cmov = false;
    rex86::Features none;
    none.mmx = false;
    none.sse = false;

    REX86_CHECK(context, run({0x0F, 0xFC, 0xC1}, mk3).reason == StopReason::kHalted);
    REX86_CHECK(context, run({0x0F, 0xFC, 0xC1}, k62).reason == StopReason::kHalted);
    REX86_CHECK(context, run({0x0F, 0xFC, 0xC1}, none).fault_kind == FaultKind::kIllegalInstruction);
    // PSHUFW and PMOVMSKB are SSE's, though they use MM registers.
    REX86_CHECK(context, run({0x0F, 0x70, 0xC1, 0x00}, mk3).fault_kind ==
                             FaultKind::kIllegalInstruction);
    REX86_CHECK(context, run({0x0F, 0xD7, 0xC1}, mk3).fault_kind == FaultKind::kIllegalInstruction);
    REX86_CHECK(context, run({0x0F, 0x70, 0xC1, 0x00}, rex86::Features{}).reason ==
                             StopReason::kHalted);
    // FXSAVE: the Mendocino has it, the K6-2 does not.
    REX86_CHECK(context, run({0x0F, 0xAE, 0x03}, mk3).reason == StopReason::kHalted);
    REX86_CHECK(context, run({0x0F, 0xAE, 0x03}, k62).fault_kind == FaultKind::kIllegalInstruction);
    // 66-prefixed MMX forms are SSE2.
    REX86_CHECK(context, run({0x66, 0x0F, 0xFC, 0xC1}, rex86::Features{}).fault_kind ==
                             FaultKind::kIllegalInstruction);
}

}  // namespace

void RunMmxTests(rex86::test::Context& context)
{
    ArithmeticTests(context);
    PackShiftTests(context);
    MoveTests(context);
    StateTests(context);
    FeatureTests(context);
}
