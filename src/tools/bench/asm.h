// A byte assembler for the benchmark workloads (design #27, decision 2). It
// knows only the encodings the kernels use, which keeps it small and keeps
// every emitted form checkable by the unit tests against the core's decoder.
// Labels resolve forward and backward references in Finish.

#ifndef REX86_TOOLS_BENCH_ASM_H_
#define REX86_TOOLS_BENCH_ASM_H_

#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

namespace rex86::bench
{

enum class Reg : std::uint8_t
{
    kEax = 0,
    kEcx = 1,
    kEdx = 2,
    kEbx = 3,
    kEsp = 4,
    kEbp = 5,
    kEsi = 6,
    kEdi = 7,
};

// The condition code in the low nibble of Jcc.
enum class Cond : std::uint8_t
{
    kO = 0,
    kNo = 1,
    kB = 2,
    kNb = 3,
    kZ = 4,
    kNz = 5,
    kBe = 6,
    kNbe = 7,
    kS = 8,
    kNs = 9,
    kP = 10,
    kNp = 11,
    kL = 12,
    kNl = 13,
    kLe = 14,
    kNle = 15,
};

// The /digit of the 0x81/0x83 group and the low bits of the short forms.
enum class Alu : std::uint8_t
{
    kAdd = 0,
    kOr = 1,
    kAdc = 2,
    kSbb = 3,
    kAnd = 4,
    kSub = 5,
    kXor = 6,
    kCmp = 7,
};

// The /digit of the 0xC1 shift group.
enum class Shift : std::uint8_t
{
    kRol = 0,
    kRor = 1,
    kShl = 4,
    kShr = 5,
    kSar = 7,
};

enum class BranchWidth : std::uint8_t
{
    // rel8; Finish fails when the distance does not fit.
    kShort,
    // rel32.
    kNear,
};

// A memory operand [base + index * scale + disp]. The factories cover the
// forms the kernels use; scale is 1, 2, 4 or 8.
struct Mem
{
    bool has_base = false;
    Reg base = Reg::kEax;
    bool has_index = false;
    Reg index = Reg::kEax;
    std::uint8_t scale = 1;
    std::int32_t disp = 0;

    static Mem Abs(std::uint32_t address);
    static Mem BaseDisp(Reg base, std::int32_t disp);
    static Mem BaseIndex(Reg base, Reg index, std::uint8_t scale, std::int32_t disp);
    static Mem IndexDisp(Reg index, std::uint8_t scale, std::uint32_t address);
};

struct Label
{
    std::int32_t index = -1;
};

class Assembler
{
public:
    explicit Assembler(std::uint32_t base);

    [[nodiscard]] std::uint32_t base() const
    {
        return base_;
    }

    // The guest address of the next byte.
    [[nodiscard]] std::uint32_t Here() const;

    [[nodiscard]] const std::vector<std::uint8_t>& bytes() const
    {
        return bytes_;
    }

    Label NewLabel();
    void Bind(Label label);
    // The bound address, or 0 when unbound.
    [[nodiscard]] std::uint32_t AddressOf(Label label) const;

    // Resolves every branch. False with a message when a label is unbound
    // or a short branch does not reach.
    bool Finish(std::string* error);

    void Byte(std::uint8_t value);
    void Bytes(std::initializer_list<std::uint8_t> values);
    void Dword(std::uint32_t value);

    // Data movement.
    void MovImm(Reg dst, std::uint32_t imm);
    void MovRR(Reg dst, Reg src);
    void MovLoad(Reg dst, const Mem& src);
    void MovStore(const Mem& dst, Reg src);
    // mov byte [mem], r8 where r8 is the low byte of src (AL, CL, DL, BL).
    void MovStore8(const Mem& dst, Reg src);
    void MovzxLoad8(Reg dst, const Mem& src);
    void Lea(Reg dst, const Mem& src);
    void Push(Reg reg);
    void Pop(Reg reg);
    void PushImm(std::uint32_t imm);

    // Arithmetic.
    void AluRR(Alu op, Reg dst, Reg src);
    void AluRI(Alu op, Reg dst, std::uint32_t imm);
    void AluRM(Alu op, Reg dst, const Mem& src);
    void AluMR(Alu op, const Mem& dst, Reg src);
    void ImulRRI(Reg dst, Reg src, std::uint32_t imm);
    void ImulRR(Reg dst, Reg src);
    void ShiftRI(Shift op, Reg reg, std::uint8_t count);
    void Inc(Reg reg);
    void Dec(Reg reg);
    void TestRR(Reg a, Reg b);

    // Control flow.
    void Jmp(Label target, BranchWidth width = BranchWidth::kNear);
    void Jcc(Cond cond, Label target, BranchWidth width = BranchWidth::kNear);
    void Call(Label target);
    void Ret();
    void RetImm(std::uint16_t bytes);
    void Nop();

    // Strings.
    void Cld();
    void RepMovsd();
    void RepStosd();

    // x87.
    void FldM64(const Mem& src);
    void FstpM64(const Mem& dst);
    void FaddM64(const Mem& src);
    void FmulM64(const Mem& src);
    void FaddpSt1St0();
    void FxchSt1();
    void FistpM32(const Mem& dst);
    void Fldz();
    void Fld1();

private:
    struct Fixup
    {
        std::uint32_t offset = 0;
        std::int32_t label = -1;
        std::uint8_t width = 4;
    };

    void ModRm(std::uint8_t reg_field, const Mem& mem);
    void ModRmReg(std::uint8_t reg_field, Reg rm);
    void Branch(std::initializer_list<std::uint8_t> opcode, Label target, std::uint8_t width);

    std::uint32_t base_ = 0;
    std::vector<std::uint8_t> bytes_;
    // The bound offset of each label, or -1.
    std::vector<std::int64_t> labels_;
    std::vector<Fixup> fixups_;
};

}  // namespace rex86::bench

#endif  // REX86_TOOLS_BENCH_ASM_H_
