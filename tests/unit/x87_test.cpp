// x87 increment 1 (#19). The host comparison fuzz (tests/host/linux/)
// covers the numbers against real hardware; these tests cover what it
// cannot see: #MF delivery at the next waiting instruction, the
// last-instruction/operand pointers, the environment layouts, a memory
// fault leaving the x87 untouched, and the Features gate.

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <vector>

#include "rex86/cpu.h"
#include "test_support.h"

namespace
{

class Host final : public rex86::Environment
{
public:
    bool LoadDescriptor(std::uint16_t, rex86::Descriptor* descriptor) override
    {
        *descriptor = rex86::Descriptor{};
        return true;
    }
    bool PortRead(std::uint16_t, std::uint8_t, std::uint32_t*) override { return false; }
    bool PortWrite(std::uint16_t, std::uint8_t, std::uint32_t) override { return false; }
    bool InterruptTarget(std::uint8_t, std::uint16_t*, std::uint32_t*) override { return false; }
    std::uint64_t ReadTimeStampCounter() override { return 0; }
    void Cpuid(std::uint32_t, std::uint32_t, std::uint32_t r[4]) override
    {
        r[0] = r[1] = r[2] = r[3] = 0;
    }
};

struct Machine
{
    static constexpr std::uint32_t kCode = 0x1000;
    static constexpr std::uint32_t kData = 0x2000;
    static constexpr std::uint32_t kStackTop = 0xF000;

    std::vector<std::uint8_t> buffer;
    rex86::GuestMemory memory;
    Host host;
    rex86::Cpu cpu;

    explicit Machine(const std::initializer_list<std::uint8_t> program,
                     const rex86::Features features = rex86::Features{})
        : buffer(16 * rex86::kGuestPageSize, 0),
          memory(buffer.data(), static_cast<std::uint32_t>(buffer.size())),
          cpu(&memory, &host, features)
    {
        memory.pages().Set(0, static_cast<std::uint32_t>(buffer.size()),
                           rex86::kPageReadWriteExecute);
        std::uint32_t at = kCode;
        for (const std::uint8_t byte : program)
        {
            buffer[at++] = byte;
        }
        cpu.state().eip = kCode;
        cpu.state().Set(rex86::Gpr::kEsp, kStackTop);
        cpu.state().Set(rex86::Gpr::kEbx, kData);
    }

    std::uint16_t Word(const std::uint32_t at) const
    {
        return static_cast<std::uint16_t>(buffer[at] | (buffer[at + 1] << 8));
    }

    std::uint32_t Dword(const std::uint32_t at) const
    {
        return Word(at) | (static_cast<std::uint32_t>(Word(at + 2)) << 16);
    }
};

void PutDouble(Machine* m, const std::uint32_t at, const double value)
{
    std::memcpy(&m->buffer[at], &value, sizeof value);
}

// The double-precision default QNaN, 0x7FF8000000000000.
void PutQuietNaN(Machine* m, const std::uint32_t at)
{
    const std::uint64_t bits = 0x7FF8000000000000ull;
    std::memcpy(&m->buffer[at], &bits, sizeof bits);
}

double GetDouble(const Machine& m, const std::uint32_t at)
{
    double value = 0;
    std::memcpy(&value, &m.buffer[at], sizeof value);
    return value;
}

}  // namespace

