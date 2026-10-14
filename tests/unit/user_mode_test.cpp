// Features::user_mode (design #69): a CPL 3, IOPL 0 process. The refused
// instructions fault before retiring, with EIP and the integer state
// unchanged and no port touched; POPF and IRET leave IF and IOPL alone.

#include <cstdint>
#include <initializer_list>
#include <vector>

#include "rex86/cpu.h"
#include "test_support.h"

namespace
{

// Flat descriptors, and counts of the port calls that must not happen.
class UserModeEnvironment final : public rex86::Environment
{
public:
    bool LoadDescriptor(std::uint16_t, rex86::Descriptor* descriptor) override
    {
        *descriptor = rex86::Descriptor{};
        descriptor->executable = true;
        return true;
    }

    bool PortRead(std::uint16_t, std::uint8_t, std::uint32_t* value) override
    {
        ++port_calls;
        *value = 0xAB;
        return true;
    }

    bool PortWrite(std::uint16_t, std::uint8_t, std::uint32_t) override
    {
        ++port_calls;
        return true;
    }

    bool InterruptTarget(std::uint8_t, std::uint16_t*, std::uint32_t*) override
    {
        return false;
    }

    std::uint64_t ReadTimeStampCounter() override
    {
        return 0;
    }

    void Cpuid(std::uint32_t, std::uint32_t, std::uint32_t registers[4]) override
    {
        registers[0] = registers[1] = registers[2] = registers[3] = 0;
    }

    int port_calls = 0;
};

rex86::Features UserMode(const bool on)
{
    rex86::Features features;
    features.user_mode = on;
    return features;
}

struct Machine
{
    std::vector<std::uint8_t> buffer;
    rex86::GuestMemory memory;
    UserModeEnvironment environment;
    rex86::Cpu cpu;

    Machine(const std::initializer_list<std::uint8_t> program, const bool user_mode = true)
        : buffer(16 * rex86::kGuestPageSize, 0),
          memory(buffer.data(), static_cast<std::uint32_t>(buffer.size())),
          cpu(&memory, &environment, UserMode(user_mode))
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
        // A Win32 thread runs with IF set.
        cpu.state().eflags |= rex86::kEflagsInterrupt;
    }

    static constexpr std::uint32_t kCodeBase = 0x1000;
    static constexpr std::uint32_t kStackTop = 0xF000;
};

// Runs `program` (one refused instruction after `prefix_steps` others) and
// checks the refusal: a fault of `kind` with `address`, the instruction
// unretired, and the registers as the prefix left them.
void CheckRefused(rex86::test::Context& context, const std::initializer_list<std::uint8_t> program,
                  const std::uint32_t refused_offset, const rex86::FaultKind kind,
                  const std::uint32_t address)
{
    Machine m(program);
    m.cpu.state().Set(rex86::Gpr::kEcx, 3);
    m.cpu.state().Set(rex86::Gpr::kEdi, 0x8000);
    m.cpu.state().Set(rex86::Gpr::kEsi, 0x9000);
    const std::uint32_t eflags = m.cpu.state().eflags;
    const rex86::Event event = m.cpu.Run(100);
    REX86_CHECK(context, event.reason == rex86::StopReason::kFault);
    REX86_CHECK(context, event.fault_kind == kind);
    REX86_CHECK_EQ(context, event.fault_address, address);
    REX86_CHECK_EQ(context, event.steps, std::uint64_t{refused_offset == 0 ? 0u : 1u});
    REX86_CHECK_EQ(context, m.cpu.state().eip, Machine::kCodeBase + refused_offset);
    REX86_CHECK_EQ(context, m.cpu.state().eflags, eflags);
    REX86_CHECK_EQ(context, m.cpu.state().Get(rex86::Gpr::kEcx), 3u);
    REX86_CHECK_EQ(context, m.cpu.state().Get(rex86::Gpr::kEdi), 0x8000u);
    REX86_CHECK_EQ(context, m.cpu.state().Get(rex86::Gpr::kEsi), 0x9000u);
    REX86_CHECK_EQ(context, m.environment.port_calls, 0);
}

}  // namespace

