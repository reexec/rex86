#include <cstdint>
#include <initializer_list>
#include <vector>

#include "rex86/cpu.h"
#include "test_support.h"

namespace
{

// A flat-memory, no-service host for driving synthetic programs.
class TestEnvironment final : public rex86::Environment
{
public:
    bool LoadDescriptor(std::uint16_t, rex86::Descriptor* descriptor) override
    {
        *descriptor = rex86::Descriptor{};
        return true;
    }

    bool PortRead(std::uint16_t, std::uint8_t, std::uint32_t* value) override
    {
        if (!serve_ports)
        {
            return false;
        }
        *value = port_read_value;
        return true;
    }

    bool PortWrite(std::uint16_t, std::uint8_t, std::uint32_t) override
    {
        return serve_ports;
    }

    bool InterruptTarget(std::uint8_t, std::uint16_t*, std::uint32_t*) override
    {
        return false;
    }

    std::uint64_t ReadTimeStampCounter() override
    {
        return 0x1122334455667788ull;
    }

    void Cpuid(std::uint32_t, std::uint32_t, std::uint32_t registers[4]) override
    {
        registers[0] = 1;
        registers[1] = 2;
        registers[2] = 3;
        registers[3] = 4;
    }

    void OnCodePageWritten(std::uint32_t page_address) override
    {
        last_code_page_written = page_address;
        ++code_page_writes;
    }

    bool serve_ports = false;
    std::uint32_t port_read_value = 0;
    std::uint32_t last_code_page_written = 0;
    int code_page_writes = 0;
};

struct Machine
{
    std::vector<std::uint8_t> buffer;
    rex86::GuestMemory memory;
    TestEnvironment environment;
    rex86::Cpu cpu;

    explicit Machine(const std::initializer_list<std::uint8_t> program)
        : buffer(16 * rex86::kGuestPageSize, 0),
          memory(buffer.data(), static_cast<std::uint32_t>(buffer.size())),
          cpu(&memory, &environment, rex86::Features{})
    {
        memory.pages().Set(0, static_cast<std::uint32_t>(buffer.size()),
                           rex86::kPageReadWriteExecute);
        std::uint32_t address = kCodeBase;
        for (const std::uint8_t byte : program)
        {
            buffer[address++] = byte;
        }
        cpu.state().eip = kCodeBase;
        cpu.state().Set(rex86::Gpr::kEsp, kStackTop);
    }

    static constexpr std::uint32_t kCodeBase = 0x1000;
    static constexpr std::uint32_t kStackTop = 0xF000;
};

}  // namespace