void RunX87Tests(rex86::test::Context& context)
{
    using rex86::FaultKind;
    using rex86::StopReason;

    {
        // fld qword [ebx]; fadd qword [ebx+8]; fstp qword [ebx+16]: the
        // basic data path, bit-exact through SoftFloat.
        Machine m({0xDD, 0x03, 0xDC, 0x43, 0x08, 0xDD, 0x5B, 0x10, 0xF4});
        PutDouble(&m, Machine::kData, 1.5);
        PutDouble(&m, Machine::kData + 8, 0.25);
        REX86_CHECK(context, m.cpu.Run(100).reason == StopReason::kHalted);
        REX86_CHECK(context, GetDouble(m, Machine::kData + 16) == 1.75);
        // The stack is empty again: TOP back at 0, every tag empty.
        REX86_CHECK_EQ(context, m.cpu.state().x87.status_word & 0x3800u, 0u);
        REX86_CHECK_EQ(context, m.cpu.state().x87.tag_word, std::uint16_t{0xFFFF});
    }
    {
        // An unmasked divide-by-zero is pending after FDIV; the FDIV itself
        // retires without a result, and the next waiting x87 instruction
        // faults kFloatingPoint before it runs (#MF).
        Machine m({0xD9, 0x2B,              // fldcw [ebx]
                   0xD9, 0xE8,              // fld1
                   0xD9, 0xEE,              // fldz
                   0xDE, 0xF9,              // fdivp st(1), st(0)
                   0xD9, 0xE8,              // fld1  <- #MF here
                   0xF4});
        m.buffer[Machine::kData] = 0x7B;  // CW 0x037B: ZE unmasked
        m.buffer[Machine::kData + 1] = 0x03;
        const rex86::Event event = m.cpu.Run(100);
        REX86_CHECK(context, event.reason == StopReason::kFault);
        REX86_CHECK(context, event.fault_kind == FaultKind::kFloatingPoint);
        REX86_CHECK_EQ(context, m.cpu.state().eip, Machine::kCode + 8);
        const std::uint16_t sw = m.cpu.state().x87.status_word;
        REX86_CHECK(context, (sw & 0x0004u) != 0);  // ZE
        REX86_CHECK(context, (sw & 0x0080u) != 0);  // ES
        REX86_CHECK(context, (sw & 0x8000u) != 0);  // B
        // The pre-computation exception left both registers in place.
        REX86_CHECK_EQ(context, (sw >> 11) & 7u, 6u);
    }
    {
        // The non-waiting forms run with an exception pending: FNSTSW
        // reads it out and FNCLEX clears it, after which FWAIT passes.
        Machine m({0xD9, 0x2B,              // fldcw [ebx]
                   0xD9, 0xE8,              // fld1
                   0xD9, 0xEE,              // fldz
                   0xDE, 0xF9,              // fdivp st(1), st(0)
                   0xDF, 0xE0,              // fnstsw ax
                   0xDB, 0xE2,              // fnclex
                   0x9B,                    // fwait
                   0xF4});
        m.buffer[Machine::kData] = 0x7B;
        m.buffer[Machine::kData + 1] = 0x03;
        REX86_CHECK(context, m.cpu.Run(100).reason == StopReason::kHalted);
        REX86_CHECK(context, (m.cpu.state().Get(rex86::Gpr::kEax) & 0x0084u) == 0x0084u);
        REX86_CHECK_EQ(context, m.cpu.state().x87.status_word & 0x80FFu, 0u);
    }
    {
        // FNSTENV (32-bit protected-mode layout): CW/SW/TW with ones in
        // the reserved halves, then FIP/FCS|FOP/FDP/FDS of the last
        // non-control instruction; afterwards every exception is masked.
        Machine m({0xDD, 0x43, 0x40,        // fld qword [ebx+0x40]
                   0xD9, 0x73, 0x10,        // fnstenv [ebx+0x10]
                   0xF4});
        PutDouble(&m, Machine::kData + 0x40, 2.0);
        m.cpu.state().x87.control_word = 0x0360;
        REX86_CHECK(context, m.cpu.Run(100).reason == StopReason::kHalted);
        const std::uint32_t env = Machine::kData + 0x10;
        REX86_CHECK_EQ(context, m.Dword(env + 0), 0xFFFF0360u);
        REX86_CHECK_EQ(context, m.Dword(env + 4), 0xFFFF3800u);  // TOP = 7
        REX86_CHECK_EQ(context, m.Dword(env + 8), 0xFFFF3FFFu);  // R7 valid
        REX86_CHECK_EQ(context, m.Dword(env + 12), Machine::kCode);
        // FOP: (DD & 7) << 8 | ModR/M 0x43.
        REX86_CHECK_EQ(context, m.Dword(env + 16) >> 16, 0x0543u);
        REX86_CHECK_EQ(context, m.Dword(env + 20), Machine::kData + 0x40);
        REX86_CHECK_EQ(context, m.cpu.state().x87.control_word, std::uint16_t{0x037F});
    }
    {
        // FNSAVE stores the registers in ST order and reinitializes;
        // FRSTOR brings the same state back.
        Machine m({0xD9, 0xE8,              // fld1
                   0xD9, 0xEB,              // fldpi
                   0xDD, 0x33,              // fnsave [ebx]
                   0xDD, 0x23,              // frstor [ebx]
                   0xDD, 0x5B, 0x70,        // fstp qword [ebx+0x70]
                   0xF4});
        REX86_CHECK(context, m.cpu.Run(100).reason == StopReason::kHalted);
        // ST(0) at offset 28 was pi: C90FDAA22168C235, exponent 0x4000.
        REX86_CHECK_EQ(context, m.Dword(Machine::kData + 28), 0x2168C235u);
        REX86_CHECK_EQ(context, m.Dword(Machine::kData + 32), 0xC90FDAA2u);
        REX86_CHECK_EQ(context, m.Word(Machine::kData + 36), std::uint16_t{0x4000});
        REX86_CHECK(context, GetDouble(m, Machine::kData + 0x70) == 3.141592653589793);
    }
    {
        // A store that faults leaves the x87 untouched: FSTP to an
        // unwritable page neither pops nor raises anything.
        Machine m({0xD9, 0xE8,              // fld1
                   0xDD, 0x1D, 0x00, 0x50, 0x00, 0x00,  // fstp qword [0x5000]
                   0xF4});
        m.memory.pages().Set(0x5000, rex86::kGuestPageSize, rex86::kPageReadExecute);
        const rex86::Event event = m.cpu.Run(100);
        REX86_CHECK(context, event.fault_kind == FaultKind::kAccessViolation);
        REX86_CHECK_EQ(context, m.cpu.state().eip, Machine::kCode + 2);
        REX86_CHECK_EQ(context, (m.cpu.state().x87.status_word >> 11) & 7u, 7u);
        REX86_CHECK_EQ(context, m.cpu.state().x87.status_word & 0x00FFu, 0u);
        // FIP still names FLD1, the last instruction that completed.
        REX86_CHECK_EQ(context, m.cpu.state().x87.last_instruction_pointer, Machine::kCode);
    }
    {
        // FCOMI writes ZF/PF/CF and clears OF/SF/AF.
        Machine m({0xD9, 0xE8,              // fld1
                   0xD9, 0xEE,              // fldz
                   0xDB, 0xF1,              // fcomi st(0), st(1)
                   0xF4});
        m.cpu.state().eflags |= rex86::kEflagsOverflow | rex86::kEflagsZero;
        REX86_CHECK(context, m.cpu.Run(100).reason == StopReason::kHalted);
        const std::uint32_t flags = m.cpu.state().eflags;
        REX86_CHECK(context, (flags & rex86::kEflagsCarry) != 0);  // 0 < 1
        REX86_CHECK(context, (flags & rex86::kEflagsZero) == 0);
        REX86_CHECK(context, (flags & rex86::kEflagsOverflow) == 0);
    }
    {
        // An unmasked #IA in a compare leaves the condition codes and the
        // stack alone (SDM: "flags not set if unmasked #IA"); the masked
        // compare reports unordered and pops.
        Machine m({0xD9, 0x2B,              // fldcw [ebx]
                   0xDD, 0x43, 0x08,        // fld qword [ebx+8]  (QNaN)
                   0xD9, 0xE8,              // fld1
                   0xD8, 0xD9,              // fcomp st(1)
                   0xF4});
        m.buffer[Machine::kData] = 0x7E;  // CW 0x037E: IE unmasked
        m.buffer[Machine::kData + 1] = 0x03;
        PutQuietNaN(&m, Machine::kData + 8);
        REX86_CHECK(context, m.cpu.Run(100).reason == StopReason::kHalted);
        const std::uint16_t sw = m.cpu.state().x87.status_word;
        REX86_CHECK(context, (sw & 0x0081u) == 0x0081u);       // IE, ES
        REX86_CHECK_EQ(context, sw & 0x4500u, 0u);              // C3/C2/C0 untouched
        REX86_CHECK_EQ(context, (sw >> 11) & 7u, 6u);           // no pop

        Machine masked({0xDD, 0x43, 0x08, 0xD9, 0xE8, 0xD8, 0xD9, 0xF4});
        PutQuietNaN(&masked, Machine::kData + 8);
        REX86_CHECK(context, masked.cpu.Run(100).reason == StopReason::kHalted);
        const std::uint16_t msw = masked.cpu.state().x87.status_word;
        REX86_CHECK_EQ(context, msw & 0x4500u, 0x4500u);        // unordered
        REX86_CHECK_EQ(context, (msw >> 11) & 7u, 7u);          // popped
    }
    {
        // With Features::x87 off an x87 instruction is illegal.
        rex86::Features features;
        features.x87 = false;
        Machine m({0xD9, 0xE8, 0xF4}, features);
        REX86_CHECK(context, m.cpu.Run(100).fault_kind == FaultKind::kIllegalInstruction);
    }
}
