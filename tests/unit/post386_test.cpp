// The integer instructions after the 386 (design #22, decision 6), the
// CMOV feature switch, POPFD's AC and ID bits, and the writability check
// of segment stores.

#include <cstdint>
#include <initializer_list>
#include <vector>

#include "rex86/cpu.h"
#include "test_support.h"

namespace
{

class NullEnvironment final : public rex86::Environment
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

constexpr std::uint32_t kCode = 0x1000;
constexpr std::uint32_t kData = 0x2000;
constexpr std::uint32_t kStack = 0x3000;

struct Machine
{
    std::vector<std::uint8_t> buffer;
    rex86::GuestMemory memory;
    NullEnvironment environment;
    rex86::Cpu cpu;

    Machine(const std::initializer_list<std::uint8_t> program,
            const rex86::Features features = rex86::Features{})
        : buffer(4 * rex86::kGuestPageSize, 0),
          memory(buffer.data(), static_cast<std::uint32_t>(buffer.size())),
          cpu(&memory, &environment, features)
    {
        memory.pages().Set(0, static_cast<std::uint32_t>(buffer.size()),
                           rex86::kPageReadWriteExecute);
        std::uint32_t address = kCode;
        for (const std::uint8_t byte : program)
        {
            buffer[address++] = byte;
        }
        cpu.state().eip = kCode;
        cpu.state().Set(rex86::Gpr::kEsp, kStack);
    }

    std::uint32_t Read32(const std::uint32_t address) const
    {
        return buffer[address] | (buffer[address + 1] << 8) |
               (buffer[address + 2] << 16) | (static_cast<std::uint32_t>(buffer[address + 3]) << 24);
    }

    void Write32(const std::uint32_t address, const std::uint32_t value)
    {
        for (int i = 0; i < 4; ++i)
        {
            buffer[address + i] = static_cast<std::uint8_t>(value >> (8 * i));
        }
    }
};

constexpr std::uint32_t kZf = 1u << 6;

}  // namespace

