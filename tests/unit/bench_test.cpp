#include <cstdint>
#include <string>
#include <vector>

#include "decode/decoder.h"
#include "test_support.h"
#include "tools/bench/asm.h"
#include "tools/bench/runner.h"
#include "tools/bench/workloads.h"

namespace
{

using rex86::bench::Alu;
using rex86::bench::Assembler;
using rex86::bench::BranchWidth;
using rex86::bench::Cond;
using rex86::bench::Image;
using rex86::bench::Label;
using rex86::bench::Machine;
using rex86::bench::Mem;
using rex86::bench::Reg;
using rex86::bench::Shift;
using rex86::bench::Workload;
using rex86::decode::DecodedInstruction;
using rex86::decode::Decoder;

// Decodes the single instruction an emitter produced and checks its
// mnemonic, operand signature and that it consumed every byte.
void CheckEncoding(rex86::test::Context& context, const Decoder& decoder,
                   const Assembler& a, const char* mnemonic, const char* signature)
{
    DecodedInstruction decoded;
    const std::vector<std::uint8_t>& bytes = a.bytes();
    const bool ok = decoder.Decode(bytes.data(), bytes.size(), a.base(), &decoded);
    REX86_CHECK(context, ok);
    if (!ok)
    {
        context.Fail(std::string("undecodable ") + mnemonic, __FILE__, __LINE__);
        return;
    }
    REX86_CHECK_EQ(context, std::string(decoded.MnemonicName()), std::string(mnemonic));
    REX86_CHECK_EQ(context, decoded.OperandSignature(), std::string(signature));
    REX86_CHECK_EQ(context, decoded.Length(), static_cast<std::uint32_t>(bytes.size()));
}

template <typename Emit>
void CheckOne(rex86::test::Context& context, const Decoder& decoder, Emit emit,
              const char* mnemonic, const char* signature)
{
    Assembler a(0x10000);
    emit(&a);
    std::string error;
    REX86_CHECK(context, a.Finish(&error));
    CheckEncoding(context, decoder, a, mnemonic, signature);
}

void AssemblerTests(rex86::test::Context& context)
{
    const Decoder decoder;
    CheckOne(context, decoder, [](Assembler* a) { a->MovImm(Reg::kEdi, 0x12345678); },
             "mov", "r32,i32");
    CheckOne(context, decoder, [](Assembler* a) { a->MovRR(Reg::kEbx, Reg::kEax); },
             "mov", "r32,r32");
    CheckOne(context, decoder,
             [](Assembler* a) { a->MovLoad(Reg::kEbx, Mem::BaseIndex(Reg::kEsi, Reg::kEcx, 4, 0)); },
             "mov", "r32,m32");
    CheckOne(context, decoder,
             [](Assembler* a) { a->MovStore(Mem::BaseDisp(Reg::kEbp, 8), Reg::kEax); },
             "mov", "m32,r32");
    CheckOne(context, decoder,
             [](Assembler* a) { a->MovStore(Mem::BaseDisp(Reg::kEsp, 0), Reg::kEax); },
             "mov", "m32,r32");
    CheckOne(context, decoder,
             [](Assembler* a) { a->MovStore(Mem::BaseDisp(Reg::kEbp, 0), Reg::kEax); },
             "mov", "m32,r32");
    CheckOne(context, decoder,
             [](Assembler* a) { a->MovStore(Mem::BaseDisp(Reg::kEsi, 0x1000), Reg::kEax); },
             "mov", "m32,r32");
    CheckOne(context, decoder, [](Assembler* a) { a->MovStore(Mem::Abs(0x00100000), Reg::kEax); },
             "mov", "m32,r32");
    CheckOne(context, decoder,
             [](Assembler* a) { a->MovStore8(Mem::IndexDisp(Reg::kEcx, 1, 0x00120000), Reg::kEax); },
             "mov", "m8,r8");
    CheckOne(context, decoder,
             [](Assembler* a) { a->MovzxLoad8(Reg::kEdx, Mem::IndexDisp(Reg::kEdx, 1, 0x00120000)); },
             "movzx", "r32,m8");
    CheckOne(context, decoder,
             [](Assembler* a) { a->Lea(Reg::kEbx, Mem::BaseIndex(Reg::kEax, Reg::kEax, 2, 1)); },
             "lea", "r32,m32");
    CheckOne(context, decoder, [](Assembler* a) { a->Push(Reg::kEbp); }, "push", "r32");
    CheckOne(context, decoder, [](Assembler* a) { a->Pop(Reg::kEbx); }, "pop", "r32");
    CheckOne(context, decoder, [](Assembler* a) { a->PushImm(0x1234); }, "push", "i32");
    CheckOne(context, decoder, [](Assembler* a) { a->AluRR(Alu::kXor, Reg::kEax, Reg::kEcx); },
             "xor", "r32,r32");
    CheckOne(context, decoder, [](Assembler* a) { a->AluRR(Alu::kAdd, Reg::kEdx, Reg::kEax); },
             "add", "r32,r32");
    CheckOne(context, decoder, [](Assembler* a) { a->AluRI(Alu::kCmp, Reg::kEcx, 256); },
             "cmp", "r32,i32");
    CheckOne(context, decoder, [](Assembler* a) { a->AluRI(Alu::kCmp, Reg::kEcx, 64); },
             "cmp", "r32,i8");
    CheckOne(context, decoder, [](Assembler* a) { a->AluRI(Alu::kAnd, Reg::kEdx, 0xFF); },
             "and", "r32,i32");
    CheckOne(context, decoder,
             [](Assembler* a) { a->AluRM(Alu::kAdd, Reg::kEdx, Mem::Abs(0x00140000)); },
             "add", "r32,m32");
    CheckOne(context, decoder,
             [](Assembler* a) { a->AluMR(Alu::kXor, Mem::Abs(0x00100000), Reg::kEax); },
             "xor", "m32,r32");
    CheckOne(context, decoder,
             [](Assembler* a) { a->ImulRRI(Reg::kEax, Reg::kEax, 0x01000193); },
             "imul", "r32,r32,i32");
    CheckOne(context, decoder, [](Assembler* a) { a->ImulRRI(Reg::kEax, Reg::kEcx, 0x2D); },
             "imul", "r32,r32,i8");
    CheckOne(context, decoder, [](Assembler* a) { a->ImulRR(Reg::kEax, Reg::kEbx); },
             "imul", "r32,r32");
    CheckOne(context, decoder, [](Assembler* a) { a->ShiftRI(Shift::kShr, Reg::kEbx, 13); },
             "shr", "r32,i8");
    CheckOne(context, decoder, [](Assembler* a) { a->ShiftRI(Shift::kRol, Reg::kEdx, 5); },
             "rol", "r32,i8");
    CheckOne(context, decoder, [](Assembler* a) { a->Inc(Reg::kEcx); }, "inc", "r32");
    CheckOne(context, decoder, [](Assembler* a) { a->Dec(Reg::kEbx); }, "dec", "r32");
    CheckOne(context, decoder, [](Assembler* a) { a->TestRR(Reg::kEax, Reg::kEax); },
             "test", "r32,r32");
    CheckOne(context, decoder, [](Assembler* a) { a->Ret(); }, "ret", "-");
    CheckOne(context, decoder, [](Assembler* a) { a->RetImm(4); }, "ret", "i16");
    CheckOne(context, decoder, [](Assembler* a) { a->Nop(); }, "nop", "-");
    CheckOne(context, decoder, [](Assembler* a) { a->Cld(); }, "cld", "-");
    CheckOne(context, decoder, [](Assembler* a) { a->RepMovsd(); }, "movsd", "-");
    CheckOne(context, decoder, [](Assembler* a) { a->RepStosd(); }, "stosd", "-");
    CheckOne(context, decoder,
             [](Assembler* a) { a->FldM64(Mem::IndexDisp(Reg::kEcx, 8, 0x00150000)); },
             "fld", "m64");
    CheckOne(context, decoder, [](Assembler* a) { a->FstpM64(Mem::Abs(0x00160010)); },
             "fstp", "m64");
    CheckOne(context, decoder, [](Assembler* a) { a->FaddM64(Mem::Abs(0x00160008)); },
             "fadd", "m64");
    CheckOne(context, decoder, [](Assembler* a) { a->FmulM64(Mem::Abs(0x00160000)); },
             "fmul", "m64");
    CheckOne(context, decoder, [](Assembler* a) { a->FaddpSt1St0(); }, "faddp", "r80");
    CheckOne(context, decoder, [](Assembler* a) { a->FxchSt1(); }, "fxch", "r80");
    CheckOne(context, decoder, [](Assembler* a) { a->FistpM32(Mem::Abs(0x00160020)); },
             "fistp", "m32");
    CheckOne(context, decoder, [](Assembler* a) { a->Fldz(); }, "fldz", "-");
    CheckOne(context, decoder, [](Assembler* a) { a->Fld1(); }, "fld1", "-");

    // Branches: a short backward Jcc, a near forward CALL and JMP, and the
    // decoder's view of their targets.
    {
        Assembler a(0x10000);
        const Label loop = a.NewLabel();
        const Label f = a.NewLabel();
        a.Bind(loop);
        a.Dec(Reg::kEcx);
        a.Jcc(Cond::kNz, loop, BranchWidth::kShort);
        a.Call(f);
        a.Jmp(f);
        a.Bind(f);
        a.Ret();
        std::string error;
        REX86_CHECK(context, a.Finish(&error));
        REX86_CHECK_EQ(context, a.AddressOf(f), 0x1000Du);
        const std::vector<std::uint8_t>& bytes = a.bytes();
        // dec ecx (1), jnz -3 (2), call rel32 (5), jmp rel32 (5), ret (1).
        REX86_CHECK_EQ(context, bytes.size(), std::size_t{14});
        REX86_CHECK_EQ(context, bytes[1], std::uint8_t{0x75});
        REX86_CHECK_EQ(context, bytes[2], std::uint8_t{0xFD});
        DecodedInstruction decoded;
        std::uint32_t target = 0;
        REX86_CHECK(context, decoder.Decode(bytes.data() + 3, 5, 0x10003, &decoded));
        REX86_CHECK(context, decoded.DirectTarget(&target));
        REX86_CHECK_EQ(context, target, 0x1000Du);
        REX86_CHECK(context, decoder.Decode(bytes.data() + 8, 5, 0x10008, &decoded));
        REX86_CHECK(context, decoded.DirectTarget(&target));
        REX86_CHECK_EQ(context, target, 0x1000Du);
    }
    // A short branch that does not reach fails Finish; so does an unbound
    // label.
    {
        Assembler a(0x10000);
        const Label far = a.NewLabel();
        a.Jmp(far, BranchWidth::kShort);
        for (int i = 0; i < 200; ++i)
        {
            a.Nop();
        }
        a.Bind(far);
        std::string error;
        REX86_CHECK(context, !a.Finish(&error));
        REX86_CHECK(context, !error.empty());
    }
    {
        Assembler a(0x10000);
        const Label unbound = a.NewLabel();
        a.Call(unbound);
        std::string error;
        REX86_CHECK(context, !a.Finish(&error));
    }
}

void WorkloadTests(rex86::test::Context& context)
{
    Image image;
    std::string error;
    REX86_CHECK(context, rex86::bench::BuildImage(&image, &error));
    REX86_CHECK_EQ(context, image.workloads.size(), std::size_t{6});
    REX86_CHECK(context, image.code.size() <= rex86::bench::kCodeSize);

    // The reference models are what the design states.
    REX86_CHECK_EQ(context, rex86::bench::KernelModel(rex86::bench::Kernel::kX87),
                   1024u * 1025u / 2u);
    const Workload* mixed = rex86::bench::FindWorkload(image, "mixed");
    REX86_CHECK(context, mixed != nullptr);
    if (mixed != nullptr)
    {
        std::uint32_t expected = 0;
        for (const Workload& workload : image.workloads)
        {
            if (workload.name != "mixed")
            {
                expected ^= workload.expected_edi;
            }
        }
        REX86_CHECK_EQ(context, mixed->expected_edi, expected);
    }
    REX86_CHECK(context, rex86::bench::FindWorkload(image, "none") == nullptr);

    // Every workload completes two laps on the core matching the model with
    // a deterministic retired count.
    for (const Workload& workload : image.workloads)
    {
        const rex86::bench::WorkloadResult smoke = rex86::bench::RunSmoke(image, workload);
        REX86_CHECK(context, smoke.verified);
        if (!smoke.verified)
        {
            context.Fail(workload.name + ": " + smoke.failure, __FILE__, __LINE__);
            continue;
        }
        // At least two laps, so the determinism check ran.
        REX86_CHECK(context, smoke.laps >= 2);
        REX86_CHECK(context, smoke.lap_instructions > 1000);
    }

    // Frame slicing: a budget smaller than a lap retires exactly the budget
    // per frame, laps complete across frames, and a lap ending exactly at
    // the budget is verified.
    const Workload* alu = rex86::bench::FindWorkload(image, "alu");
    REX86_CHECK(context, alu != nullptr);
    if (alu != nullptr)
    {
        rex86::bench::WorkloadResult probe = rex86::bench::RunSmoke(image, *alu);
        const std::uint64_t lap = probe.lap_instructions;
        REX86_CHECK(context, lap > 0);

        rex86::bench::Options options;
        options.frame_budget = lap / 3 + 7;
        options.frames = 7;
        const rex86::bench::WorkloadResult sliced =
            rex86::bench::RunWorkload(image, *alu, options);
        REX86_CHECK(context, sliced.verified);
        REX86_CHECK_EQ(context, sliced.frame_ms.size(), std::size_t{7});
        REX86_CHECK_EQ(context, sliced.retired, options.frame_budget * 7);
        REX86_CHECK_EQ(context, sliced.laps, (options.frame_budget * 7) / lap);
        REX86_CHECK_EQ(context, sliced.lap_instructions, lap);

        options.frame_budget = lap;
        options.frames = 2;
        const rex86::bench::WorkloadResult exact =
            rex86::bench::RunWorkload(image, *alu, options);
        REX86_CHECK(context, exact.verified);
        REX86_CHECK_EQ(context, exact.laps, std::uint64_t{2});

        // A budget too small for any lap is reported, not counted as ok.
        options.frame_budget = 10;
        options.frames = 1;
        const rex86::bench::WorkloadResult none =
            rex86::bench::RunWorkload(image, *alu, options);
        REX86_CHECK(context, !none.verified);
        REX86_CHECK_EQ(context, none.laps, std::uint64_t{0});
    }

    // A wrong expectation is caught at the gate.
    if (alu != nullptr)
    {
        Workload wrong = *alu;
        wrong.expected_edi ^= 1u;
        const rex86::bench::WorkloadResult result = rex86::bench::RunSmoke(image, wrong);
        REX86_CHECK(context, !result.verified);
        REX86_CHECK(context, result.failure.find("edi expected") != std::string::npos);
    }

    // Machine: the image is laid out with the page attributes the design
    // states, and the state returns to the lap's start.
    if (alu != nullptr)
    {
        Machine machine(image, *alu);
        REX86_CHECK(context, machine.loaded());
        const rex86::PageAttributeTable& pages = machine.memory().pages();
        REX86_CHECK(context, pages.AllHave(rex86::bench::kCodeBase, rex86::bench::kCodeSize,
                                           rex86::kPageReadExecute));
        REX86_CHECK(context, !pages.AllHave(rex86::bench::kCodeBase, 1, rex86::PageFlag::kWrite));
        REX86_CHECK(context, pages.AllHave(rex86::bench::kDataBase, rex86::bench::kDataSize,
                                           rex86::kPageReadWrite));
        REX86_CHECK(context, pages.Get(0) == rex86::PageFlag::kNone);
        REX86_CHECK_EQ(context, machine.cpu().state().eip, alu->entry);
        REX86_CHECK_EQ(context, machine.cpu().state().Get(rex86::Gpr::kEsp),
                       rex86::bench::kStackTop);
        REX86_CHECK(context, machine.cpu().IsGate(alu->gate));
    }
}

void StatisticsTests(rex86::test::Context& context)
{
    const std::vector<double> values = {5.0, 1.0, 4.0, 2.0, 3.0};
    REX86_CHECK_EQ(context, rex86::bench::Percentile(values, 0.50), 3.0);
    // ceil(0.99 * 5) - 1 = 4: the maximum.
    REX86_CHECK_EQ(context, rex86::bench::Percentile(values, 0.99), 5.0);
    REX86_CHECK_EQ(context, rex86::bench::Percentile(values, 1.00), 5.0);
    REX86_CHECK_EQ(context, rex86::bench::Percentile(values, 0.0), 1.0);
    REX86_CHECK_EQ(context, rex86::bench::Percentile({}, 0.5), 0.0);
    std::vector<double> hundred;
    for (int i = 1; i <= 100; ++i)
    {
        hundred.push_back(static_cast<double>(i));
    }
    REX86_CHECK_EQ(context, rex86::bench::Percentile(hundred, 0.99), 99.0);
    REX86_CHECK_EQ(context, rex86::bench::Percentile(hundred, 0.50), 50.0);

    // The boards' frame worth at IPC 1.0 (design #27, decision 4).
    REX86_CHECK_EQ(context, rex86::bench::FrameInstructions(rex86::bench::kBoards[0], 1.0),
                   std::uint64_t{6666667});
    REX86_CHECK_EQ(context, rex86::bench::FrameInstructions(rex86::bench::kBoards[3], 1.0),
                   std::uint64_t{23333333});
    REX86_CHECK_EQ(context, rex86::bench::FrameInstructions(rex86::bench::kBoards[2], 0.5),
                   std::uint64_t{10833333});

    rex86::bench::WorkloadResult result;
    result.retired = 3000000;
    result.seconds = 1.5;
    REX86_CHECK_EQ(context, result.Mips(), 2.0);
}

}  // namespace

void RunBenchTests(rex86::test::Context& context)
{
    AssemblerTests(context);
    WorkloadTests(context);
    StatisticsTests(context);
}
