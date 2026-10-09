#include <cstdint>
#include <initializer_list>
#include <vector>

#include "interp/interpreter.h"
#include "rex86/cpu.h"
#include "test_support.h"

namespace
{

using rex86::Gpr;
using rex86::StopReason;

// A host whose port writes act on the Cpu in the middle of a Run, as a
// device model raising an interrupt or a debugger asking to stop would.
class ActingEnvironment final : public rex86::Environment
{
public:
    bool LoadDescriptor(std::uint16_t, rex86::Descriptor* d) override
    {
        *d = rex86::Descriptor{};
        return true;
    }
    bool PortRead(std::uint16_t, std::uint8_t, std::uint32_t*) override { return false; }
    bool PortWrite(std::uint16_t port, std::uint8_t, std::uint32_t) override
    {
        if (port == 0x10)
        {
            cpu->RaiseInterrupt(0x20);
        }
        else if (port == 0x11)
        {
            cpu->RequestStop();
        }
        return true;
    }
    bool InterruptTarget(std::uint8_t, std::uint16_t*, std::uint32_t*) override { return false; }
    std::uint64_t ReadTimeStampCounter() override { return 0; }
    void Cpuid(std::uint32_t, std::uint32_t, std::uint32_t r[4]) override
    {
        r[0] = r[1] = r[2] = r[3] = 0;
    }
    rex86::Cpu* cpu = nullptr;
};

constexpr std::uint32_t kWarmUp = 0x2000;
constexpr std::uint32_t kCode = 0x1000;

// A Cpu over 64 KiB of read-write-execute memory. With warm set, it first
// runs a 100-iteration loop so that its decode cache exists and the code
// under test runs through it.
struct Rig
{
    std::vector<std::uint8_t> buffer = std::vector<std::uint8_t>(0x10000, 0x90);
    rex86::GuestMemory memory{buffer.data(), 0x10000};
    ActingEnvironment environment;
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
        cpu.state().Set(Gpr::kEax, 0);
        cpu.state().Set(Gpr::kEcx, 0);
        cpu.state().eip = kCode;
    }

    void Put(std::uint32_t at, std::initializer_list<std::uint8_t> bytes)
    {
        for (const std::uint8_t b : bytes) buffer[at++] = b;
    }
};

}  // namespace

