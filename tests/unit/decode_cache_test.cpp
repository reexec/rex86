#include <cstdint>
#include <initializer_list>
#include <vector>

#include "interp/decode_cache.h"
#include "interp/interpreter.h"
#include "rex86/cpu.h"
#include "test_support.h"

namespace
{

using rex86::FaultKind;
using rex86::Gpr;
using rex86::interp::DecodeCache;
using rex86::interp::StepStatus;

class NullEnvironment final : public rex86::Environment
{
public:
    bool LoadDescriptor(std::uint16_t, rex86::Descriptor* d) override
    {
        *d = rex86::Descriptor{};
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
    void OnCodePageWritten(std::uint32_t) override { ++writes; }
    int writes = 0;
};

// 64 KiB of read-write-execute memory, flat state, steps driven by hand
// through one or two caches.
struct Rig
{
    std::vector<std::uint8_t> buffer = std::vector<std::uint8_t>(0x10000, 0x90);
    rex86::GuestMemory memory{buffer.data(), 0x10000};
    NullEnvironment environment;
    rex86::CpuState state;
    rex86::Features features;

    Rig()
    {
        memory.pages().Set(0, 0x10000, rex86::kPageReadWriteExecute);
        state.Reset();
        state.Set(Gpr::kEsp, 0xF000);
    }

    void Put(std::uint32_t at, std::initializer_list<std::uint8_t> bytes)
    {
        for (const std::uint8_t b : bytes) buffer[at++] = b;
    }

