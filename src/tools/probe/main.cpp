#include <cstdint>
#include <cstdio>
#include <vector>

#include "rex86/cpu.h"
#include "rex86/version.h"

// Prints what this build of the core is, one key=value line, in the form
// rePIU's repiu_core_probe established: the same program is run on every
// host and the lines are compared. It opens no window and reads no file, so
// it runs under Node for the wasm32 build as well.
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
};

const char* EngineName(rex86::Engine engine)
{
    switch (engine)
    {
    case rex86::Engine::kNone:
        return "none";
    case rex86::Engine::kInterpreter:
        return "interpreter";
    case rex86::Engine::kTranslator:
        return "translator";
    }
    return "unknown";
}

const char* StopReasonName(rex86::StopReason reason)
{
    switch (reason)
    {
    case rex86::StopReason::kNoEngine:
        return "no_engine";
    case rex86::StopReason::kBudgetExhausted:
        return "budget_exhausted";
    case rex86::StopReason::kGate:
        return "gate";
    case rex86::StopReason::kSoftwareInterrupt:
        return "software_interrupt";
    case rex86::StopReason::kPortIo:
        return "port_io";
    case rex86::StopReason::kFault:
        return "fault";
    case rex86::StopReason::kHalted:
        return "halted";
    case rex86::StopReason::kStopRequested:
        return "stop_requested";
    }
    return "unknown";
}

}  // namespace

int main()
{
    std::vector<std::uint8_t> buffer(rex86::kGuestPageSize, 0);
    rex86::GuestMemory memory(buffer.data(), static_cast<std::uint32_t>(buffer.size()));
    NullEnvironment environment;
    rex86::Cpu cpu(&memory, &environment, rex86::Features{});
    const rex86::Event event = cpu.Run(1);

    std::printf("[rex86-probe] version=%s pointer_bytes=%u page_size=%u cpu_state_bytes=%u\n",
                rex86::VersionString(),
                static_cast<unsigned>(sizeof(void*)),
                static_cast<unsigned>(rex86::kGuestPageSize),
                static_cast<unsigned>(sizeof(rex86::CpuState)));
    std::printf("[rex86-probe] engine=%s run=%s retired=%llu eflags=0x%08x x87_cw=0x%04x\n",
                EngineName(cpu.ActiveEngine()),
                StopReasonName(event.reason),
                static_cast<unsigned long long>(event.instructions_retired),
                cpu.state().eflags,
                cpu.state().x87.control_word);
    std::printf("[rex86-probe] result=ok\n");
    return 0;
}