void RunInterpTests(rex86::test::Context& context)
{
    using rex86::Gpr;
    using rex86::StopReason;

    {
        // mov eax, 5; add eax, 3; hlt
        Machine m({0xB8, 0x05, 0x00, 0x00, 0x00,
                   0x83, 0xC0, 0x03,
                   0xF4});
        const rex86::Event event = m.cpu.Run(100);
        REX86_CHECK(context, event.reason == StopReason::kHalted);
        REX86_CHECK_EQ(context, event.instructions_retired, std::uint64_t{3});
        REX86_CHECK_EQ(context, m.cpu.state().Get(Gpr::kEax), 8u);
        // HLT retired, so EIP points past it.
        REX86_CHECK_EQ(context, m.cpu.state().eip, Machine::kCodeBase + 9);
    }
    {
        // Flag edges: add al, 1 over 0xFF sets CF/ZF/AF/PF; sub 0 - 0x80
        // byte sets OF.
        Machine m({0xB0, 0xFF,         // mov al, 0xFF
                   0x04, 0x01,         // add al, 1
                   0xF4});
        m.cpu.Run(100);
        const std::uint32_t flags = m.cpu.state().eflags;
        REX86_CHECK(context, (flags & rex86::kEflagsCarry) != 0);
        REX86_CHECK(context, (flags & rex86::kEflagsZero) != 0);
        REX86_CHECK(context, (flags & rex86::kEflagsAdjust) != 0);
        REX86_CHECK(context, (flags & rex86::kEflagsParity) != 0);
        REX86_CHECK(context, (flags & rex86::kEflagsOverflow) == 0);
    }
    {
        // 16-bit operand in 32-bit code: 66 05 adds to AX only.
        Machine m({0xB8, 0xFF, 0xFF, 0x00, 0x00,  // mov eax, 0x0000FFFF
                   0x66, 0x05, 0x02, 0x00,        // add ax, 2
                   0xF4});
        m.cpu.Run(100);
        REX86_CHECK_EQ(context, m.cpu.state().Get(Gpr::kEax), 0x00000001u);
        REX86_CHECK(context, (m.cpu.state().eflags & rex86::kEflagsCarry) != 0);
    }
    {
        // push/pop and call/ret through the stack.
        Machine m({0xB8, 0x78, 0x56, 0x34, 0x12,  // mov eax, 0x12345678
                   0x50,                          // push eax
                   0x5B,                          // pop ebx
                   0xE8, 0x01, 0x00, 0x00, 0x00,  // call +1
                   0xF4,                          // hlt (after return)
                   0xC3});                        // the callee: ret
        const rex86::Event event = m.cpu.Run(100);
        REX86_CHECK(context, event.reason == StopReason::kHalted);
        REX86_CHECK_EQ(context, m.cpu.state().Get(Gpr::kEbx), 0x12345678u);
        REX86_CHECK_EQ(context, m.cpu.state().Get(Gpr::kEsp),
                       Machine::kStackTop);
    }
    {
        // A conditional loop: counts ECX down to zero.
        Machine m({0xB9, 0x05, 0x00, 0x00, 0x00,  // mov ecx, 5
                   0x31, 0xC0,                    // xor eax, eax
                   0x40,                          // inc eax
                   0x49,                          // dec ecx
                   0x75, 0xFC,                    // jnz -4
                   0xF4});
        m.cpu.Run(100);
        REX86_CHECK_EQ(context, m.cpu.state().Get(Gpr::kEax), 5u);
        REX86_CHECK_EQ(context, m.cpu.state().Get(Gpr::kEcx), 0u);
    }
    {
        // Memory operands: mov [ebx], eax; cmp [ebx], eax.
        Machine m({0xBB, 0x00, 0x20, 0x00, 0x00,  // mov ebx, 0x2000
                   0xB8, 0xEF, 0xBE, 0xAD, 0xDE,  // mov eax, 0xDEADBEEF
                   0x89, 0x03,                    // mov [ebx], eax
                   0x39, 0x03,                    // cmp [ebx], eax
                   0xF4});
        m.cpu.Run(100);
        REX86_CHECK(context, (m.cpu.state().eflags & rex86::kEflagsZero) != 0);
        std::uint32_t stored = 0;
        REX86_CHECK(context, m.memory.Read32(0x2000, &stored));
        REX86_CHECK_EQ(context, stored, 0xDEADBEEFu);
    }
    {
        // A gate stops execution before the instruction at it runs.
        Machine m({0x90, 0x90, 0xF4});
        m.cpu.RegisterGate(Machine::kCodeBase + 1);
        const rex86::Event event = m.cpu.Run(100);
        REX86_CHECK(context, event.reason == StopReason::kGate);
        REX86_CHECK_EQ(context, event.gate_address, Machine::kCodeBase + 1);
        REX86_CHECK_EQ(context, event.instructions_retired, std::uint64_t{1});
    }
    {
        // The budget stops without an event from the guest.
        Machine m({0x90, 0x90, 0x90, 0x90, 0xF4});
        const rex86::Event event = m.cpu.Run(2);
        REX86_CHECK(context, event.reason == StopReason::kBudgetExhausted);
        REX86_CHECK_EQ(context, event.instructions_retired, std::uint64_t{2});
    }
    {
        // INT n retires and stops with its vector; EIP points after it.
        Machine m({0xCD, 0x21, 0xF4});
        const rex86::Event event = m.cpu.Run(100);
        REX86_CHECK(context, event.reason == StopReason::kSoftwareInterrupt);
        REX86_CHECK_EQ(context, event.vector, std::uint8_t{0x21});
        REX86_CHECK_EQ(context, m.cpu.state().eip, Machine::kCodeBase + 2);
    }
    {
        // A declined OUT stops with the port event; a served IN completes.
        Machine m({0xE6, 0x42,  // out 0x42, al
                   0xF4});
        const rex86::Event event = m.cpu.Run(100);
        REX86_CHECK(context, event.reason == StopReason::kPortIo);
        REX86_CHECK_EQ(context, event.port, std::uint16_t{0x42});
        REX86_CHECK(context, event.port_is_write);
        REX86_CHECK_EQ(context, event.port_width, std::uint8_t{1});

        Machine served({0xE4, 0x60,  // in al, 0x60
                        0xF4});
        served.environment.serve_ports = true;
        served.environment.port_read_value = 0xAB;
        REX86_CHECK(context, served.cpu.Run(100).reason == StopReason::kHalted);
        REX86_CHECK_EQ(context,
                       served.cpu.state().Get(Gpr::kEax) & 0xFFu, 0xABu);
    }
    {
        // A write to an unmapped page faults without retiring; EIP stays.
        Machine m({0xBB, 0x00, 0x00, 0x0F, 0x00,  // mov ebx, 0xF0000 (end)
                   0x89, 0x03,                    // mov [ebx], eax
                   0xF4});
        const rex86::Event event = m.cpu.Run(100);
        REX86_CHECK(context, event.reason == StopReason::kFault);
        REX86_CHECK(context,
                    event.fault_kind == rex86::FaultKind::kAccessViolation);
        REX86_CHECK(context, event.fault_on_write);
        REX86_CHECK_EQ(context, event.instructions_retired, std::uint64_t{1});
        REX86_CHECK_EQ(context, m.cpu.state().eip, Machine::kCodeBase + 5);
    }
    {
        // A store into a kTranslated page clears the flag and reports it.
        Machine m({0xBB, 0x00, 0x20, 0x00, 0x00,  // mov ebx, 0x2000
                   0x88, 0x03,                    // mov [ebx], al
                   0xF4});
        m.memory.pages().Add(0x2000, 1, rex86::PageFlag::kTranslated);
        m.cpu.Run(100);
        REX86_CHECK(context, !rex86::Has(m.memory.pages().Get(0x2000),
                                         rex86::PageFlag::kTranslated));
        REX86_CHECK_EQ(context, m.environment.code_page_writes, 1);
        REX86_CHECK_EQ(context, m.environment.last_code_page_written,
                       0x2000u);
    }
    {
        // RDTSC and CPUID come from the Environment, never the host CPU.
        Machine m({0x0F, 0x31,  // rdtsc
                   0xF4});
        m.cpu.Run(100);
        REX86_CHECK_EQ(context, m.cpu.state().Get(Gpr::kEax), 0x55667788u);
        REX86_CHECK_EQ(context, m.cpu.state().Get(Gpr::kEdx), 0x11223344u);

        Machine c({0x0F, 0xA2,  // cpuid
                   0xF4});
        c.cpu.Run(100);
        REX86_CHECK_EQ(context, c.cpu.state().Get(Gpr::kEax), 1u);
        REX86_CHECK_EQ(context, c.cpu.state().Get(Gpr::kEbx), 2u);
        REX86_CHECK_EQ(context, c.cpu.state().Get(Gpr::kEcx), 3u);
        REX86_CHECK_EQ(context, c.cpu.state().Get(Gpr::kEdx), 4u);
    }
    {
        // An instruction outside the implemented increments reports
        // kIllegalInstruction rather than quietly doing nothing: here AAA
        // (BCD, a later increment).
        Machine m({0x37,  // aaa
                   0xF4});
        const rex86::Event event = m.cpu.Run(100);
        REX86_CHECK(context, event.reason == StopReason::kFault);
        REX86_CHECK(context,
                    event.fault_kind == rex86::FaultKind::kIllegalInstruction);
        REX86_CHECK_EQ(context, event.instructions_retired, std::uint64_t{0});
    }

    // --- increment 2 (#13) ---
    {
        // SHL: CF takes the last bit out, OF is defined at count 1; a
        // zero count touches nothing.
        Machine m({0xB0, 0x81,        // mov al, 0x81
                   0xC0, 0xE0, 0x01,  // shl al, 1
                   0xF4});
        m.cpu.Run(100);
        REX86_CHECK_EQ(context, m.cpu.state().Get(Gpr::kEax) & 0xFFu, 0x02u);
        REX86_CHECK(context, (m.cpu.state().eflags & rex86::kEflagsCarry) != 0);
        REX86_CHECK(context,
                    (m.cpu.state().eflags & rex86::kEflagsOverflow) != 0);
    }
    {
        // RCR rotates through CF: 1 rcr 1 with CF set gives the sign bit
        // and leaves CF = old bit 0.
        Machine m({0xF9,              // stc
                   0xB0, 0x01,        // mov al, 1
                   0xD0, 0xD8,        // rcr al, 1
                   0xF4});
        m.cpu.Run(100);
        REX86_CHECK_EQ(context, m.cpu.state().Get(Gpr::kEax) & 0xFFu, 0x80u);
        REX86_CHECK(context, (m.cpu.state().eflags & rex86::kEflagsCarry) != 0);
    }
    {
        // MUL fills the high half and sets CF/OF when it is significant.
        Machine m({0xB8, 0x00, 0x00, 0x00, 0x80,  // mov eax, 0x80000000
                   0xBB, 0x04, 0x00, 0x00, 0x00,  // mov ebx, 4
                   0xF7, 0xE3,                    // mul ebx
                   0xF4});
        m.cpu.Run(100);
        REX86_CHECK_EQ(context, m.cpu.state().Get(Gpr::kEax), 0u);
        REX86_CHECK_EQ(context, m.cpu.state().Get(Gpr::kEdx), 2u);
        REX86_CHECK(context, (m.cpu.state().eflags & rex86::kEflagsCarry) != 0);
    }
    {
        // Division by zero faults with kDivide, retires nothing and
        // leaves EIP at the instruction.
        Machine m({0x31, 0xDB,  // xor ebx, ebx
                   0xF7, 0xF3,  // div ebx
                   0xF4});
        const rex86::Event event = m.cpu.Run(100);
        REX86_CHECK(context, event.reason == StopReason::kFault);
        REX86_CHECK(context, event.fault_kind == rex86::FaultKind::kDivide);
        REX86_CHECK_EQ(context, event.instructions_retired, std::uint64_t{1});
        REX86_CHECK_EQ(context, m.cpu.state().eip, Machine::kCodeBase + 2);
    }
    {
        // IDIV: -7 / 2 = -3 remainder -1.
        Machine m({0xB8, 0xF9, 0xFF, 0xFF, 0xFF,  // mov eax, -7
                   0x99,                          // cdq
                   0xBB, 0x02, 0x00, 0x00, 0x00,  // mov ebx, 2
                   0xF7, 0xFB,                    // idiv ebx
                   0xF4});
        m.cpu.Run(100);
        REX86_CHECK_EQ(context, m.cpu.state().Get(Gpr::kEax), 0xFFFFFFFDu);
        REX86_CHECK_EQ(context, m.cpu.state().Get(Gpr::kEdx), 0xFFFFFFFFu);
    }
    {
        // REP MOVSB copies ECX bytes and leaves it zero; DF=0 ascends.
        Machine m({0xBE, 0x00, 0x20, 0x00, 0x00,  // mov esi, 0x2000
                   0xBF, 0x00, 0x30, 0x00, 0x00,  // mov edi, 0x3000
                   0xB9, 0x04, 0x00, 0x00, 0x00,  // mov ecx, 4
                   0xF3, 0xA4,                    // rep movsb
                   0xF4});
        const char source[4] = {'r', 'e', 'x', '!'};
        m.memory.WriteBytes(0x2000,
                            reinterpret_cast<const std::uint8_t*>(source), 4);
        const rex86::Event event = m.cpu.Run(100);
        REX86_CHECK(context, event.reason == StopReason::kHalted);
        REX86_CHECK_EQ(context, event.instructions_retired, std::uint64_t{5});
        std::uint32_t copied = 0;
        m.memory.Read32(0x3000, &copied);
        REX86_CHECK_EQ(context, copied, 0x21786572u);  // "rex!"
        REX86_CHECK_EQ(context, m.cpu.state().Get(Gpr::kEcx), 0u);
        REX86_CHECK_EQ(context, m.cpu.state().Get(Gpr::kEsi), 0x2004u);
    }
    {
        // REPNE SCASB finds the byte and stops with ZF set.
        Machine m({0xBF, 0x00, 0x20, 0x00, 0x00,  // mov edi, 0x2000
                   0xB9, 0x10, 0x00, 0x00, 0x00,  // mov ecx, 16
                   0xB0, 0x58,                    // mov al, 'X'
                   0xF2, 0xAE,                    // repne scasb
                   0xF4});
        m.memory.Write8(0x2003, 'X');
        m.cpu.Run(100);
        REX86_CHECK(context, (m.cpu.state().eflags & rex86::kEflagsZero) != 0);
        REX86_CHECK_EQ(context, m.cpu.state().Get(Gpr::kEdi), 0x2004u);
        REX86_CHECK_EQ(context, m.cpu.state().Get(Gpr::kEcx), 12u);
    }
    {
        // BTS over memory addresses bits beyond the operand: bit 35 of
        // [0x2000] is bit 3 of byte 0x2004.
        Machine m({0xBB, 0x00, 0x20, 0x00, 0x00,  // mov ebx, 0x2000
                   0xB8, 0x23, 0x00, 0x00, 0x00,  // mov eax, 35
                   0x0F, 0xAB, 0x03,              // bts [ebx], eax
                   0xF4});
        m.cpu.Run(100);
        std::uint8_t byte = 0;
        m.memory.Read8(0x2004, &byte);
        REX86_CHECK_EQ(context, byte, 0x08u);
        REX86_CHECK(context, (m.cpu.state().eflags & rex86::kEflagsCarry) == 0);
    }
    {
        // BSF on zero sets ZF and preserves the destination; otherwise it
        // finds the lowest set bit.
        Machine m({0xB8, 0x2A, 0x00, 0x00, 0x00,  // mov eax, 42
                   0x31, 0xDB,                    // xor ebx, ebx
                   0x0F, 0xBC, 0xC3,              // bsf eax, ebx
                   0xF4});
        m.cpu.Run(100);
        REX86_CHECK(context, (m.cpu.state().eflags & rex86::kEflagsZero) != 0);
        REX86_CHECK_EQ(context, m.cpu.state().Get(Gpr::kEax), 42u);

        Machine n({0xBB, 0x30, 0x00, 0x00, 0x00,  // mov ebx, 0x30
                   0x0F, 0xBC, 0xC3,              // bsf eax, ebx
                   0xF4});
        n.cpu.Run(100);
        REX86_CHECK_EQ(context, n.cpu.state().Get(Gpr::kEax), 4u);
    }
    {
        // SETNZ writes exactly one byte from the condition.
        Machine m({0x31, 0xC0,        // xor eax, eax  (ZF=1)
                   0x0F, 0x95, 0xC3,  // setnz bl
                   0x40,              // inc eax       (ZF=0)
                   0x0F, 0x95, 0xC1,  // setnz cl
                   0xF4});
        m.cpu.Run(100);
        REX86_CHECK_EQ(context, m.cpu.state().Get(Gpr::kEbx) & 0xFFu, 0u);
        REX86_CHECK_EQ(context, m.cpu.state().Get(Gpr::kEcx) & 0xFFu, 1u);
    }
    {
        // ENTER 8,0 and LEAVE restore the frame exactly.
        Machine m({0xC8, 0x08, 0x00, 0x00,  // enter 8, 0
                   0xC9,                    // leave
                   0xF4});
        const std::uint32_t old_ebp = 0x1234;
        m.cpu.state().Set(Gpr::kEbp, old_ebp);
        m.cpu.Run(100);
        REX86_CHECK_EQ(context, m.cpu.state().Get(Gpr::kEbp), old_ebp);
        REX86_CHECK_EQ(context, m.cpu.state().Get(Gpr::kEsp),
                       Machine::kStackTop);
    }
}
