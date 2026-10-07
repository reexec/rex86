#include "rex86/cpu_state.h"

#include <cstdint>

#include "test_support.h"

void RunCpuStateTests(rex86::test::Context& context)
{
    using rex86::CpuState;
    using rex86::Gpr;
    using rex86::Segment;

    CpuState state;
    state.Set(Gpr::kEax, 0x12345678u);
    state.Set(Gpr::kEsp, 0x0013FF00u);
    state.eip = 0x00401000u;
    state.eflags |= rex86::kEflagsCarry | rex86::kEflagsInterrupt;
    state.Seg(Segment::kFs).base = 0x7FFDE000u;
    state.x87.control_word = 0x027Fu;
    state.x87.registers[0][9] = 0x40u;

    REX86_CHECK_EQ(context, state.Get(Gpr::kEax), std::uint32_t{0x12345678u});
    REX86_CHECK_EQ(context, state.Get(Gpr::kEsp), std::uint32_t{0x0013FF00u});
    REX86_CHECK(context, !state.Seg(Segment::kFs).IsFlat());

    state.Reset();

    // Reset values the Intel SDM defines: EFLAGS bit 1 set and nothing else,
    // FNINIT's control word 0x037F and an all-empty tag word, flat segments
    // with a 32-bit default width, and CS alone executable.
    REX86_CHECK_EQ(context, state.Get(Gpr::kEax), std::uint32_t{0});
    REX86_CHECK_EQ(context, state.Get(Gpr::kEsp), std::uint32_t{0});
    REX86_CHECK_EQ(context, state.eip, std::uint32_t{0});
    REX86_CHECK_EQ(context, state.eflags, rex86::kEflagsReserved1);
    REX86_CHECK_EQ(context, state.x87.control_word, std::uint16_t{0x037Fu});
    REX86_CHECK_EQ(context, state.x87.status_word, std::uint16_t{0});
    REX86_CHECK_EQ(context, state.x87.tag_word, std::uint16_t{0xFFFFu});
    REX86_CHECK_EQ(context, state.x87.registers[0][9], std::uint8_t{0});
    for (const rex86::SegmentRegister& segment : state.segments)
    {
        REX86_CHECK(context, segment.IsFlat());
        REX86_CHECK(context, segment.default_32bit);
        REX86_CHECK(context, segment.present);
    }
    REX86_CHECK(context, state.Seg(Segment::kCs).executable);
    REX86_CHECK(context, !state.Seg(Segment::kCs).writable);
    REX86_CHECK(context, !state.Seg(Segment::kDs).executable);
    REX86_CHECK(context, state.Seg(Segment::kDs).writable);

    // The 80-bit register format is ten bytes, whatever the host's long
    // double is.
    REX86_CHECK_EQ(context, sizeof(state.x87.registers[0]), std::size_t{10});
    REX86_CHECK_EQ(context, sizeof(state.x87.registers), std::size_t{80});
}