void RunPost386Tests(rex86::test::Context& context)
{
    using rex86::FaultKind;
    using rex86::Gpr;
    using rex86::StopReason;

    // CMOVZ eax, ecx: moves when ZF is set, not otherwise.
    {
        Machine m({0x0F, 0x44, 0xC1, 0x0F, 0x44, 0xC1});
        rex86::CpuState& s = m.cpu.state();
        s.Set(Gpr::kEax, 1);
        s.Set(Gpr::kEcx, 2);
        s.eflags |= kZf;
        REX86_CHECK(context, m.cpu.Step().reason == StopReason::kBudgetExhausted);
        REX86_CHECK_EQ(context, s.Get(Gpr::kEax), 2u);
        s.Set(Gpr::kEcx, 3);
        s.eflags &= ~kZf;
        m.cpu.Step();
        REX86_CHECK_EQ(context, s.Get(Gpr::kEax), 2u);
    }
    // CMOVZ eax, [ebx] reads its source even when the condition is false.
    {
        Machine m({0x0F, 0x44, 0x03});
        m.cpu.state().Set(Gpr::kEbx, 0x00F00000u);
        m.cpu.state().eflags &= ~kZf;
        const rex86::Event event = m.cpu.Step();
        REX86_CHECK(context, event.reason == StopReason::kFault);
        REX86_CHECK(context, event.fault_kind == FaultKind::kAccessViolation);
    }
    // Features::cmov off: CMOVcc is an invalid opcode.
    {
        rex86::Features features;
        features.cmov = false;
        Machine m({0x0F, 0x44, 0xC1}, features);
        const rex86::Event event = m.cpu.Step();
        REX86_CHECK(context, event.reason == StopReason::kFault);
        REX86_CHECK(context, event.fault_kind == FaultKind::kIllegalInstruction);
    }
    // BSWAP eax.
    {
        Machine m({0x0F, 0xC8});
        m.cpu.state().Set(Gpr::kEax, 0x11223344u);
        m.cpu.Step();
        REX86_CHECK_EQ(context, m.cpu.state().Get(Gpr::kEax), 0x44332211u);
    }
    // XADD [ebx], ecx: the old memory value lands in ECX, the sum in memory.
    {
        Machine m({0x0F, 0xC1, 0x0B});
        m.Write32(kData, 40);
        m.cpu.state().Set(Gpr::kEbx, kData);
        m.cpu.state().Set(Gpr::kEcx, 2);
        m.cpu.Step();
        REX86_CHECK_EQ(context, m.Read32(kData), 42u);
        REX86_CHECK_EQ(context, m.cpu.state().Get(Gpr::kEcx), 40u);
    }
    // CMPXCHG ecx, edx: equal stores EDX and sets ZF; unequal loads EAX.
    {
        Machine m({0x0F, 0xB1, 0xD1, 0x0F, 0xB1, 0xD1});
        rex86::CpuState& s = m.cpu.state();
        s.Set(Gpr::kEax, 5);
        s.Set(Gpr::kEcx, 5);
        s.Set(Gpr::kEdx, 9);
        m.cpu.Step();
        REX86_CHECK_EQ(context, s.Get(Gpr::kEcx), 9u);
        REX86_CHECK(context, (s.eflags & kZf) != 0);
        m.cpu.Step();
        REX86_CHECK_EQ(context, s.Get(Gpr::kEax), 9u);
        REX86_CHECK(context, (s.eflags & kZf) == 0);
    }
    // CMPXCHG8B [ebx], both outcomes.
    {
        Machine m({0x0F, 0xC7, 0x0B, 0x0F, 0xC7, 0x0B});
        rex86::CpuState& s = m.cpu.state();
        m.Write32(kData, 0x11111111u);
        m.Write32(kData + 4, 0x22222222u);
        s.Set(Gpr::kEbx, kData);
        s.Set(Gpr::kEax, 0x11111111u);
        s.Set(Gpr::kEdx, 0x22222222u);
        s.Set(Gpr::kEcx, 0x44444444u);
        m.cpu.Step();
        REX86_CHECK_EQ(context, m.Read32(kData), kData);
        REX86_CHECK_EQ(context, m.Read32(kData + 4), 0x44444444u);
        REX86_CHECK(context, (s.eflags & kZf) != 0);
        m.cpu.Step();
        REX86_CHECK_EQ(context, s.Get(Gpr::kEax), kData);
        REX86_CHECK_EQ(context, s.Get(Gpr::kEdx), 0x44444444u);
        REX86_CHECK(context, (s.eflags & kZf) == 0);
    }
    // UD2 is an invalid opcode, not an unimplemented instruction.
    {
        Machine m({0x0F, 0x0B});
        const rex86::Event event = m.cpu.Step();
        REX86_CHECK(context, event.reason == StopReason::kFault);
        REX86_CHECK(context, event.fault_kind == FaultKind::kIllegalInstruction);
        REX86_CHECK_EQ(context, event.fault_address, kCode);
    }
    // POPFD writes AC and ID (design #21, decision 3).
    {
        Machine m({0x9D});
        m.Write32(kStack, 0x00240202u);
        m.cpu.Step();
        REX86_CHECK_EQ(context, m.cpu.state().eflags & 0x00240000u, 0x00240000u);
    }
    // A store through a read-only code segment is #GP, flat or not.
    {
        Machine m({0x2E, 0x89, 0x03});
        rex86::SegmentRegister& cs = m.cpu.state().Seg(rex86::Segment::kCs);
        cs.executable = true;
        cs.writable = false;
        m.cpu.state().Set(Gpr::kEbx, kData);
        const rex86::Event event = m.cpu.Step();
        REX86_CHECK(context, event.reason == StopReason::kFault);
        REX86_CHECK(context, event.fault_kind == FaultKind::kGeneralProtection);
        REX86_CHECK_EQ(context, m.cpu.state().eip, kCode);
    }
}
