#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

#include "rex86/cpu.h"
#include "test_support.h"

namespace
{

using rex86::Gpr;
using rex86::StopReason;

class QuietEnvironment final : public rex86::Environment
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
};

constexpr std::uint32_t kCode = 0x1000;
constexpr std::uint32_t kReadOnly = 0xC000;

// One guest: code and data read-write-execute, one read-only page, and a
// Cpu over it.
struct Rig
{
    std::vector<std::uint8_t> buffer = std::vector<std::uint8_t>(0x10000, 0xF4);  // hlt
    rex86::GuestMemory memory{buffer.data(), 0x10000};
    QuietEnvironment environment;
    rex86::Cpu cpu{&memory, &environment, rex86::Features{}};

    Rig(std::initializer_list<std::uint8_t> code, const rex86::TranslationOptions& options)
    {
        memory.pages().Set(0, 0x10000, rex86::kPageReadWriteExecute);
        memory.pages().Set(kReadOnly, 0x1000, rex86::PageFlag::kMapped | rex86::PageFlag::kRead);
        std::uint32_t at = kCode;
        for (const std::uint8_t b : code)
        {
            buffer[at++] = b;
        }
        cpu.state().eip = kCode;
        cpu.state().Set(Gpr::kEsp, 0x9000);
        cpu.state().Seg(rex86::Segment::kCs).executable = true;
        cpu.SetTranslation(options);
    }
};

rex86::TranslationOptions Interpreter()
{
    return rex86::TranslationOptions{};
}

rex86::TranslationOptions Evaluator(const std::uint32_t threshold)
{
    rex86::TranslationOptions options;
    options.mode = rex86::TranslationMode::kEvaluator;
    options.threshold = threshold;
    return options;
}

bool SameMachine(const Rig& a, const Rig& b)
{
    return a.cpu.state().gpr == b.cpu.state().gpr && a.cpu.state().eip == b.cpu.state().eip &&
           a.cpu.state().eflags == b.cpu.state().eflags && a.buffer == b.buffer;
}

bool SameEvent(const rex86::Event& a, const rex86::Event& b)
{
    return a.reason == b.reason && a.steps == b.steps && a.fault_kind == b.fault_kind &&
           a.fault_address == b.fault_address && a.gate_address == b.gate_address;
}

// mov ecx, 1000; L: add eax, ecx; mov [0x8000 + ecx*4], eax; dec ecx;
// jnz L; hlt
const std::initializer_list<std::uint8_t> kLoop = {
    0xB9, 0xE8, 0x03, 0x00, 0x00,              // mov ecx, 1000
    0x01, 0xC8,                                // L: add eax, ecx
    0x89, 0x04, 0x8D, 0x00, 0x80, 0x00, 0x00,  // mov [ecx*4 + 0x8000], eax
    0x49,                                      // dec ecx
    0x75, 0xF4,                                // jnz L
    0xF4};                                     // hlt

void LoopTests(rex86::test::Context& context)
{
    for (const std::uint32_t threshold : {0u, 4u, 32u})
    {
        Rig interpreted(kLoop, Interpreter());
        Rig translated(kLoop, Evaluator(threshold));
        const rex86::Event a = interpreted.cpu.Run(1000000);
        const rex86::Event b = translated.cpu.Run(1000000);
        REX86_CHECK(context, a.reason == StopReason::kHalted);
        REX86_CHECK(context, SameEvent(a, b));
        REX86_CHECK(context, SameMachine(interpreted, translated));
        REX86_CHECK(context, translated.cpu.ActiveEngine() == rex86::Engine::kTranslator);
        const rex86::TranslationStats stats = translated.cpu.translation_stats();
        REX86_CHECK(context, stats.blocks_translated >= 1);
        REX86_CHECK(context, stats.translated_steps > 3000);
    }
}

// A budget smaller than a block runs the interpreter; Run(1) again and
// again matches the interpreter at every step.
void BudgetTests(rex86::test::Context& context)
{
    for (const std::uint64_t budget : {1u, 2u, 3u, 7u})
    {
        Rig interpreted(kLoop, Interpreter());
        Rig translated(kLoop, Evaluator(0));
        bool same = true;
        for (int i = 0; i < 400; ++i)
        {
            const rex86::Event a = interpreted.cpu.Run(budget);
            const rex86::Event b = translated.cpu.Run(budget);
            same = same && SameEvent(a, b) && SameMachine(interpreted, translated);
        }
        REX86_CHECK(context, same);
    }
}

// With one instruction per block, single steps run translated and still
// match the interpreter step by step (design #46).
void SingleInstructionBlockTests(rex86::test::Context& context)
{
    rex86::TranslationOptions options = Evaluator(0);
    options.max_block_instructions = 1;
    Rig interpreted(kLoop, Interpreter());
    Rig translated(kLoop, options);
    bool same = true;
    for (int i = 0; i < 300; ++i)
    {
        const rex86::Event a = interpreted.cpu.Step();
        const rex86::Event b = translated.cpu.Step();
        same = same && SameEvent(a, b) && SameMachine(interpreted, translated);
    }
    REX86_CHECK(context, same);
    REX86_CHECK(context, translated.cpu.translation_stats().translated_steps > 250);
    // Blocks longer than the budget never run, so at the default length
    // single steps run translated only where a block is one instruction
    // long (the jnz, reached by stepping).
    Rig longer(kLoop, Evaluator(0));
    for (int i = 0; i < 300; ++i)
    {
        longer.cpu.Step();
    }
    REX86_CHECK(context, longer.cpu.translation_stats().translated_steps * 2 <
                             translated.cpu.translation_stats().translated_steps);
}

