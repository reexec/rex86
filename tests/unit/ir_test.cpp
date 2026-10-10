#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "interp/interpreter.h"
#include "rex86/cpu_state.h"
#include "rex86/guest_memory.h"
#include "test_support.h"
#include "tools/irdiff/irdiff.h"
#include "translate/ir/evaluator.h"
#include "translate/ir/frontend.h"
#include "translate/ir/optimize.h"

namespace
{

using rex86::Gpr;
using rex86::PageFlag;
namespace ir = rex86::translate::ir;

constexpr std::uint32_t kCode = 0x1000;

// Two code pages, then data: read-write, read-only, translated.
struct Guest
{
    std::vector<std::uint8_t> bytes = std::vector<std::uint8_t>(0x8000, 0);
    rex86::GuestMemory memory{bytes.data(), 0x8000};
    rex86::CpuState state;

    Guest()
    {
        memory.pages().Set(kCode, 0x2000, rex86::kPageReadExecute);
        memory.pages().Set(0x4000, 0x1000, rex86::kPageReadWrite);
        memory.pages().Set(0x5000, 0x1000, PageFlag::kMapped | PageFlag::kRead);
        memory.pages().Set(0x6000, 0x1000, rex86::kPageReadWrite);
        memory.pages().Add(0x6000, 0x1000, PageFlag::kTranslated);
        state.eip = kCode;
        state.Set(Gpr::kEsp, 0x4800);
        state.Seg(rex86::Segment::kCs).executable = true;
        state.Seg(rex86::Segment::kCs).writable = false;
    }

