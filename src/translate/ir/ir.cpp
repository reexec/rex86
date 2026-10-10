#include "translate/ir/ir.h"

#include <cstdio>

namespace rex86::translate::ir
{

bool DefinesValue(const Op op)
{
    switch (op)
    {
        case Op::kSetReg:
        case Op::kSetFlag:
        case Op::kStore:
        case Op::kCheck:
        case Op::kBegin:
        case Op::kExitIf:
        case Op::kExit:
            return false;
        default:
            return true;
    }
}

bool IsPure(const Op op)
{
    // A load only follows the check of its own access (design #43,
    // decision 3), so it cannot fault and removing an unused one is safe.
    return DefinesValue(op);
}

Value Builder::Push(const Inst& inst)
{
    block_->insts.push_back(inst);
    return static_cast<Value>(block_->insts.size() - 1);
}

Value Builder::Const(const std::uint32_t value)
{
    Inst inst;
    inst.op = Op::kConst;
    inst.imm = value;
    return Push(inst);
}

Value Builder::GetReg(const Gpr reg)
{
    Inst inst;
    inst.op = Op::kGetReg;
    inst.slot = static_cast<std::uint8_t>(reg);
    return Push(inst);
}

Value Builder::GetFlag(const std::uint32_t flag)
{
    Inst inst;
    inst.op = Op::kGetFlag;
    inst.imm = flag;
    return Push(inst);
}

Value Builder::Binary(const Op op, const Value a, const Value b)
{
    Inst inst;
    inst.op = op;
    inst.a = a;
    inst.b = b;
    return Push(inst);
}

Value Builder::Not(const Value a)
{
    Inst inst;
    inst.op = Op::kNot;
    inst.a = a;
    return Push(inst);
}

Value Builder::Select(const Value condition, const Value if_true, const Value if_false)
{
    Inst inst;
    inst.op = Op::kSelect;
    inst.a = if_true;
    inst.b = if_false;
    inst.c = condition;
    return Push(inst);
}

Value Builder::Parity(const Value a)
{
    Inst inst;
    inst.op = Op::kParity;
    inst.a = a;
    return Push(inst);
}

Value Builder::Load(const Segment segment, const Value offset, const unsigned bytes)
{
    Inst inst;
    inst.op = Op::kLoad;
    inst.slot = static_cast<std::uint8_t>(segment);
    inst.bytes = static_cast<std::uint8_t>(bytes);
    inst.a = offset;
    return Push(inst);
}

void Builder::SetReg(const Gpr reg, const Value value)
{
    Inst inst;
    inst.op = Op::kSetReg;
    inst.slot = static_cast<std::uint8_t>(reg);
    inst.a = value;
    Push(inst);
}

void Builder::SetFlag(const std::uint32_t flag, const Value value)
{
    Inst inst;
    inst.op = Op::kSetFlag;
    inst.imm = flag;
    inst.a = value;
    Push(inst);
}

void Builder::Store(const Segment segment, const Value offset, const unsigned bytes,
                    const Value value)
{
    Inst inst;
    inst.op = Op::kStore;
    inst.slot = static_cast<std::uint8_t>(segment);
    inst.bytes = static_cast<std::uint8_t>(bytes);
    inst.a = offset;
    inst.b = value;
    Push(inst);
}

void Builder::Check(const Segment segment, const Value offset, const unsigned bytes,
                    const bool write)
{
    Inst inst;
    inst.op = Op::kCheck;
    inst.slot = static_cast<std::uint8_t>(segment);
    inst.bytes = static_cast<std::uint8_t>(bytes);
    inst.write = write;
    inst.a = offset;
    Push(inst);
}

void Builder::Begin(const std::uint32_t index, const std::uint32_t eip)
{
    Inst inst;
    inst.op = Op::kBegin;
    inst.index = index;
    inst.imm = eip;
    Push(inst);
}

void Builder::ExitIf(const Value condition, const ExitKind kind, const std::uint32_t eip,
                     const std::uint32_t steps)
{
    Inst inst;
    inst.op = Op::kExitIf;
    inst.kind = kind;
    inst.imm = eip;
    inst.index = steps;
    inst.a = condition;
    Push(inst);
}

void Builder::Exit(const ExitKind kind, const std::uint32_t eip, const std::uint32_t steps)
{
    Inst inst;
    inst.op = Op::kExit;
    inst.kind = kind;
    inst.imm = eip;
    inst.index = steps;
    Push(inst);
}

void Builder::ExitTo(const Value eip, const std::uint32_t steps)
{
    Inst inst;
    inst.op = Op::kExit;
    inst.kind = ExitKind::kContinue;
    inst.dynamic = true;
    inst.index = steps;
    inst.a = eip;
    Push(inst);
}

namespace
{

const char* Name(const Op op)
{
    switch (op)
    {
        case Op::kConst: return "const";
        case Op::kGetReg: return "getreg";
        case Op::kGetFlag: return "getflag";
        case Op::kAdd: return "add";
        case Op::kSub: return "sub";
        case Op::kAnd: return "and";
        case Op::kOr: return "or";
        case Op::kXor: return "xor";
        case Op::kNot: return "not";
        case Op::kMul: return "mul";
        case Op::kMulHiS: return "mulhis";
        case Op::kMulHiU: return "mulhiu";
        case Op::kShl: return "shl";
        case Op::kShr: return "shr";
        case Op::kSar: return "sar";
        case Op::kEq: return "eq";
        case Op::kNe: return "ne";
        case Op::kLtU: return "ltu";
        case Op::kLtS: return "lts";
        case Op::kSelect: return "select";
        case Op::kParity: return "parity";
        case Op::kLoad: return "load";
        case Op::kSetReg: return "setreg";
        case Op::kSetFlag: return "setflag";
        case Op::kStore: return "store";
        case Op::kCheck: return "check";
        case Op::kBegin: return "begin";
        case Op::kExitIf: return "exitif";
        case Op::kExit: return "exit";
    }
    return "?";
}

}  // namespace

std::string Dump(const Block& block)
{
    std::string text;
    char line[160];
    std::snprintf(line, sizeof line, "block eip=%08X instructions=%u pages=%u\n",
                  static_cast<unsigned>(block.start_eip),
                  static_cast<unsigned>(block.instruction_count),
                  static_cast<unsigned>(block.page_count));
    text += line;
    for (std::size_t i = 0; i < block.insts.size(); ++i)
    {
        const Inst& inst = block.insts[i];
        std::snprintf(line, sizeof line,
                      "  %3zu %-8s slot=%u bytes=%u write=%d dyn=%d kind=%u imm=%08X index=%u a=%d b=%d c=%d\n",
                      i, Name(inst.op), inst.slot, inst.bytes, inst.write ? 1 : 0,
                      inst.dynamic ? 1 : 0, static_cast<unsigned>(inst.kind),
                      static_cast<unsigned>(inst.imm), static_cast<unsigned>(inst.index),
                      inst.a == kNoValue ? -1 : static_cast<int>(inst.a),
                      inst.b == kNoValue ? -1 : static_cast<int>(inst.b),
                      inst.c == kNoValue ? -1 : static_cast<int>(inst.c));
        text += line;
    }
    return text;
}

}  // namespace rex86::translate::ir