// A store the check refuses: the interpreter runs the instruction and
// raises the same fault, after the same steps.
void FaultTests(rex86::test::Context& context)
{
    // mov eax, 0xC000; inc ebx; mov [eax], ebx; hlt
    const std::initializer_list<std::uint8_t> code = {0xB8, 0x00, 0xC0, 0x00, 0x00, 0x43,
                                                      0x89, 0x18, 0xF4};
    Rig interpreted(code, Interpreter());
    Rig translated(code, Evaluator(0));
    const rex86::Event a = interpreted.cpu.Run(100);
    const rex86::Event b = translated.cpu.Run(100);
    REX86_CHECK(context, a.reason == StopReason::kFault);
    REX86_CHECK(context, SameEvent(a, b));
    REX86_CHECK(context, SameMachine(interpreted, translated));
    REX86_CHECK(context, translated.cpu.translation_stats().interpreter_exits >= 1);
}

// A gate registered inside an already translated loop stops execution
// before its instruction, as on the interpreter.
void GateTests(rex86::test::Context& context)
{
    Rig interpreted(kLoop, Interpreter());
    Rig translated(kLoop, Evaluator(0));
    interpreted.cpu.Run(100);
    translated.cpu.Run(100);
    const std::uint32_t gate = kCode + 7;  // the mov inside the loop
    interpreted.cpu.RegisterGate(gate);
    translated.cpu.RegisterGate(gate);
    const rex86::Event a = interpreted.cpu.Run(1000000);
    const rex86::Event b = translated.cpu.Run(1000000);
    REX86_CHECK(context, a.reason == StopReason::kGate);
    REX86_CHECK(context, SameEvent(a, b));
    REX86_CHECK(context, SameMachine(interpreted, translated));
}

// Code that rewrites itself inside a translated block, and a host that
// rewrites code and calls InvalidateCode.
void SelfModifyingTests(rex86::test::Context& context)
{
    // mov ecx, 50; L: mov byte [P+1], cl; P: add eax, 0 (83 C0 imm8);
    // dec ecx; jnz L; hlt  -- the add's immediate is the loop counter.
    const std::uint32_t patch = kCode + 5 + 6 + 2;  // P + 2, the imm8
    const std::initializer_list<std::uint8_t> code = {
        0xB9, 0x32, 0x00, 0x00, 0x00,                                    // mov ecx, 50
        0x88, 0x0D, static_cast<std::uint8_t>(patch), static_cast<std::uint8_t>(patch >> 8), 0x00, 0x00,
        0x83, 0xC0, 0x00,                                                // P: add eax, 0
        0x49,                                                            // dec ecx
        0x75, 0xF4,                                                      // jnz L
        0xF4};
    Rig interpreted(code, Interpreter());
    Rig translated(code, Evaluator(0));
    const rex86::Event a = interpreted.cpu.Run(100000);
    const rex86::Event b = translated.cpu.Run(100000);
    REX86_CHECK(context, a.reason == StopReason::kHalted);
    REX86_CHECK(context, SameEvent(a, b));
    REX86_CHECK(context, SameMachine(interpreted, translated));
    REX86_CHECK_EQ(context, translated.cpu.state().Get(Gpr::kEax), std::uint32_t{50 * 51 / 2});

    // The host patches the loop body and says so.
    Rig host(kLoop, Evaluator(0));
    host.cpu.Run(1000000);
    host.buffer[kCode + 5] = 0x29;  // add eax, ecx -> sub eax, ecx
    host.cpu.InvalidateCode(kCode, 0x100);
    host.cpu.state().eip = kCode;
    host.cpu.state().Set(Gpr::kEax, 0);
    host.cpu.Run(1000000);
    Rig reference(kLoop, Interpreter());
    reference.buffer[kCode + 5] = 0x29;
    reference.cpu.Run(1000000);
    REX86_CHECK_EQ(context, host.cpu.state().Get(Gpr::kEax), reference.cpu.state().Get(Gpr::kEax));
}

// A stop request is seen before the next block.
void StopTests(rex86::test::Context& context)
{
    Rig translated(kLoop, Evaluator(0));
    translated.cpu.Run(50);
    translated.cpu.RequestStop();
    const rex86::Event event = translated.cpu.Run(1000000);
    REX86_CHECK(context, event.reason == StopReason::kStopRequested);
    REX86_CHECK_EQ(context, event.steps, std::uint64_t{0});
    // Turning translation off returns to the interpreter.
    translated.cpu.SetTranslation(Interpreter());
    REX86_CHECK(context, translated.cpu.ActiveEngine() == rex86::Engine::kInterpreter);
    REX86_CHECK(context, translated.cpu.Run(1000000).reason == StopReason::kHalted);
}

}  // namespace

void RunTranslateRuntimeTests(rex86::test::Context& context)
{
    LoopTests(context);
    BudgetTests(context);
    SingleInstructionBlockTests(context);
    FaultTests(context);
    GateTests(context);
    SelfModifyingTests(context);
    StopTests(context);
}
