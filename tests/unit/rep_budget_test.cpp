#include <cstdint>
#include <initializer_list>
#include <vector>

#include "rex86/cpu.h"
#include "test_support.h"

// Design #32: a REP string counts one step per iteration and may stop
// between iterations on the budget, a stop request or a pending interrupt.
namespace
{

using rex86::Gpr;
using rex86::StopReason;

// Serves every port with a counter, folds every write into a digest, and
// acts on the Cpu for two ports: 0x10 raises interrupt 0x20 and 0x11 asks
// to stop. With a handler set, interrupts are delivered there.
class RepEnvironment final : public rex86::Environment
{
public:
    // Flat; selector 0x08 is the code segment IRET loads back.
    bool LoadDescriptor(std::uint16_t selector, rex86::Descriptor* d) override
    {
        *d = rex86::Descriptor{};
        d->executable = selector == 0x08;
        return true;
    }
    bool PortRead(std::uint16_t, std::uint8_t, std::uint32_t* value) override
    {
        if (!serve_reads)
        {
            return false;
        }
        *value = next_read++;
        return true;
    }
    bool PortWrite(std::uint16_t port, std::uint8_t, std::uint32_t value) override
    {
        if (port == 0x10)
        {
            cpu->RaiseInterrupt(0x20);
        }
        else if (port == 0x11)
        {
            cpu->RequestStop();
        }
        written = written * 31u + value;
        return true;
    }
    bool InterruptTarget(std::uint8_t, std::uint16_t* cs, std::uint32_t* eip) override
    {
        if (handler == 0)
        {
            return false;
        }
        *cs = cpu->state().Seg(rex86::Segment::kCs).selector;
        *eip = handler;
        return true;
    }
    std::uint64_t ReadTimeStampCounter() override { return 0; }
    void Cpuid(std::uint32_t, std::uint32_t, std::uint32_t r[4]) override
    {
        r[0] = r[1] = r[2] = r[3] = 0;
    }

    rex86::Cpu* cpu = nullptr;
    bool serve_reads = true;
    std::uint32_t next_read = 0x5A;
    std::uint32_t written = 0;
    std::uint32_t handler = 0;
};

constexpr std::uint32_t kWarmUp = 0x2000;
constexpr std::uint32_t kCode = 0x1000;
constexpr std::uint32_t kSource = 0x4000;
constexpr std::uint32_t kDestination = 0x6000;

// A Cpu over 64 KiB of read-write-execute memory whose source and
// destination areas hold the same pattern, but for a differing byte at
// destination + 23 and a 0x77 at destination + 30 (for REPE CMPS and REPNE
// SCAS). With warm set, a loop first makes the decode cache.
struct Rig
{
    std::vector<std::uint8_t> buffer = std::vector<std::uint8_t>(0x10000, 0x90);
    rex86::GuestMemory memory{buffer.data(), 0x10000};
    RepEnvironment environment;
    rex86::Cpu cpu{&memory, &environment, rex86::Features{}};

    explicit Rig(const bool warm)
    {
        memory.pages().Set(0, 0x10000, rex86::kPageReadWriteExecute);
        environment.cpu = &cpu;
        cpu.state().Set(Gpr::kEsp, 0xF000);
        if (warm)
        {
            // mov ecx, 100; L: dec ecx; jnz L; hlt
            Put(kWarmUp, {0xB9, 100, 0, 0, 0, 0x49, 0x75, 0xFD, 0xF4});
            cpu.state().eip = kWarmUp;
            cpu.Run(1000);
        }
        for (std::uint32_t i = 0; i < 0x2000; ++i)
        {
            buffer[kSource + i] = static_cast<std::uint8_t>(i * 7u + 3u);
            buffer[kDestination + i] = static_cast<std::uint8_t>(i * 7u + 3u);
        }
        buffer[kDestination + 23] ^= 0xFF;
        buffer[kDestination + 30] = 0x77;
        cpu.state().Set(Gpr::kEax, 0);
        cpu.state().Set(Gpr::kEcx, 0);
        cpu.state().eip = kCode;
    }

    void Put(std::uint32_t at, std::initializer_list<std::uint8_t> bytes)
    {
        for (const std::uint8_t b : bytes) buffer[at++] = b;
    }