    rex86::interp::StepResult StepAt(std::uint32_t eip, DecodeCache* cache, rex86::CpuState* s = nullptr)
    {
        rex86::CpuState& target = s != nullptr ? *s : state;
        target.eip = eip;
        return rex86::interp::Step(target, memory, environment, features, cache);
    }
};

}  // namespace

void RunDecodeCacheTests(rex86::test::Context& context)
{
    // The page table's generation rises when kTranslated clears, by Remove
    // or Set, and stays otherwise.
    {
        rex86::PageAttributeTable pages(0x3000);
        pages.Set(0, 0x3000, rex86::kPageReadWriteExecute);
        REX86_CHECK_EQ(context, pages.Generation(0x1000), 0u);
        pages.Add(0x1000, 1, rex86::PageFlag::kTranslated);
        pages.Add(0x1000, 1, rex86::PageFlag::kTranslated);
        REX86_CHECK_EQ(context, pages.Generation(0x1000), 0u);
        pages.Remove(0x1000, 1, rex86::PageFlag::kTranslated);
        REX86_CHECK_EQ(context, pages.Generation(0x1000), 1u);
        pages.Remove(0x1000, 1, rex86::PageFlag::kTranslated);
        REX86_CHECK_EQ(context, pages.Generation(0x1000), 1u);
        pages.Add(0x1000, 1, rex86::PageFlag::kTranslated);
        pages.Set(0x1000, 1, rex86::kPageReadWrite);
        REX86_CHECK_EQ(context, pages.Generation(0x1000), 2u);
        REX86_CHECK_EQ(context, pages.Generation(0x2000), 0u);
        REX86_CHECK_EQ(context, pages.Generation(0x9000), 0u);
    }
    // A hit returns the decode and the page carries kTranslated.
    {
        Rig rig;
        DecodeCache cache;
        rig.Put(0x1010, {0x40});  // inc eax
        rig.StepAt(0x1010, &cache);
        REX86_CHECK(context, rex86::Has(rig.memory.pages().Get(0x1010), rex86::PageFlag::kTranslated));
        REX86_CHECK(context, cache.Lookup(0x1010, 0x1010, true, rig.state.Seg(rex86::Segment::kCs),
                                          rig.memory) != nullptr);
        rig.StepAt(0x1010, &cache);
        REX86_CHECK_EQ(context, rig.state.Get(Gpr::kEax), 2u);
    }
    // Self-modifying code: a guest store rewrites a cached instruction on
    // its own page, and another cached instruction on the same page.
    {
        Rig rig;
        DecodeCache cache;
        rig.Put(0x1000, {0xC6, 0x05, 0x10, 0x10, 0x00, 0x00, 0x48});  // mov byte [0x1010], 0x48
        rig.Put(0x1008, {0xC6, 0x05, 0x20, 0x10, 0x00, 0x00, 0x49});  // mov byte [0x1020], 0x49
        rig.Put(0x1010, {0x40});  // inc eax
        rig.Put(0x1020, {0x41});  // inc ecx
        rig.StepAt(0x1010, &cache);
        rig.StepAt(0x1020, &cache);
        REX86_CHECK_EQ(context, rig.state.Get(Gpr::kEax), 1u);
        REX86_CHECK_EQ(context, rig.state.Get(Gpr::kEcx), 1u);
        const int writes = rig.environment.writes;
        rig.StepAt(0x1000, &cache);  // inc eax -> dec eax
        REX86_CHECK_EQ(context, rig.environment.writes, writes + 1);
        rig.StepAt(0x1010, &cache);
        REX86_CHECK_EQ(context, rig.state.Get(Gpr::kEax), 0u);
        // 0x1010 was decoded again, setting kTranslated again; the stale
        // inc ecx must still not be trusted.
        rig.StepAt(0x1008, &cache);  // inc ecx -> dec ecx
        rig.StepAt(0x1010, &cache);
        rig.StepAt(0x1020, &cache);
        REX86_CHECK_EQ(context, rig.state.Get(Gpr::kEcx), 0u);
    }
    // Two Cpus over one memory: B's store is seen by A's cache even after
    // A re-decodes another instruction on the page.
    {
        Rig rig;
        DecodeCache a;
        DecodeCache b;
        rex86::CpuState other = rig.state;
        rig.Put(0x1000, {0xC6, 0x05, 0x20, 0x10, 0x00, 0x00, 0x49});  // mov byte [0x1020], 0x49
        rig.Put(0x1010, {0x40});
        rig.Put(0x1020, {0x41});
        rig.StepAt(0x1010, &a);
        rig.StepAt(0x1020, &a);
        rig.StepAt(0x1000, &b, &other);  // B rewrites inc ecx
        rig.StepAt(0x1010, &a);          // A re-decodes on the page
        rig.StepAt(0x1020, &a);
        REX86_CHECK_EQ(context, rig.state.Get(Gpr::kEcx), 0u);
    }
    // The same linear address under another CS base is another key: a
    // relative branch's target depends on EIP.
    {
        Rig rig;
        DecodeCache cache;
        rig.Put(0x1010, {0xEB, 0x02});  // jmp $+4
        rig.StepAt(0x1010, &cache);
        REX86_CHECK_EQ(context, rig.state.eip, 0x1014u);
        rig.state.Seg(rex86::Segment::kCs).base = 0x10;
        rig.StepAt(0x1000, &cache);
        REX86_CHECK_EQ(context, rig.state.eip, 0x1004u);
    }
    // A cached instruction that no longer fits the CS limit faults as the
    // byte fetch would; an executable page made non-executable faults too.
    {
        Rig rig;
        DecodeCache cache;
        rig.Put(0x1010, {0xB8, 1, 2, 3, 4});  // mov eax, imm32
        rig.StepAt(0x1010, &cache);
        rig.state.Seg(rex86::Segment::kCs).limit = 0x1012;
        const auto limited = rig.StepAt(0x1010, &cache);
        REX86_CHECK(context, limited.status == StepStatus::kFaulted);
        REX86_CHECK(context, limited.event.fault_kind == FaultKind::kGeneralProtection);
        rig.state.Seg(rex86::Segment::kCs).limit = 0xFFFFFFFFu;
        rig.memory.pages().Set(0x1000, 1, rex86::kPageReadWrite);
        const auto unexec = rig.StepAt(0x1010, &cache);
        REX86_CHECK(context, unexec.status == StepStatus::kFaulted);
        REX86_CHECK(context, unexec.event.fault_kind == FaultKind::kAccessViolation);
    }
    // Through Cpu: after the warm-up the cache is on, and the host's
    // InvalidateCode makes a code change visible.
    {
        std::vector<std::uint8_t> buffer(0x10000, 0);
        rex86::GuestMemory memory(buffer.data(), 0x10000);
        memory.pages().Set(0, 0x10000, rex86::kPageReadWriteExecute);
        NullEnvironment environment;
        rex86::Cpu cpu(&memory, &environment, rex86::Features{});
        // loop: inc eax; dec ecx; jnz loop; hlt
        const std::uint8_t code[] = {0x40, 0x49, 0x75, 0xFC, 0xF4};
        for (unsigned i = 0; i < sizeof code; ++i) buffer[0x1000 + i] = code[i];
        cpu.state().eip = 0x1000;
        cpu.state().Set(Gpr::kEcx, 100);
        cpu.state().Set(Gpr::kEsp, 0xF000);
        cpu.Run(1000);
        REX86_CHECK_EQ(context, cpu.state().Get(Gpr::kEax), 100u);
        buffer[0x1000] = 0x48;  // dec eax
        cpu.InvalidateCode(0x1000, 1);
        cpu.state().eip = 0x1000;
        cpu.state().Set(Gpr::kEcx, 10);
        cpu.Run(1000);
        REX86_CHECK_EQ(context, cpu.state().Get(Gpr::kEax), 90u);
        rex86::Cpu moved = std::move(cpu);
        REX86_CHECK_EQ(context, moved.state().Get(Gpr::kEax), 90u);
    }
}
