#include "rex86/cpu.h"

#include <cstdint>
#include <vector>

#include "test_support.h"

namespace
{

// A host that supplies flat descriptors, answers no port and routes no
// interrupt, so every path that needs the host is observable.
class RecordingEnvironment final : public rex86::Environment
{
public:
    bool LoadDescriptor(std::uint16_t selector, rex86::Descriptor* descriptor) override
    {
        ++descriptor_loads;
        last_selector = selector;
        *descriptor = rex86::Descriptor{};
        return true;
    }

    bool PortRead(std::uint16_t, std::uint8_t, std::uint32_t*) override
    {
        return false;
    }

    bool PortWrite(std::uint16_t, std::uint8_t, std::uint32_t) override
    {
        return false;
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

    int descriptor_loads = 0;
    std::uint16_t last_selector = 0;
};

}  // namespace

void RunCpuTests(rex86::test::Context& context)
{
    using rex86::Cpu;
    using rex86::Event;
    using rex86::StopReason;

    std::vector<std::uint8_t> buffer(2 * rex86::kGuestPageSize, 0);
    rex86::GuestMemory memory(buffer.data(), static_cast<std::uint32_t>(buffer.size()));
    RecordingEnvironment environment;
    rex86::Features features;
    Cpu cpu(&memory, &environment, features);

    // A fresh Cpu holds reset state and no engine.
    REX86_CHECK_EQ(context, cpu.state().eflags, rex86::kEflagsReserved1);
    REX86_CHECK(context, cpu.ActiveEngine() == rex86::Engine::kNone);
    REX86_CHECK(context, cpu.memory() == &memory);
    REX86_CHECK(context, cpu.environment() == &environment);
    REX86_CHECK(context, cpu.code_cache() == nullptr);
    REX86_CHECK(context, cpu.features().x87);
    REX86_CHECK(context, !cpu.features().sse);

    // Without an engine, Run reports exactly that, retiring nothing. It does
    // not pretend the budget ran out.
    Event event = cpu.Run(1000);
    REX86_CHECK(context, event.reason == StopReason::kNoEngine);
    REX86_CHECK_EQ(context, event.instructions_retired, std::uint64_t{0});
    REX86_CHECK(context, cpu.Step().reason == StopReason::kNoEngine);

    // RequestStop is honoured by the next Run and then forgotten.
    cpu.RequestStop();
    REX86_CHECK(context, cpu.Run(1).reason == StopReason::kStopRequested);
    REX86_CHECK(context, cpu.Run(1).reason == StopReason::kNoEngine);

    // Gates are a set of linear addresses.
    cpu.RegisterGate(0x7FFE0000u);
    cpu.RegisterGate(0x7FFE0010u);
    cpu.RegisterGate(0x7FFE0000u);
    REX86_CHECK_EQ(context, cpu.gate_count(), std::size_t{2});
    REX86_CHECK(context, cpu.IsGate(0x7FFE0010u));
    REX86_CHECK(context, !cpu.IsGate(0x7FFE0004u));
    cpu.UnregisterGate(0x7FFE0000u);
    REX86_CHECK(context, !cpu.IsGate(0x7FFE0000u));
    REX86_CHECK_EQ(context, cpu.gate_count(), std::size_t{1});

    // Pending interrupts: highest vector first, cleared one at a time.
    std::uint8_t vector = 0;
    REX86_CHECK(context, !cpu.HasPendingInterrupt());
    REX86_CHECK(context, !cpu.NextPendingInterrupt(&vector));
    cpu.RaiseInterrupt(0x08);
    cpu.RaiseInterrupt(0x70);
    cpu.RaiseInterrupt(0x08);
    REX86_CHECK(context, cpu.HasPendingInterrupt());
    REX86_CHECK(context, cpu.NextPendingInterrupt(&vector));
    REX86_CHECK_EQ(context, vector, std::uint8_t{0x70});
    cpu.ClearPendingInterrupt(0x70);
    REX86_CHECK(context, cpu.NextPendingInterrupt(&vector));
    REX86_CHECK_EQ(context, vector, std::uint8_t{0x08});
    cpu.ClearPendingInterrupt(0x08);
    REX86_CHECK(context, !cpu.HasPendingInterrupt());

    // InvalidateCode clears the translated flag and nothing else.
    REX86_CHECK(context, memory.pages().Set(0, 0x1000, rex86::kPageReadWriteExecute | rex86::PageFlag::kTranslated));
    cpu.InvalidateCode(0x100, 0x10);
    REX86_CHECK(context, !rex86::Has(memory.pages().Get(0x100), rex86::PageFlag::kTranslated));
    REX86_CHECK(context, rex86::Has(memory.pages().Get(0x100), rex86::PageFlag::kExecute));

    // The event carries guest values only: no pointers anywhere in it.
    Event filled;
    filled.reason = StopReason::kGate;
    filled.gate_address = 0x7FFE0010u;
    REX86_CHECK_EQ(context, filled.gate_address, std::uint32_t{0x7FFE0010u});
    REX86_CHECK(context, filled.fault_kind == rex86::FaultKind::kNone);

    // The environment was not consulted by a core without an engine.
    REX86_CHECK_EQ(context, environment.descriptor_loads, 0);
}
