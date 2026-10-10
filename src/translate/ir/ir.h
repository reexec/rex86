// The translation IR (design #43, decision 2): one block of straight-line
// code over 32-bit values, with explicit guest-state access, memory checks
// that exit to the interpreter, and exits at the end. The evaluator
// (evaluator.h) defines what each operation means; backends reproduce it.

#ifndef REX86_TRANSLATE_IR_IR_H_
#define REX86_TRANSLATE_IR_IR_H_

#include <cstdint>
#include <string>
#include <vector>

#include "rex86/cpu_state.h"

namespace rex86::translate::ir
{

// A value is the index of the operation that defines it.
using Value = std::uint32_t;
inline constexpr Value kNoValue = 0xFFFFFFFFu;

enum class Op : std::uint8_t
{
    // Values.
    kConst,    // imm
    kGetReg,   // the 32-bit GPR `slot`
    kGetFlag,  // 1 when the EFLAGS bit `imm` is set, else 0
    kAdd,      // a + b
    kSub,      // a - b
    kAnd,      // a & b
    kOr,       // a | b
    kXor,      // a ^ b
    kNot,      // ~a
    kMul,      // low 32 bits of a * b
    kMulHiS,   // high 32 bits of the signed 64-bit product
    kMulHiU,   // high 32 bits of the unsigned 64-bit product
    kShl,      // a << (b & 31)
    kShr,      // a >> (b & 31), logical
    kSar,      // a >> (b & 31), arithmetic
    kEq,       // 1 when a == b
    kNe,       // 1 when a != b
    kLtU,      // 1 when a < b, unsigned
    kLtS,      // 1 when a < b, signed
    kSelect,   // c != 0 ? a : b (operands a, b, c)
    kParity,   // 1 when the low byte of a has an even number of one bits
    kLoad,     // `bytes` bytes at segment `slot`, offset a, zero-extended
    // Effects.
    kSetReg,   // GPR `slot` = a
    kSetFlag,  // EFLAGS bit `imm` = a (0 or 1)
    kStore,    // `bytes` bytes of b at segment `slot`, offset a
    kCheck,    // exit to the interpreter unless the access would succeed
               // (segment `slot`, offset a, `bytes`, `write`)
    kBegin,    // instruction `index` starts at EIP imm
    kExitIf,   // when a != 0, exit `kind` to EIP imm after `index` steps
    kExit,     // exit `kind` after `index` steps, to EIP imm or, when
               // `dynamic`, to the value a
};

enum class ExitKind : std::uint8_t
{
    kContinue,   // continue at EIP: every instruction before it retired
    kInterpret,  // the interpreter executes the instruction at EIP
};

struct Inst
{
    Op op = Op::kConst;
    // A GPR (Gpr numbering) for register operations, a Segment for memory.
    std::uint8_t slot = 0;
    // 1, 2 or 4 for memory operations.
    std::uint8_t bytes = 0;
    bool write = false;
    bool dynamic = false;
    ExitKind kind = ExitKind::kContinue;
    std::uint32_t imm = 0;
    // The instruction index for kBegin, the step count for exits.
    std::uint32_t index = 0;
    Value a = kNoValue;
    Value b = kNoValue;
    Value c = kNoValue;
};

// True for operations that define a value.
bool DefinesValue(Op op);
// True for operations whose only effect is their value.
bool IsPure(Op op);

struct Block
{
    std::uint32_t start_eip = 0;
    // Guest instructions in the block; an exit after all of them reports
    // this many steps.
    std::uint32_t instruction_count = 0;
    // The linear page addresses the block's bytes came from (one or two)
    // and their translation generations when it was formed.
    std::uint32_t page_count = 0;
    std::uint32_t pages[2] = {};
    std::uint32_t generations[2] = {};
    std::vector<Inst> insts;
};

// Appends operations to a block; every helper returns the value it defines.
class Builder
{
public:
    explicit Builder(Block* block) : block_(block) {}

    Value Const(std::uint32_t value);
    Value GetReg(Gpr reg);
    Value GetFlag(std::uint32_t flag);
    Value Binary(Op op, Value a, Value b);
    Value Not(Value a);
    Value Select(Value condition, Value if_true, Value if_false);
    Value Parity(Value a);
    Value Load(Segment segment, Value offset, unsigned bytes);
    void SetReg(Gpr reg, Value value);
    void SetFlag(std::uint32_t flag, Value value);
    void Store(Segment segment, Value offset, unsigned bytes, Value value);
    void Check(Segment segment, Value offset, unsigned bytes, bool write);
    void Begin(std::uint32_t index, std::uint32_t eip);
    void ExitIf(Value condition, ExitKind kind, std::uint32_t eip, std::uint32_t steps);
    void Exit(ExitKind kind, std::uint32_t eip, std::uint32_t steps);
    void ExitTo(Value eip, std::uint32_t steps);

    // Shorthands.
    Value Add(Value a, Value b) { return Binary(Op::kAdd, a, b); }
    Value Sub(Value a, Value b) { return Binary(Op::kSub, a, b); }
    Value And(Value a, Value b) { return Binary(Op::kAnd, a, b); }
    Value Or(Value a, Value b) { return Binary(Op::kOr, a, b); }
    Value Xor(Value a, Value b) { return Binary(Op::kXor, a, b); }
    Value Shl(Value a, Value b) { return Binary(Op::kShl, a, b); }
    Value Shr(Value a, Value b) { return Binary(Op::kShr, a, b); }
    Value Sar(Value a, Value b) { return Binary(Op::kSar, a, b); }
    Value Eq(Value a, Value b) { return Binary(Op::kEq, a, b); }
    Value Ne(Value a, Value b) { return Binary(Op::kNe, a, b); }
    Value LtU(Value a, Value b) { return Binary(Op::kLtU, a, b); }
    Value LtS(Value a, Value b) { return Binary(Op::kLtS, a, b); }

    Block* block() const { return block_; }

private:
    Value Push(const Inst& inst);
    Block* block_;
};

// A readable listing, one operation per line, for failure reports.
std::string Dump(const Block& block);

}  // namespace rex86::translate::ir

#endif  // REX86_TRANSLATE_IR_IR_H_
