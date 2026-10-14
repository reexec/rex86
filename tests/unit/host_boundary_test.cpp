// Features::host_boundaries (design #70): instructions the host names stop
// the core before they run, are asked about once per decode, run once on
// request, and end translated blocks.

#include <cstdint>
#include <initializer_list>
#include <vector>

#include "rex86/cpu.h"
#include "test_support.h"

namespace
{

// A flat host whose boundary is every one-byte NOP (0x90).
class BoundaryEnvironment final : public rex86::Environment
{
public:
    bool LoadDescriptor(std::uint16_t, rex86::Descriptor* descriptor) override
    {
        *descriptor = rex86::Descriptor{};
        descriptor->executable = true;
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

    bool IsHostBoundary(const std::uint8_t* bytes, std::uint32_t length) override
    {
        ++questions;
        return length == 1U && bytes[0] == 0x90U;
    }

    int questions = 0;
};

rex86::Features Boundaries(const bool on)
{
    rex86::Features features;
    features.host_boundaries = on;
    return features;
}

struct Machine
{
    std::vector<std::uint8_t> buffer;
    rex86::GuestMemory memory;
    BoundaryEnvironment environment;
    rex86::Cpu cpu;

    Machine(const std::initializer_list<std::uint8_t> program, const bool on = true)
        : buffer(16 * rex86::kGuestPageSize, 0),
          memory(buffer.data(), static_cast<std::uint32_t>(buffer.size())),
          cpu(&memory, &environment, Boundaries(on))
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

void RunHostBoundaryTests(rex86::test::Context& context)
{
    using rex86::Gpr;
    using rex86::StopReason;
    constexpr std::uint32_t kBase = Machine::kCodeBase;

    {
        // Off by default: the host is not asked and the NOP runs.
        Machine m({0x90, 0xF4}, false);
        REX86_CHECK(context, m.cpu.Run(100).reason == StopReason::kHalted);
        REX86_CHECK_EQ(context, m.environment.questions, 0);
    }
    {
        // On: the core stops before the NOP, which has not run, and stops
        // there again until told to run it once.
        Machine m({0xB8, 0x01, 0x00, 0x00, 0x00,  // mov eax, 1
                   0x90,                          // nop: a boundary
                   0xB8, 0x02, 0x00, 0x00, 0x00,  // mov eax, 2
                   0xF4});                        // hlt
        rex86::Event event = m.cpu.Run(100);
        REX86_CHECK(context, event.reason == StopReason::kHostBoundary);
        REX86_CHECK_EQ(context, event.gate_address, kBase + 5);
        REX86_CHECK_EQ(context, event.steps, std::uint64_t{1});
        REX86_CHECK_EQ(context, m.cpu.state().eip, kBase + 5);
        REX86_CHECK_EQ(context, m.cpu.state().Get(Gpr::kEax), 1u);
        REX86_CHECK(context, m.cpu.Run(100).reason == StopReason::kHostBoundary);
        m.cpu.SkipHostBoundaryOnce();
        event = m.cpu.Run(100);
        REX86_CHECK(context, event.reason == StopReason::kHalted);
        REX86_CHECK_EQ(context, m.cpu.state().Get(Gpr::kEax), 2u);
    }
    {
        // The skip applies only where it was asked for.
        Machine m({0x90, 0x90, 0xF4});
        REX86_CHECK(context, m.cpu.Run(100).reason == StopReason::kHostBoundary);
        m.cpu.SkipHostBoundaryOnce();
        m.cpu.state().eip = kBase + 1;
        const rex86::Event event = m.cpu.Run(100);
        REX86_CHECK(context, event.reason == StopReason::kHostBoundary);
        REX86_CHECK_EQ(context, event.gate_address, kBase + 1);
    }
    {
        // A cached decode is asked about once: a loop of 50,000 passes
        // through three instructions asks far fewer than 150,000 times.
        Machine m({0xB9, 0x50, 0xC3, 0x00, 0x00,  // mov ecx, 50000
                   0x49,                          // dec ecx
                   0x75, 0xFD,                    // jnz back to dec
                   0xF4});                        // hlt
        REX86_CHECK(context, m.cpu.Run(1000000).reason == StopReason::kHalted);
        REX86_CHECK_EQ(context, m.cpu.state().Get(Gpr::kEcx), 0u);
        REX86_CHECK(context, m.environment.questions < 50000);
    }
    {
        // A translated block ends before a boundary: the loop translates,
        // and the NOP after it still stops the core.
        Machine m({0xB9, 0x00, 0x01, 0x00, 0x00,  // mov ecx, 256
                   0x40,                          // inc eax
                   0x49,                          // dec ecx
                   0x75, 0xFC,                    // jnz back to inc
                   0x90,                          // nop: a boundary
                   0xF4});                        // hlt
        rex86::TranslationOptions options;
        options.mode = rex86::TranslationMode::kEvaluator;
        options.threshold = 0;
        REX86_CHECK(context, m.cpu.SetTranslation(options));
        const rex86::Event event = m.cpu.Run(100000);
        REX86_CHECK(context, event.reason == StopReason::kHostBoundary);
        REX86_CHECK_EQ(context, event.gate_address, kBase + 9);
        REX86_CHECK_EQ(context, m.cpu.state().Get(Gpr::kEax), 256u);
    }
}