    void Code(std::uint32_t at, std::initializer_list<std::uint8_t> code)
    {
        for (const std::uint8_t b : code)
        {
            bytes[at++] = b;
        }
    }
};

std::size_t CountOps(const ir::Block& block, const ir::Op op)
{
    std::size_t n = 0;
    for (const ir::Inst& inst : block.insts)
    {
        n += inst.op == op ? 1 : 0;
    }
    return n;
}

void Report(rex86::test::Context& context, const char* name, const rex86::irdiff::Stats& stats)
{
    std::printf("[rex86-unit-tests] %s cases=%llu compared=%llu exited=%llu mismatches=%llu\n", name,
                static_cast<unsigned long long>(stats.cases),
                static_cast<unsigned long long>(stats.compared),
                static_cast<unsigned long long>(stats.exited),
                static_cast<unsigned long long>(stats.mismatches));
    if (stats.mismatches != 0)
    {
        context.Fail(std::string(name) + " differential: " + stats.first_failure, __FILE__, __LINE__);
    }
}

void DifferentialTests(rex86::test::Context& context)
{
    rex86::irdiff::Options options;
    options.seed = 43;
    options.cases = 10000;
    const rex86::irdiff::Stats forms = rex86::irdiff::RunForms(options);
    Report(context, "ir forms", forms);
    REX86_CHECK_EQ(context, forms.mismatches, std::uint64_t{0});

    // Every mnemonic of the first coverage is compared at least once.
    static const char* const kMnemonics[] = {
        "mov", "movzx", "movsx", "lea", "xchg", "add", "adc", "sub", "sbb", "cmp", "and",
        "or", "xor", "test", "inc", "dec", "neg", "not", "shl", "shr", "sar", "rol", "ror",
        "imul", "push", "pop", "jmp", "call", "ret", "jz", "jnz", "jb", "jle", "setz",
        "setnbe", "cmovz", "cmovl"};
    for (const char* mnemonic : kMnemonics)
    {
        const auto found = forms.compared_by_mnemonic.find(mnemonic);
        const bool compared = found != forms.compared_by_mnemonic.end() && found->second > 0;
        if (!compared)
        {
            context.Fail(std::string("no compared case for ") + mnemonic, __FILE__, __LINE__);
        }
        else
        {
            context.Check(true, mnemonic, __FILE__, __LINE__);
        }
    }

    options.seed = 4300;
    options.cases = 2000;
    const rex86::irdiff::Stats blocks = rex86::irdiff::RunBlocks(options);
    Report(context, "ir blocks", blocks);
    REX86_CHECK_EQ(context, blocks.mismatches, std::uint64_t{0});
    REX86_CHECK(context, blocks.compared > blocks.cases / 2);
}

// A store that the interpreter would fault on, or that would land in
// translated code, leaves the instruction to the interpreter untouched.
void CheckExitTests(rex86::test::Context& context)
{
    for (const std::uint32_t target : {0x5010u, 0x6010u, 0x7FFEu})
    {
        Guest g;
        g.Code(kCode, {0x89, 0x18});  // mov [eax], ebx
        g.state.Set(Gpr::kEax, target);
        g.state.Set(Gpr::kEbx, 0x11223344u);
        ir::Block block;
        REX86_CHECK(context, ir::FormBlock(g.state, g.memory, rex86::Features{}, {}, &block));
        const std::vector<std::uint8_t> before = g.bytes;
        const ir::ExitResult exit = ir::Evaluate(block, g.state, g.memory);
        REX86_CHECK(context, exit.kind == ir::ExitKind::kInterpret);
        REX86_CHECK_EQ(context, exit.steps, std::uint32_t{0});
        REX86_CHECK_EQ(context, exit.eip, kCode);
        REX86_CHECK(context, g.bytes == before);
        REX86_CHECK(context, rex86::Has(g.memory.pages().Get(0x6000), PageFlag::kTranslated));
    }
}

void BlockFormationTests(rex86::test::Context& context)
{
    // A gate on the second instruction ends the block before it.
    {
        Guest g;
        g.Code(kCode, {0x83, 0xC0, 0x01, 0x83, 0xC0, 0x01});  // add eax, 1 twice
        std::vector<std::uint64_t> words(rex86::interp::GateFilter::kWords, 0);
        const std::uint32_t bit = rex86::interp::GateFilter::Bit(kCode + 3);
        words[bit >> 6] |= std::uint64_t{1} << (bit & 63u);
        const rex86::interp::GateFilter filter{words.data()};
        ir::FrontendOptions options;
        options.gates = &filter;
        ir::Block block;
        REX86_CHECK(context, ir::FormBlock(g.state, g.memory, rex86::Features{}, options, &block));
        REX86_CHECK_EQ(context, block.instruction_count, std::uint32_t{1});
        const ir::ExitResult exit = ir::Evaluate(block, g.state, g.memory);
        REX86_CHECK(context, exit.kind == ir::ExitKind::kContinue);
        REX86_CHECK_EQ(context, exit.eip, kCode + 3);
        REX86_CHECK_EQ(context, g.state.Get(Gpr::kEax), std::uint32_t{1});
    }
    // The next instruction cannot be fetched: the block stops before it and
    // leaves it to the interpreter, which raises the fault.
    {
        Guest g;
        const std::uint32_t at = 0x2FFD;
        g.Code(at, {0x83, 0xC0, 0x01});  // add eax, 1, then the unmapped 0x3000
        g.state.eip = at;
        ir::Block block;
        REX86_CHECK(context, ir::FormBlock(g.state, g.memory, rex86::Features{}, {}, &block));
        REX86_CHECK_EQ(context, block.instruction_count, std::uint32_t{1});
        const ir::ExitResult exit = ir::Evaluate(block, g.state, g.memory);
        REX86_CHECK(context, exit.kind == ir::ExitKind::kInterpret);
        REX86_CHECK_EQ(context, exit.eip, std::uint32_t{0x3000});
        REX86_CHECK_EQ(context, exit.steps, std::uint32_t{1});
    }
    // A block spans at most two pages.
    {
        Guest g;
        g.memory.pages().Set(0x3000, 0x1000, rex86::kPageReadExecute);
        for (std::uint32_t at = 0x1FF0; at < 0x3010; ++at)
        {
            g.bytes[at] = 0x40;  // inc eax
        }
        g.state.eip = 0x1FF0;
        ir::FrontendOptions options;
        options.max_instructions = 0x2000;
        ir::Block block;
        REX86_CHECK(context, ir::FormBlock(g.state, g.memory, rex86::Features{}, options, &block));
        REX86_CHECK_EQ(context, block.page_count, std::uint32_t{2});
        REX86_CHECK_EQ(context, block.instruction_count, std::uint32_t{0x1010});
    }
    // No block starts under a non-flat CS, or at an uncovered instruction.
    {
        Guest g;
        g.Code(kCode, {0x90});
        g.state.Seg(rex86::Segment::kCs).limit = 0x7FFF;
        ir::Block block;
        REX86_CHECK(context, !ir::FormBlock(g.state, g.memory, rex86::Features{}, {}, &block));
        Guest h;
        h.Code(kCode, {0xF4});  // hlt
        REX86_CHECK(context, !ir::FormBlock(h.state, h.memory, rex86::Features{}, {}, &block));
    }
    // Disabled CMOV stays with the interpreter (#UD).
    {
        Guest g;
        g.Code(kCode, {0x0F, 0x44, 0xC1});  // cmovz eax, ecx
        rex86::Features features;
        features.cmov = false;
        ir::Block block;
        REX86_CHECK(context, !ir::FormBlock(g.state, g.memory, features, {}, &block));
    }
}

// POP m through ESP addresses the destination after the increment, PUSH m
// before the decrement (SDM POP, PUSH), as the interpreter does.
void StackAddressTests(rex86::test::Context& context)
{
    for (const bool pop : {true, false})
    {
        Guest g;
        if (pop)
        {
            g.Code(kCode, {0x8F, 0x44, 0x24, 0x04, 0xF4});  // pop dword [esp+4]; hlt
        }
        else
        {
            g.Code(kCode, {0xFF, 0x74, 0x24, 0x04, 0xF4});  // push dword [esp+4]; hlt
        }
        for (std::uint32_t i = 0; i < 16; ++i)
        {
            g.bytes[0x47F0 + i] = static_cast<std::uint8_t>(0xA0 + i);
        }
        ir::Block block;
        REX86_CHECK(context, ir::FormBlock(g.state, g.memory, rex86::Features{}, {}, &block));
        std::vector<std::uint8_t> interpreted_bytes = g.bytes;
        rex86::GuestMemory interpreted_memory{interpreted_bytes.data(), 0x8000};
        interpreted_memory.pages() = g.memory.pages();
        rex86::CpuState interpreted = g.state;
        class : public rex86::Environment
        {
        public:
            bool LoadDescriptor(std::uint16_t, rex86::Descriptor* d) override { *d = {}; return true; }
            bool PortRead(std::uint16_t, std::uint8_t, std::uint32_t*) override { return false; }
            bool PortWrite(std::uint16_t, std::uint8_t, std::uint32_t) override { return false; }
            bool InterruptTarget(std::uint8_t, std::uint16_t*, std::uint32_t*) override { return false; }
            std::uint64_t ReadTimeStampCounter() override { return 0; }
            void Cpuid(std::uint32_t, std::uint32_t, std::uint32_t r[4]) override { r[0] = r[1] = r[2] = r[3] = 0; }
        } environment;
        rex86::interp::Step(interpreted, interpreted_memory, environment, rex86::Features{});
        const ir::ExitResult exit = ir::Evaluate(block, g.state, g.memory);
        REX86_CHECK_EQ(context, exit.steps, std::uint32_t{1});
        REX86_CHECK(context, g.state.gpr == interpreted.gpr && g.state.eip == interpreted.eip);
        REX86_CHECK(context, g.bytes == interpreted_bytes);
    }
}

// Flag writes overwritten by the next instruction disappear, and the ones
// before a possible exit stay.
void OptimizeTests(rex86::test::Context& context)
{
    Guest g;
    // add, add, mov [eax], ebx, then hlt, which ends the block.
    g.Code(kCode, {0x83, 0xC0, 0x01, 0x83, 0xC0, 0x01, 0x89, 0x18, 0xF4});
    g.state.Set(Gpr::kEax, 0x4000);
    ir::Block block;
    REX86_CHECK(context, ir::FormBlock(g.state, g.memory, rex86::Features{}, {}, &block));
    ir::Block optimized = block;
    ir::Optimize(&optimized);
    REX86_CHECK_EQ(context, CountOps(block, ir::Op::kSetFlag), std::size_t{12});
    REX86_CHECK_EQ(context, CountOps(optimized, ir::Op::kSetFlag), std::size_t{6});
    REX86_CHECK(context, optimized.insts.size() < block.insts.size());
    rex86::CpuState a = g.state;
    rex86::CpuState b = g.state;
    std::vector<std::uint8_t> a_bytes = g.bytes;
    std::vector<std::uint8_t> b_bytes = g.bytes;
    rex86::GuestMemory a_memory{a_bytes.data(), 0x8000};
    rex86::GuestMemory b_memory{b_bytes.data(), 0x8000};
    a_memory.pages() = g.memory.pages();
    b_memory.pages() = g.memory.pages();
    ir::Evaluate(block, a, a_memory);
    ir::Evaluate(optimized, b, b_memory);
    REX86_CHECK(context, a.gpr == b.gpr && a.eflags == b.eflags && a.eip == b.eip);
    REX86_CHECK(context, a_bytes == b_bytes);
}

}  // namespace

void RunIrTests(rex86::test::Context& context)
{
    DifferentialTests(context);
    CheckExitTests(context);
    BlockFormationTests(context);
    StackAddressTests(context);
    OptimizeTests(context);
}