void RunBlockTests(rex86::test::Context& context)
{
    for (const bool warm : {false, true})
    {
        // Straight-line code falling into a gate stops before the gate's
        // instruction, mid-block.
        {
            Rig rig(warm);
            rig.Put(kCode, {0x40, 0x40, 0x40, 0x40, 0xF4});  // inc eax x4; hlt
            rig.cpu.RegisterGate(kCode + 2);
            const rex86::Event event = rig.cpu.Run(100);
            REX86_CHECK(context, event.reason == StopReason::kGate);
            REX86_CHECK_EQ(context, event.gate_address, kCode + 2);
            REX86_CHECK_EQ(context, event.steps, std::uint64_t{2});
            REX86_CHECK_EQ(context, rig.cpu.state().Get(Gpr::kEax), 2u);
        }
        // A filter hit that is not a gate (another address sharing the
        // filter bit) only sends the block back to the loop, which runs on.
        {
            Rig rig(warm);
            rig.Put(kCode, {0x40, 0x40, 0x40, 0x40, 0xF4});
            const std::uint32_t target = kCode + 2;
            std::uint32_t alias = 0x100000;
            while (rex86::interp::GateFilter::Bit(alias) != rex86::interp::GateFilter::Bit(target))
            {
                ++alias;
            }
            rig.cpu.RegisterGate(alias);
            const rex86::Event event = rig.cpu.Run(100);
            REX86_CHECK(context, event.reason == StopReason::kHalted);
            REX86_CHECK_EQ(context, rig.cpu.state().Get(Gpr::kEax), 4u);
        }
        // Unregistering one gate keeps the others in the rebuilt filter.
        {
            Rig rig(warm);
            rig.Put(kCode, {0x40, 0x40, 0x40, 0x40, 0xF4});
            rig.cpu.RegisterGate(kCode + 1);
            rig.cpu.RegisterGate(kCode + 3);
            rig.cpu.UnregisterGate(kCode + 1);
            const rex86::Event event = rig.cpu.Run(100);
            REX86_CHECK(context, event.reason == StopReason::kGate);
            REX86_CHECK_EQ(context, event.gate_address, kCode + 3);
            REX86_CHECK_EQ(context, rig.cpu.state().Get(Gpr::kEax), 3u);
        }
        // An interrupt the host raises during an instruction is taken at
        // the very next boundary when IF is set.
        {
            Rig rig(warm);
            rig.Put(kCode, {0xE6, 0x10, 0x41, 0x41, 0xF4});  // out 0x10, al; inc ecx x2; hlt
            rig.cpu.state().eflags |= rex86::kEflagsInterrupt;
            const rex86::Event event = rig.cpu.Run(100);
            REX86_CHECK(context, event.reason == StopReason::kSoftwareInterrupt);
            REX86_CHECK_EQ(context, static_cast<unsigned>(event.vector), 0x20u);
            REX86_CHECK_EQ(context, event.steps, std::uint64_t{1});
            REX86_CHECK_EQ(context, rig.cpu.state().Get(Gpr::kEcx), 0u);
        }
        // With IF clear the interrupt waits; STI's shadow lets one more
        // instruction run before delivery.
        {
            Rig rig(warm);
            rig.Put(kCode, {0x41, 0x41, 0xFB, 0x41, 0x41, 0xF4});  // inc ecx x2; sti; inc ecx x2; hlt
            rig.cpu.RaiseInterrupt(0x20);
            const rex86::Event event = rig.cpu.Run(100);
            REX86_CHECK(context, event.reason == StopReason::kSoftwareInterrupt);
            REX86_CHECK_EQ(context, event.steps, std::uint64_t{4});
            REX86_CHECK_EQ(context, rig.cpu.state().Get(Gpr::kEcx), 3u);
            // Cleared, the block runs on to HLT.
            rig.cpu.ClearPendingInterrupt(0x20);
            REX86_CHECK(context, rig.cpu.Run(100).reason == StopReason::kHalted);
            REX86_CHECK_EQ(context, rig.cpu.state().Get(Gpr::kEcx), 4u);
        }
        // A stop requested during an instruction ends the Run at the next
        // boundary, and only that Run.
        {
            Rig rig(warm);
            rig.Put(kCode, {0xE6, 0x11, 0x41, 0x41, 0xF4});  // out 0x11, al; inc ecx x2; hlt
            const rex86::Event event = rig.cpu.Run(100);
            REX86_CHECK(context, event.reason == StopReason::kStopRequested);
            REX86_CHECK_EQ(context, event.steps, std::uint64_t{1});
            REX86_CHECK_EQ(context, rig.cpu.state().Get(Gpr::kEcx), 0u);
            REX86_CHECK(context, rig.cpu.Run(100).reason == StopReason::kHalted);
            REX86_CHECK_EQ(context, rig.cpu.state().Get(Gpr::kEcx), 2u);
        }
        // The budget is exact inside a block.
        {
            Rig rig(warm);
            rig.Put(kCode, {0x41, 0xEB, 0xFD});  // L: inc ecx; jmp L
            const rex86::Event event = rig.cpu.Run(7);
            REX86_CHECK(context, event.reason == StopReason::kBudgetExhausted);
            REX86_CHECK_EQ(context, event.steps, std::uint64_t{7});
            REX86_CHECK_EQ(context, rig.cpu.state().Get(Gpr::kEcx), 4u);
        }
        // Self-modifying code within one block: the loop rewrites its own
        // first instruction, already decoded as inc eax, into dec eax, and
        // the next pass runs the new one.
        {
            Rig rig(warm);
            rig.Put(kCode, {0x40,                                     // L: inc eax
                            0x41,                                     // inc ecx
                            0x83, 0xF9, 0x02,                         // cmp ecx, 2
                            0x74, 0x09,                               // je done
                            0xC6, 0x05, 0x00, 0x10, 0x00, 0x00, 0x48, // mov byte [L], 0x48
                            0xEB, 0xF0,                               // jmp L
                            0xF4});                                   // done: hlt
            REX86_CHECK(context, rig.cpu.Run(100).reason == StopReason::kHalted);
            REX86_CHECK_EQ(context, rig.cpu.state().Get(Gpr::kEax), 0u);
        }
        // A fault mid-block reports the instructions retired before it and
        // leaves EIP at the faulting instruction.
        {
            Rig rig(warm);
            // inc ecx; mov eax, [0xFFFF0]; hlt  (beyond the 64 KiB memory)
            rig.Put(kCode, {0x41, 0xA1, 0xF0, 0xFF, 0x0F, 0x00, 0xF4});
            const rex86::Event event = rig.cpu.Run(100);
            REX86_CHECK(context, event.reason == StopReason::kFault);
            REX86_CHECK(context, event.fault_kind == rex86::FaultKind::kAccessViolation);
            REX86_CHECK_EQ(context, event.steps, std::uint64_t{1});
            REX86_CHECK_EQ(context, rig.cpu.state().eip, kCode + 1);
        }
    }
}