    void Put(std::uint32_t at, const std::vector<std::uint8_t>& bytes)
    {
        for (const std::uint8_t b : bytes) buffer[at++] = b;
    }
};

std::vector<std::uint8_t> Imm32(const std::uint8_t opcode, const std::uint32_t value)
{
    return {opcode, static_cast<std::uint8_t>(value), static_cast<std::uint8_t>(value >> 8),
            static_cast<std::uint8_t>(value >> 16), static_cast<std::uint8_t>(value >> 24)};
}

// mov esi, source; mov edi, destination; mov ecx, count; <body>; hlt
std::vector<std::uint8_t> Program(const std::uint32_t source, const std::uint32_t destination,
                                  const std::uint32_t count,
                                  std::initializer_list<std::uint8_t> body)
{
    std::vector<std::uint8_t> code;
    for (const auto& part : {Imm32(0xBE, source), Imm32(0xBF, destination), Imm32(0xB9, count)})
    {
        code.insert(code.end(), part.begin(), part.end());
    }
    code.insert(code.end(), body.begin(), body.end());
    code.push_back(0xF4);
    return code;
}

// Runs the program uninterrupted, then again in Runs of each budget, and
// checks that the registers, memory, port traffic and step totals match.
void CheckEquivalence(rex86::test::Context& context, const bool warm,
                      const std::vector<std::uint8_t>& code)
{
    Rig whole(warm);
    whole.Put(kCode, code);
    const rex86::Event reference = whole.cpu.Run(100000);
    REX86_CHECK(context, reference.reason == StopReason::kHalted);

    for (const std::uint64_t budget : {1u, 2u, 7u})
    {
        Rig pieces(warm);
        pieces.Put(kCode, code);
        std::uint64_t steps = 0;
        rex86::Event event;
        for (int runs = 0; runs < 100000; ++runs)
        {
            event = pieces.cpu.Run(budget);
            steps += event.steps;
            if (event.reason != StopReason::kBudgetExhausted)
            {
                break;
            }
        }
        REX86_CHECK(context, event.reason == StopReason::kHalted);
        REX86_CHECK_EQ(context, steps, reference.steps);
        REX86_CHECK(context, pieces.cpu.state().gpr == whole.cpu.state().gpr);
        REX86_CHECK_EQ(context, pieces.cpu.state().eflags, whole.cpu.state().eflags);
        REX86_CHECK_EQ(context, pieces.cpu.state().eip, whole.cpu.state().eip);
        REX86_CHECK(context, pieces.buffer == whole.buffer);
        REX86_CHECK_EQ(context, pieces.environment.written, whole.environment.written);
        REX86_CHECK_EQ(context, pieces.environment.next_read, whole.environment.next_read);
    }
}

}  // namespace