void RunUserModeTests(rex86::test::Context& context)
{
    using rex86::FaultKind;
    using rex86::Gpr;
    using rex86::StopReason;
    constexpr std::uint32_t kBase = Machine::kCodeBase;

    // The privileged instructions: kPrivilegedInstruction at their address.
    CheckRefused(context, {0xFA}, 0, FaultKind::kPrivilegedInstruction, kBase);         // cli
    CheckRefused(context, {0xFB}, 0, FaultKind::kPrivilegedInstruction, kBase);         // sti
    CheckRefused(context, {0xF4}, 0, FaultKind::kPrivilegedInstruction, kBase);         // hlt
    CheckRefused(context, {0xE4, 0x60}, 0, FaultKind::kPrivilegedInstruction, kBase);   // in al, 0x60
    CheckRefused(context, {0xEC}, 0, FaultKind::kPrivilegedInstruction, kBase);         // in al, dx
    CheckRefused(context, {0x66, 0xED}, 0, FaultKind::kPrivilegedInstruction, kBase);   // in ax, dx
    CheckRefused(context, {0xE6, 0x42}, 0, FaultKind::kPrivilegedInstruction, kBase);   // out 0x42, al
    CheckRefused(context, {0xEF}, 0, FaultKind::kPrivilegedInstruction, kBase);         // out dx, eax
    CheckRefused(context, {0x6C}, 0, FaultKind::kPrivilegedInstruction, kBase);         // insb
    CheckRefused(context, {0xF3, 0x6D}, 0, FaultKind::kPrivilegedInstruction, kBase);   // rep insd
    CheckRefused(context, {0x6E}, 0, FaultKind::kPrivilegedInstruction, kBase);         // outsb
    CheckRefused(context, {0xF3, 0x66, 0x6F}, 0, FaultKind::kPrivilegedInstruction,
                 kBase);                                                                // rep outsw
    // After a retired instruction: the fault names the refused one.
    CheckRefused(context, {0x90, 0xFA}, 1, FaultKind::kPrivilegedInstruction, kBase + 1);

    // INT imm8 through a DPL 0 gate: #GP with the IDT error code.
    CheckRefused(context, {0xCD, 0x21}, 0, FaultKind::kGeneralProtection, 0x21u * 8u + 2u);
    CheckRefused(context, {0xCD, 0x31}, 0, FaultKind::kGeneralProtection, 0x31u * 8u + 2u);
    CheckRefused(context, {0xCD, 0x03}, 0, FaultKind::kGeneralProtection, 0x03u * 8u + 2u);

    {
        // INT3 is unchanged: it retires and stops with vector 3.
        Machine m({0xCC});
        const rex86::Event event = m.cpu.Run(100);
        REX86_CHECK(context, event.reason == StopReason::kSoftwareInterrupt);
        REX86_CHECK_EQ(context, event.vector, std::uint8_t{3});
        REX86_CHECK_EQ(context, m.cpu.state().eip, kBase + 1);
    }
    {
        // POPFD keeps IF and IOPL but takes the other flags.
        Machine m({0x68, 0x01, 0x30, 0x00, 0x00,  // push 0x00003001 (IOPL 3, CF, IF off)
                   0x9D,                          // popfd
                   0xFA});                        // cli: stops the run
        const rex86::Event event = m.cpu.Run(100);
        REX86_CHECK(context, event.fault_kind == FaultKind::kPrivilegedInstruction);
        const std::uint32_t eflags = m.cpu.state().eflags;
        REX86_CHECK(context, (eflags & rex86::kEflagsInterrupt) != 0);
        REX86_CHECK_EQ(context, eflags & 0x3000u, 0u);
        REX86_CHECK_EQ(context, eflags & rex86::kEflagsCarry, rex86::kEflagsCarry);
    }
    {
        // IRETD likewise.
        Machine m({0x68, 0x01, 0x30, 0x00, 0x00,  // push eflags image
                   0x6A, 0x08,                    // push cs selector
                   0x68, 0x00, 0x20, 0x00, 0x00,  // push 0x2000
                   0xCF});                        // iretd
        m.buffer[0x2000] = 0xFA;  // cli at the target stops the run
        const rex86::Event event = m.cpu.Run(100);
        REX86_CHECK(context, event.fault_kind == FaultKind::kPrivilegedInstruction);
        REX86_CHECK_EQ(context, event.fault_address, 0x2000u);
        const std::uint32_t eflags = m.cpu.state().eflags;
        REX86_CHECK(context, (eflags & rex86::kEflagsInterrupt) != 0);
        REX86_CHECK_EQ(context, eflags & 0x3000u, 0u);
        REX86_CHECK_EQ(context, eflags & rex86::kEflagsCarry, rex86::kEflagsCarry);
    }
    {
        // Off by default: CLI runs and INT n stops after retiring, as before.
        Machine m({0xFA, 0xCD, 0x21}, false);
        const rex86::Event event = m.cpu.Run(100);
        REX86_CHECK(context, event.reason == StopReason::kSoftwareInterrupt);
        REX86_CHECK_EQ(context, event.vector, std::uint8_t{0x21});
        REX86_CHECK_EQ(context, m.cpu.state().eflags & rex86::kEflagsInterrupt, 0u);
        REX86_CHECK_EQ(context, m.cpu.state().eip, kBase + 3);
    }
    {
        // A translated block reaches the refusal the same way: the IR
        // frontend leaves these instructions to the interpreter.
        Machine m({0x40,              // inc eax
                   0x41,              // inc ecx
                   0x81, 0xF9, 0x00, 0x01, 0x00, 0x00,  // cmp ecx, 0x100
                   0x75, 0xF6,        // jnz back to inc eax
                   0xFA});            // cli
        rex86::TranslationOptions options;
        options.mode = rex86::TranslationMode::kEvaluator;
        options.threshold = 0;
        REX86_CHECK(context, m.cpu.SetTranslation(options));
        const rex86::Event event = m.cpu.Run(100000);
        REX86_CHECK(context, event.reason == StopReason::kFault);
        REX86_CHECK(context, event.fault_kind == FaultKind::kPrivilegedInstruction);
        REX86_CHECK_EQ(context, m.cpu.state().eip, kBase + 10);
        REX86_CHECK_EQ(context, m.cpu.state().Get(Gpr::kEcx), 0x100u);
    }
}