void RunRepBudgetTests(rex86::test::Context& context)
{
    for (const bool warm : {false, true})
    {
        // Running a REP in pieces ends where running it at once does, for
        // every string instruction, both directions, 16-bit addressing,
        // REPE/REPNE ending early and a REP with no iteration.
        {
            const std::uint8_t kStd = 0xFD;
            const std::vector<std::vector<std::uint8_t>> programs = {
                Program(kSource, kDestination, 37, {0xF3, 0xA4}),                 // rep movsb
                Program(kSource + 0x100, kDestination + 0x100, 9,
                        {kStd, 0xF3, 0xA5}),                                       // std; rep movsd
                Program(kSource, kDestination, 11,
                        {0xB8, 0xCD, 0xAB, 0x34, 0x12, 0x66, 0xF3, 0xAB}),         // rep stosw
                Program(kSource, kDestination, 13, {0xF3, 0xAC}),                 // rep lodsb
                Program(kSource, kDestination, 40, {0xF3, 0xA6}),                 // repe cmpsb
                Program(kSource, kDestination, 50, {0xB0, 0x77, 0xF2, 0xAE}),     // repne scasb
                Program(0xFFF8, kDestination, 0xABCD0014, {0x67, 0xF3, 0xA4}),    // a16 rep movsb
                Program(kSource, kDestination, 9,
                        {0x66, 0xBA, 0x40, 0x00, 0xF3, 0x6C}),                     // rep insb
                Program(kSource, kDestination, 6,
                        {0x66, 0xBA, 0x40, 0x00, 0xF3, 0x6F}),                     // rep outsd
                Program(kSource, kDestination, 0, {0xF3, 0xA4}),                  // ecx = 0
                Program(kSource, kDestination, 5, {0xF3, 0xA4, 0x41, 0xF3, 0xA4}), // two REPs
            };
            for (const auto& code : programs)
            {
                CheckEquivalence(context, warm, code);
            }
        }
        // The budget runs out between iterations: the completed ones are
        // architectural and EIP stays at the instruction; the next Run
        // finishes it.
        {
            Rig rig(warm);
            rig.Put(kCode, Program(kSource, kDestination, 10, {0xF3, 0xAA}));  // rep stosb
            const std::uint32_t rep = kCode + 15;
            REX86_CHECK(context, !rig.cpu.InstructionInProgress());
            rex86::Event event = rig.cpu.Run(3 + 4);
            REX86_CHECK(context, event.reason == StopReason::kBudgetExhausted);
            REX86_CHECK_EQ(context, event.steps, std::uint64_t{7});
            REX86_CHECK_EQ(context, rig.cpu.state().eip, rep);
            REX86_CHECK_EQ(context, rig.cpu.state().Get(Gpr::kEcx), 6u);
            REX86_CHECK_EQ(context, rig.cpu.state().Get(Gpr::kEdi), kDestination + 4);
            REX86_CHECK(context, rig.cpu.InstructionInProgress());
            event = rig.cpu.Run(100);
            REX86_CHECK(context, event.reason == StopReason::kHalted);
            REX86_CHECK_EQ(context, event.steps, std::uint64_t{7});  // six iterations and HLT
            REX86_CHECK_EQ(context, rig.cpu.state().Get(Gpr::kEcx), 0u);
            REX86_CHECK(context, !rig.cpu.InstructionInProgress());
        }
        // Step runs one iteration, as a trap-flag single step does.
        {
            Rig rig(warm);
            rig.Put(kCode, {0xB9, 3, 0, 0, 0, 0xF3, 0xAA, 0xF4});  // mov ecx, 3; rep stosb; hlt
            rig.cpu.state().Set(Gpr::kEdi, kDestination);
            rig.cpu.Step();
            for (const std::uint32_t ecx : {2u, 1u})
            {
                const rex86::Event event = rig.cpu.Step();
                REX86_CHECK_EQ(context, event.steps, std::uint64_t{1});
                REX86_CHECK_EQ(context, rig.cpu.state().Get(Gpr::kEcx), ecx);
                REX86_CHECK_EQ(context, rig.cpu.state().eip, kCode + 5);
                REX86_CHECK(context, rig.cpu.InstructionInProgress());
            }
            rig.cpu.Step();
            REX86_CHECK_EQ(context, rig.cpu.state().eip, kCode + 7);
            REX86_CHECK(context, !rig.cpu.InstructionInProgress());
        }
        // A stop requested during an iteration ends the Run at the next
        // iteration boundary; the next Run continues the instruction.
        {
            Rig rig(warm);
            // mov dx, 0x11; rep outsb with ECX = 5
            rig.Put(kCode, Program(kSource, kDestination, 5, {0x66, 0xBA, 0x11, 0x00, 0xF3, 0x6E}));
            rex86::Event event = rig.cpu.Run(1000);
            REX86_CHECK(context, event.reason == StopReason::kStopRequested);
            REX86_CHECK_EQ(context, event.steps, std::uint64_t{5});  // four MOVs, one iteration
            REX86_CHECK_EQ(context, rig.cpu.state().Get(Gpr::kEcx), 4u);
            REX86_CHECK(context, rig.cpu.InstructionInProgress());
            // Every iteration asks again.
            for (int i = 0; i < 4; ++i)
            {
                event = rig.cpu.Run(1000);
                REX86_CHECK(context, event.reason == StopReason::kStopRequested);
            }
            REX86_CHECK_EQ(context, rig.cpu.state().Get(Gpr::kEcx), 0u);
            REX86_CHECK(context, rig.cpu.Run(1000).reason == StopReason::kHalted);
        }
        // An interrupt raised during an iteration is delivered between
        // iterations with the string instruction as the return address, and
        // the instruction continues after IRET.
        {
            Rig rig(warm);
            const std::uint32_t handler = 0x3000;
            // mov eax, [esp]; mov [ebx*4 + 0x8000], eax; inc ebx; iret
            rig.Put(handler, {0x8B, 0x04, 0x24, 0x89, 0x04, 0x9D, 0x00, 0x80, 0x00, 0x00, 0x43,
                              0xCF});
            rig.environment.handler = handler;
            // mov dx, 0x10; rep outsb with ECX = 4
            rig.Put(kCode, Program(kSource, kDestination, 4, {0x66, 0xBA, 0x10, 0x00, 0xF3, 0x6E}));
            const std::uint32_t rep = kCode + 19;
            rig.cpu.state().Set(Gpr::kEbx, 0);
            // A non-null CS, so that IRET may load it back.
            rig.cpu.state().Seg(rex86::Segment::kCs).selector = 0x08;
            rig.cpu.state().eflags |= rex86::kEflagsInterrupt;
            const rex86::Event event = rig.cpu.Run(1000);
            REX86_CHECK(context, event.reason == StopReason::kHalted);
            REX86_CHECK_EQ(context, rig.cpu.state().Get(Gpr::kEbx), 4u);
            REX86_CHECK_EQ(context, rig.cpu.state().Get(Gpr::kEcx), 0u);
            REX86_CHECK_EQ(context, rig.cpu.state().Get(Gpr::kEsi), kSource + 4);
            std::uint32_t returned[4] = {};
            for (std::uint32_t i = 0; i < 4; ++i)
            {
                rig.memory.Read32(0x8000 + 4 * i, &returned[i]);
            }
            // Three interrupts arrive mid-string, the fourth after the last
            // iteration retired the instruction.
            REX86_CHECK_EQ(context, returned[0], rep);
            REX86_CHECK_EQ(context, returned[1], rep);
            REX86_CHECK_EQ(context, returned[2], rep);
            REX86_CHECK_EQ(context, returned[3], rep + 2);
        }
        // A gate at a REP in progress does not fire when the REP continues
        // (it was checked when the REP started), but does once the host has
        // moved EIP and execution comes back to the instruction afresh.
        {
            Rig rig(warm);
            rig.Put(kCode, {0xF3, 0xAA, 0xF4});  // rep stosb; hlt
            rig.cpu.state().Set(Gpr::kEcx, 10);
            rig.cpu.state().Set(Gpr::kEdi, kDestination);
            REX86_CHECK(context, rig.cpu.Run(3).reason == StopReason::kBudgetExhausted);
            rig.cpu.RegisterGate(kCode);
            const rex86::Event event = rig.cpu.Run(100);
            REX86_CHECK(context, event.reason == StopReason::kHalted);
            REX86_CHECK_EQ(context, rig.cpu.state().Get(Gpr::kEcx), 0u);
        }
        {
            Rig rig(warm);
            rig.Put(kCode, {0xF3, 0xAA, 0xF4});           // rep stosb; hlt
            rig.Put(0x3000, {0x90, 0xE9, 0xFA, 0xDF, 0xFF, 0xFF});  // nop; jmp kCode
            rig.cpu.state().Set(Gpr::kEcx, 10);
            rig.cpu.state().Set(Gpr::kEdi, kDestination);
            REX86_CHECK(context, rig.cpu.Run(3).reason == StopReason::kBudgetExhausted);
            rig.cpu.RegisterGate(kCode);
            rig.cpu.state().eip = 0x3000;
            REX86_CHECK(context, !rig.cpu.InstructionInProgress());
            const rex86::Event event = rig.cpu.Run(100);
            REX86_CHECK(context, event.reason == StopReason::kGate);
            REX86_CHECK_EQ(context, event.gate_address, kCode);
            REX86_CHECK_EQ(context, event.steps, std::uint64_t{2});
            REX86_CHECK_EQ(context, rig.cpu.state().Get(Gpr::kEcx), 7u);
        }
        // A REP with no iteration is one step.
        {
            Rig rig(warm);
            rig.Put(kCode, {0xF3, 0xA4, 0xF4});  // rep movsb; hlt (ECX = 0)
            const rex86::Event event = rig.cpu.Run(1);
            REX86_CHECK(context, event.reason == StopReason::kBudgetExhausted);
            REX86_CHECK_EQ(context, event.steps, std::uint64_t{1});
            REX86_CHECK_EQ(context, rig.cpu.state().eip, kCode + 2);
            REX86_CHECK(context, !rig.cpu.InstructionInProgress());
        }
        // A fault mid-string counts the iterations completed before it.
        {
            Rig rig(warm);
            rig.Put(kCode, {0xF3, 0xAA, 0xF4});  // rep stosb; hlt
            rig.cpu.state().Set(Gpr::kEcx, 5);
            rig.cpu.state().Set(Gpr::kEdi, 0xFFFE);  // the third store is beyond memory
            const rex86::Event event = rig.cpu.Run(100);
            REX86_CHECK(context, event.reason == StopReason::kFault);
            REX86_CHECK_EQ(context, event.steps, std::uint64_t{2});
            REX86_CHECK_EQ(context, rig.cpu.state().Get(Gpr::kEcx), 3u);
            REX86_CHECK_EQ(context, rig.cpu.state().eip, kCode);
        }
        // A declined INS before any iteration counts none.
        {
            Rig rig(warm);
            rig.environment.serve_reads = false;
            rig.Put(kCode, {0xF3, 0x6C, 0xF4});  // rep insb; hlt
            rig.cpu.state().Set(Gpr::kEcx, 3);
            rig.cpu.state().Set(Gpr::kEdi, kDestination);
            const rex86::Event event = rig.cpu.Run(100);
            REX86_CHECK(context, event.reason == StopReason::kPortIo);
            REX86_CHECK_EQ(context, event.steps, std::uint64_t{0});
            REX86_CHECK_EQ(context, rig.cpu.state().Get(Gpr::kEcx), 3u);
        }
    }
}
