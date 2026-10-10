// Lowering mirrors the interpreter instruction by instruction: each case
// below names the interpreter function whose semantics it reproduces, and
// the differential tests (tests/unit/ir_test.cpp) hold the two equal. An
// instruction emits its address computations, then every memory check, then
// its effects (design #43, decision 3), so a failed check exits with the
// instruction's starting state.

#include "translate/ir/frontend.h"

#include <array>
#include <cstring>

#include "interp/access.h"

namespace rex86::translate::ir
{

namespace
{

constexpr std::uint32_t Mask(const unsigned width)
{
    return width >= 32 ? 0xFFFFFFFFu : ((1u << width) - 1u);
}

bool HasRawPrefix(const decode::DecodedInstruction& d, const std::uint8_t value)
{
    const auto& raw = d.instruction.raw;
    for (ZyanU8 i = 0; i < raw.prefix_count; ++i)
    {
        if (raw.prefixes[i].value == value)
        {
            return true;
        }
    }
    return false;
}

bool IsGpr(const ZydisRegister reg)
{
    const ZydisRegisterClass cls = ZydisRegisterGetClass(reg);
    return cls == ZYDIS_REGCLASS_GPR8 || cls == ZYDIS_REGCLASS_GPR16 ||
           cls == ZYDIS_REGCLASS_GPR32;
}

bool IsGpr32OrNone(const ZydisRegister reg)
{
    return reg == ZYDIS_REGISTER_NONE || ZydisRegisterGetClass(reg) == ZYDIS_REGCLASS_GPR32;
}

// Every visible operand is a GPR, a 32-bit-addressed memory operand or an
// immediate.
bool OperandsSupported(const decode::DecodedInstruction& d)
{
    for (ZyanU8 i = 0; i < d.instruction.operand_count_visible; ++i)
    {
        const ZydisDecodedOperand& op = d.operands[i];
        switch (op.type)
        {
            case ZYDIS_OPERAND_TYPE_REGISTER:
                if (!IsGpr(op.reg.value))
                {
                    return false;
                }
                break;
            case ZYDIS_OPERAND_TYPE_MEMORY:
                if ((op.mem.type != ZYDIS_MEMOP_TYPE_MEM && op.mem.type != ZYDIS_MEMOP_TYPE_AGEN) ||
                    !IsGpr32OrNone(op.mem.base) || !IsGpr32OrNone(op.mem.index))
                {
                    return false;
                }
                break;
            case ZYDIS_OPERAND_TYPE_IMMEDIATE:
                break;
            default:
                return false;
        }
    }
    return true;
}

// A GPR as a 32-bit slot, the field's shift and width.
struct RegisterField
{
    Gpr slot;
    unsigned shift;
    unsigned width;
};

RegisterField FieldOf(const ZydisRegister reg)
{
    if (reg >= ZYDIS_REGISTER_EAX && reg <= ZYDIS_REGISTER_EDI)
    {
        return {static_cast<Gpr>(reg - ZYDIS_REGISTER_EAX), 0, 32};
    }
    if (reg >= ZYDIS_REGISTER_AX && reg <= ZYDIS_REGISTER_DI)
    {
        return {static_cast<Gpr>(reg - ZYDIS_REGISTER_AX), 0, 16};
    }
    if (reg >= ZYDIS_REGISTER_AH && reg <= ZYDIS_REGISTER_BH)
    {
        return {static_cast<Gpr>(reg - ZYDIS_REGISTER_AH), 8, 8};
    }
    return {static_cast<Gpr>(reg - ZYDIS_REGISTER_AL), 0, 8};
}

bool IsConditionalBranch(const ZydisMnemonic m)
{
    switch (m)
    {
        case ZYDIS_MNEMONIC_JO: case ZYDIS_MNEMONIC_JNO: case ZYDIS_MNEMONIC_JB:
        case ZYDIS_MNEMONIC_JNB: case ZYDIS_MNEMONIC_JZ: case ZYDIS_MNEMONIC_JNZ:
        case ZYDIS_MNEMONIC_JBE: case ZYDIS_MNEMONIC_JNBE: case ZYDIS_MNEMONIC_JS:
        case ZYDIS_MNEMONIC_JNS: case ZYDIS_MNEMONIC_JP: case ZYDIS_MNEMONIC_JNP:
        case ZYDIS_MNEMONIC_JL: case ZYDIS_MNEMONIC_JNL: case ZYDIS_MNEMONIC_JLE:
        case ZYDIS_MNEMONIC_JNLE:
            return true;
        default:
            return false;
    }
}

bool IsSetcc(const ZydisMnemonic m)
{
    switch (m)
    {
        case ZYDIS_MNEMONIC_SETO: case ZYDIS_MNEMONIC_SETNO: case ZYDIS_MNEMONIC_SETB:
        case ZYDIS_MNEMONIC_SETNB: case ZYDIS_MNEMONIC_SETZ: case ZYDIS_MNEMONIC_SETNZ:
        case ZYDIS_MNEMONIC_SETBE: case ZYDIS_MNEMONIC_SETNBE: case ZYDIS_MNEMONIC_SETS:
        case ZYDIS_MNEMONIC_SETNS: case ZYDIS_MNEMONIC_SETP: case ZYDIS_MNEMONIC_SETNP:
        case ZYDIS_MNEMONIC_SETL: case ZYDIS_MNEMONIC_SETNL: case ZYDIS_MNEMONIC_SETLE:
        case ZYDIS_MNEMONIC_SETNLE:
            return true;
        default:
            return false;
    }
}

bool IsCmovcc(const ZydisMnemonic m)
{
    switch (m)
    {
        case ZYDIS_MNEMONIC_CMOVO: case ZYDIS_MNEMONIC_CMOVNO: case ZYDIS_MNEMONIC_CMOVB:
        case ZYDIS_MNEMONIC_CMOVNB: case ZYDIS_MNEMONIC_CMOVZ: case ZYDIS_MNEMONIC_CMOVNZ:
        case ZYDIS_MNEMONIC_CMOVBE: case ZYDIS_MNEMONIC_CMOVNBE: case ZYDIS_MNEMONIC_CMOVS:
        case ZYDIS_MNEMONIC_CMOVNS: case ZYDIS_MNEMONIC_CMOVP: case ZYDIS_MNEMONIC_CMOVNP:
        case ZYDIS_MNEMONIC_CMOVL: case ZYDIS_MNEMONIC_CMOVNL: case ZYDIS_MNEMONIC_CMOVLE:
        case ZYDIS_MNEMONIC_CMOVNLE:
            return true;
        default:
            return false;
    }
}

class Lowerer
{
public:
    Lowerer(Builder& b, const decode::DecodedInstruction& d, const std::uint32_t eip,
            const std::uint32_t index)
        : b_(b), d_(d), ops_(d.operands), eip_(eip), next_eip_(eip + d.Length()), index_(index)
    {
        addresses_.fill(kNoValue);
    }

    // Emits the instruction; true when it ends the block (its own exits).
    bool Lower();

private:
    // --- operands -------------------------------------------------------

    std::size_t IndexOf(const ZydisDecodedOperand& op) const
    {
        return static_cast<std::size_t>(&op - ops_);
    }

    // interp::EffectiveAddress at 32-bit addressing. esp_value, when given,
    // stands for ESP (POP m computes its address after the increment).
    Value Address(const ZydisDecodedOperand& op, const Value esp_value = kNoValue)
    {
        Value& cached = addresses_[IndexOf(op)];
        if (cached != kNoValue)
        {
            return cached;
        }
        Value address = b_.Const(static_cast<std::uint32_t>(op.mem.disp.value));
        const auto reg = [&](const ZydisRegister r) {
            const Gpr slot = static_cast<Gpr>(r - ZYDIS_REGISTER_EAX);
            return slot == Gpr::kEsp && esp_value != kNoValue ? esp_value : b_.GetReg(slot);
        };
        if (op.mem.base != ZYDIS_REGISTER_NONE)
        {
            address = b_.Add(address, reg(op.mem.base));
        }
        if (op.mem.index != ZYDIS_REGISTER_NONE)
        {
            address = b_.Add(address, b_.Binary(Op::kMul, reg(op.mem.index),
                                                 b_.Const(static_cast<std::uint32_t>(op.mem.scale))));
        }
        cached = address;
        return address;
    }

    Segment SegmentOf(const ZydisDecodedOperand& op) const
    {
        return interp::SegmentOf(d_, op);
    }

    // The checks of a memory operand: a read, a write, or both for a
    // read-modify-write destination. Registers and immediates need none.
    void CheckOperand(const ZydisDecodedOperand& op, const bool read, const bool write,
                      const Value esp_value = kNoValue)
    {
        if (op.type != ZYDIS_OPERAND_TYPE_MEMORY)
        {
            return;
        }
        const Value address = Address(op, esp_value);
        if (read)
        {
            b_.Check(SegmentOf(op), address, op.size / 8u, false);
        }
        if (write)
        {
            b_.Check(SegmentOf(op), address, op.size / 8u, true);
        }
    }

    Value ReadRegister(const ZydisRegister reg)
    {
        const RegisterField field = FieldOf(reg);
        Value value = b_.GetReg(field.slot);
        if (field.shift != 0)
        {
            value = b_.Shr(value, b_.Const(field.shift));
        }
        return Masked(value, field.width);
    }

    void WriteRegister(const ZydisRegister reg, const Value value)
    {
        const RegisterField field = FieldOf(reg);
        if (field.width == 32)
        {
            b_.SetReg(field.slot, value);
            return;
        }
        const std::uint32_t mask = Mask(field.width) << field.shift;
        Value part = Masked(value, field.width);
        if (field.shift != 0)
        {
            part = b_.Shl(part, b_.Const(field.shift));
        }
        b_.SetReg(field.slot, b_.Or(b_.And(b_.GetReg(field.slot), b_.Const(~mask)), part));
    }

    // interp::ReadOperand, after the operand's checks.
    Value Read(const ZydisDecodedOperand& op)
    {
        switch (op.type)
        {
            case ZYDIS_OPERAND_TYPE_REGISTER:
                return ReadRegister(op.reg.value);
            case ZYDIS_OPERAND_TYPE_MEMORY:
                return b_.Load(SegmentOf(op), Address(op), op.size / 8u);
            default:
                if (op.imm.is_signed)
                {
                    return b_.Const(static_cast<std::uint32_t>(static_cast<std::int64_t>(op.imm.value.s)));
                }
                return b_.Const(static_cast<std::uint32_t>(op.imm.value.u) & Mask(op.size));
        }
    }

    // interp::WriteOperand, after the operand's checks.
    void Write(const ZydisDecodedOperand& op, const Value value)
    {
        if (op.type == ZYDIS_OPERAND_TYPE_REGISTER)
        {
            WriteRegister(op.reg.value, value);
        }
        else
        {
            b_.Store(SegmentOf(op), Address(op), op.size / 8u, value);
        }
    }

    // --- values ---------------------------------------------------------

    Value Masked(const Value value, const unsigned width)
    {
        return width >= 32 ? value : b_.And(value, b_.Const(Mask(width)));
    }

    Value SignExtend(const Value value, const unsigned width)
    {
        if (width >= 32)
        {
            return value;
        }
        const Value shift = b_.Const(32u - width);
        return b_.Sar(b_.Shl(value, shift), shift);
    }

    Value Bit(const Value value, const unsigned bit)
    {
        return b_.And(b_.Shr(value, b_.Const(bit)), b_.Const(1));
    }

    Value Invert(const Value bit)
    {
        return b_.Xor(bit, b_.Const(1));
    }

    // --- flags (interp/flags.h) -----------------------------------------

    // SetResultFlags: result is already masked to the width.
    void ResultFlags(const unsigned width, const Value result)
    {
        b_.SetFlag(kEflagsZero, b_.Eq(result, b_.Const(0)));
        b_.SetFlag(kEflagsSign, Bit(result, width - 1u));
        b_.SetFlag(kEflagsParity, b_.Parity(result));
    }

    // SetArithmeticFlags: lhs and rhs masked to the width, carry_in 0 or 1,
    // result masked.
    void ArithmeticFlags(const unsigned width, const Value lhs, const Value rhs,
                         const Value carry_in, const Value result, const bool subtract,
                         const bool set_carry = true)
    {
        if (set_carry)
        {
            Value carry;
            if (subtract)
            {
                carry = b_.Or(b_.LtU(lhs, rhs), b_.And(carry_in, b_.Eq(lhs, rhs)));
            }
            else if (width < 32)
            {
                carry = Bit(b_.Add(b_.Add(lhs, rhs), carry_in), width);
            }
            else
            {
                carry = b_.Or(b_.LtU(result, lhs), b_.And(carry_in, b_.Eq(result, lhs)));
            }
            b_.SetFlag(kEflagsCarry, carry);
        }
        const Value overflow = subtract
            ? b_.And(b_.Xor(lhs, rhs), b_.Xor(lhs, result))
            : b_.And(b_.Xor(lhs, result), b_.Xor(rhs, result));
        b_.SetFlag(kEflagsOverflow, Bit(overflow, width - 1u));
        b_.SetFlag(kEflagsAdjust, Bit(b_.Xor(b_.Xor(lhs, rhs), result), 4));
        ResultFlags(width, result);
    }

    // SetLogicFlags.
    void LogicFlags(const unsigned width, const Value result)
    {
        const Value zero = b_.Const(0);
        b_.SetFlag(kEflagsCarry, zero);
        b_.SetFlag(kEflagsOverflow, zero);
        b_.SetFlag(kEflagsAdjust, zero);
        ResultFlags(width, result);
    }

    // ConditionCodeHolds.
    Value Condition(const unsigned code)
    {
        const auto flag = [&](const std::uint32_t f) { return b_.GetFlag(f); };
        switch (code & 0xFu)
        {
            case 0x0: return flag(kEflagsOverflow);
            case 0x1: return Invert(flag(kEflagsOverflow));
            case 0x2: return flag(kEflagsCarry);
            case 0x3: return Invert(flag(kEflagsCarry));
            case 0x4: return flag(kEflagsZero);
            case 0x5: return Invert(flag(kEflagsZero));
            case 0x6: return b_.Or(flag(kEflagsCarry), flag(kEflagsZero));
            case 0x7: return Invert(b_.Or(flag(kEflagsCarry), flag(kEflagsZero)));
            case 0x8: return flag(kEflagsSign);
            case 0x9: return Invert(flag(kEflagsSign));
            case 0xA: return flag(kEflagsParity);
            case 0xB: return Invert(flag(kEflagsParity));
            case 0xC: return b_.Xor(flag(kEflagsSign), flag(kEflagsOverflow));
            case 0xD: return Invert(b_.Xor(flag(kEflagsSign), flag(kEflagsOverflow)));
            case 0xE:
                return b_.Or(flag(kEflagsZero), b_.Xor(flag(kEflagsSign), flag(kEflagsOverflow)));
            default:
                return Invert(
                    b_.Or(flag(kEflagsZero), b_.Xor(flag(kEflagsSign), flag(kEflagsOverflow))));
        }
    }

    // --- instructions ---------------------------------------------------

    void LowerAluBinary(ZydisMnemonic m);
    void LowerIncDec(bool inc);
    void LowerNeg();
    void LowerNot();
    void LowerMov();
    void LowerMovExtend(bool sign);
    void LowerLea();
    void LowerXchg();
    void LowerShift(ZydisMnemonic m);
    void LowerImul();
    void LowerPush();
    void LowerPop();
    void LowerSetcc();
    void LowerCmovcc();
    void LowerJcc();
    void LowerJmp();
    void LowerCall();
    void LowerRet();

    Builder& b_;
    const decode::DecodedInstruction& d_;
    const ZydisDecodedOperand* ops_;
    std::uint32_t eip_;
    std::uint32_t next_eip_;
    std::uint32_t index_;
    std::array<Value, ZYDIS_MAX_OPERAND_COUNT> addresses_{};
};

// interp ExecAluBinary.
void Lowerer::LowerAluBinary(const ZydisMnemonic m)
{
    const ZydisDecodedOperand& dst = ops_[0];
    const ZydisDecodedOperand& src = ops_[1];
    const unsigned width = dst.size;
    const bool write = m != ZYDIS_MNEMONIC_CMP && m != ZYDIS_MNEMONIC_TEST;
    CheckOperand(dst, true, write);
    CheckOperand(src, true, false);
    const Value lhs = Masked(Read(dst), width);
    const Value rhs = Masked(Read(src), width);
    Value result = kNoValue;
    switch (m)
    {
        case ZYDIS_MNEMONIC_ADD:
            result = Masked(b_.Add(lhs, rhs), width);
            ArithmeticFlags(width, lhs, rhs, b_.Const(0), result, false);
            break;
        case ZYDIS_MNEMONIC_ADC:
        {
            const Value carry = b_.GetFlag(kEflagsCarry);
            result = Masked(b_.Add(b_.Add(lhs, rhs), carry), width);
            ArithmeticFlags(width, lhs, rhs, carry, result, false);
            break;
        }
        case ZYDIS_MNEMONIC_SUB:
        case ZYDIS_MNEMONIC_CMP:
            result = Masked(b_.Sub(lhs, rhs), width);
            ArithmeticFlags(width, lhs, rhs, b_.Const(0), result, true);
            break;
        case ZYDIS_MNEMONIC_SBB:
        {
            const Value carry = b_.GetFlag(kEflagsCarry);
            result = Masked(b_.Sub(b_.Sub(lhs, rhs), carry), width);
            ArithmeticFlags(width, lhs, rhs, carry, result, true);
            break;
        }
        case ZYDIS_MNEMONIC_AND:
        case ZYDIS_MNEMONIC_TEST:
            result = b_.And(lhs, rhs);
            LogicFlags(width, result);
            break;
        case ZYDIS_MNEMONIC_OR:
            result = b_.Or(lhs, rhs);
            LogicFlags(width, result);
            break;
        default:  // XOR
            result = b_.Xor(lhs, rhs);
            LogicFlags(width, result);
            break;
    }
    if (write)
    {
        Write(dst, result);
    }
}

// interp Execute, INC/DEC: CF is preserved.
void Lowerer::LowerIncDec(const bool inc)
{
    const ZydisDecodedOperand& op = ops_[0];
    const unsigned width = op.size;
    CheckOperand(op, true, true);
    const Value value = Masked(Read(op), width);
    const Value one = b_.Const(1);
    const Value result = Masked(inc ? b_.Add(value, one) : b_.Sub(value, one), width);
    ArithmeticFlags(width, value, one, b_.Const(0), result, !inc, false);
    Write(op, result);
}

// interp Execute, NEG.
void Lowerer::LowerNeg()
{
    const ZydisDecodedOperand& op = ops_[0];
    const unsigned width = op.size;
    CheckOperand(op, true, true);
    const Value value = Masked(Read(op), width);
    const Value zero = b_.Const(0);
    const Value result = Masked(b_.Sub(zero, value), width);
    ArithmeticFlags(width, zero, value, zero, result, true);
    Write(op, result);
}

// interp Execute, NOT: no flags.
void Lowerer::LowerNot()
{
    const ZydisDecodedOperand& op = ops_[0];
    CheckOperand(op, true, true);
    Write(op, Masked(b_.Not(Read(op)), op.size));
}

// interp Execute, MOV.
void Lowerer::LowerMov()
{
    CheckOperand(ops_[1], true, false);
    CheckOperand(ops_[0], false, true);
    Write(ops_[0], Masked(Read(ops_[1]), ops_[0].size));
}

// interp Execute, MOVZX/MOVSX.
void Lowerer::LowerMovExtend(const bool sign)
{
    CheckOperand(ops_[1], true, false);
    Value value = Masked(Read(ops_[1]), ops_[1].size);
    if (sign)
    {
        value = SignExtend(value, ops_[1].size);
    }
    Write(ops_[0], Masked(value, ops_[0].size));
}

// interp Execute, LEA: no memory access.
void Lowerer::LowerLea()
{
    Write(ops_[0], Masked(Address(ops_[1]), ops_[0].size));
}

// interp Execute, XCHG.
void Lowerer::LowerXchg()
{
    CheckOperand(ops_[0], true, true);
    CheckOperand(ops_[1], true, true);
    const Value a = Read(ops_[0]);
    const Value b = Read(ops_[1]);
    Write(ops_[0], b);
    Write(ops_[1], a);
}

// interp ExecShift with a constant count (Covered rejects CL).
void Lowerer::LowerShift(const ZydisMnemonic m)
{
    const ZydisDecodedOperand& op = ops_[0];
    const unsigned width = op.size;
    const std::uint32_t sign_bit = width - 1u;
    const unsigned count = static_cast<unsigned>(ops_[1].imm.value.u) & 0x1Fu;
    if (count == 0)
    {
        // The operand is read and nothing changes; the read of a
        // read-modify-write destination already demands a writable
        // segment, so the write check stands in for it (stricter is safe:
        // a failed check leaves the instruction to the interpreter).
        CheckOperand(op, true, true);
        return;
    }
    CheckOperand(op, true, true);
    const Value value = Masked(Read(op), width);
    Value result = kNoValue;
    Value carry = kNoValue;
    Value overflow = kNoValue;
    switch (m)
    {
        case ZYDIS_MNEMONIC_SHL:
            result = Masked(b_.Shl(value, b_.Const(count)), width);
            if (count <= width)
            {
                carry = Bit(value, width - count);
            }
            else
            {
                carry = count % width == 0 ? b_.And(value, b_.Const(1)) : b_.Const(0);
            }
            overflow = b_.Xor(carry, Bit(result, sign_bit));
            break;
        case ZYDIS_MNEMONIC_SHR:
            result = b_.Shr(value, b_.Const(count));
            if (count <= width)
            {
                carry = Bit(value, count - 1u);
            }
            else
            {
                carry = count % width == 0 ? Bit(value, sign_bit) : b_.Const(0);
            }
            overflow = count == 1 ? Bit(value, sign_bit) : b_.Const(0);
            break;
        case ZYDIS_MNEMONIC_SAR:
        {
            const Value extended = SignExtend(value, width);
            result = Masked(b_.Sar(extended, b_.Const(count)), width);
            carry = b_.And(b_.Sar(extended, b_.Const(count - 1u)), b_.Const(1));
            overflow = b_.Const(0);
            break;
        }
        case ZYDIS_MNEMONIC_ROL:
        {
            const unsigned effective = count % width;
            result = effective == 0
                ? value
                : Masked(b_.Or(b_.Shl(value, b_.Const(effective)),
                               b_.Shr(value, b_.Const(width - effective))),
                         width);
            carry = b_.And(result, b_.Const(1));
            overflow = b_.Xor(carry, Bit(result, sign_bit));
            break;
        }
        default:  // ROR
        {
            const unsigned effective = count % width;
            result = effective == 0
                ? value
                : Masked(b_.Or(b_.Shr(value, b_.Const(effective)),
                               b_.Shl(value, b_.Const(width - effective))),
                         width);
            carry = Bit(result, sign_bit);
            overflow = Bit(b_.Xor(result, b_.Shl(result, b_.Const(1))), sign_bit);
            break;
        }
    }
    b_.SetFlag(kEflagsCarry, carry);
    b_.SetFlag(kEflagsOverflow, overflow);
    if (m == ZYDIS_MNEMONIC_SHL || m == ZYDIS_MNEMONIC_SHR || m == ZYDIS_MNEMONIC_SAR)
    {
        ResultFlags(width, result);
        b_.SetFlag(kEflagsAdjust, b_.Const(0));
    }
    Write(op, result);
}

// interp ExecImul.
void Lowerer::LowerImul()
{
    const unsigned visible = d_.instruction.operand_count_visible;
    const unsigned width = ops_[0].size;
    Value low = kNoValue;
    Value fits = kNoValue;
    if (visible == 1)
    {
        CheckOperand(ops_[0], true, false);
        const Value source = Masked(Read(ops_[0]), width);
        const Value accumulator = Masked(b_.GetReg(Gpr::kEax), width);
        if (width == 32)
        {
            low = b_.Binary(Op::kMul, accumulator, source);
            const Value high = b_.Binary(Op::kMulHiS, accumulator, source);
            b_.SetReg(Gpr::kEax, low);
            b_.SetReg(Gpr::kEdx, high);
            fits = b_.Eq(high, b_.Sar(low, b_.Const(31)));
        }
        else
        {
            const Value product =
                b_.Binary(Op::kMul, SignExtend(accumulator, width), SignExtend(source, width));
            low = Masked(product, width);
            if (width == 8)
            {
                WriteRegister(ZYDIS_REGISTER_AX, product);
            }
            else
            {
                WriteRegister(ZYDIS_REGISTER_AX, low);
                WriteRegister(ZYDIS_REGISTER_DX, Masked(b_.Shr(product, b_.Const(16)), 16));
            }
            fits = b_.Eq(SignExtend(low, width), product);
        }
    }
    else
    {
        const ZydisDecodedOperand& lhs_op = visible == 2 ? ops_[0] : ops_[1];
        const ZydisDecodedOperand& rhs_op = visible == 2 ? ops_[1] : ops_[2];
        CheckOperand(lhs_op, true, false);
        CheckOperand(rhs_op, true, false);
        const Value lhs = Read(lhs_op);
        const Value rhs = Read(rhs_op);
        if (width == 32)
        {
            low = b_.Binary(Op::kMul, lhs, rhs);
            fits = b_.Eq(b_.Binary(Op::kMulHiS, lhs, rhs), b_.Sar(low, b_.Const(31)));
        }
        else
        {
            const Value product =
                b_.Binary(Op::kMul, SignExtend(lhs, width), SignExtend(rhs, width));
            low = Masked(product, width);
            fits = b_.Eq(SignExtend(low, width), product);
        }
        Write(ops_[0], low);
    }
    const Value overflow = Invert(fits);
    b_.SetFlag(kEflagsCarry, overflow);
    b_.SetFlag(kEflagsOverflow, overflow);
    ResultFlags(width, low);
    b_.SetFlag(kEflagsAdjust, b_.Const(0));
}

// interp Execute, PUSH, with a 32-bit operand and stack.
void Lowerer::LowerPush()
{
    const Value esp = b_.GetReg(Gpr::kEsp);
    const Value new_esp = b_.Sub(esp, b_.Const(4));
    CheckOperand(ops_[0], true, false);
    b_.Check(Segment::kSs, new_esp, 4, true);
    const Value value = Read(ops_[0]);
    b_.Store(Segment::kSs, new_esp, 4, value);
    b_.SetReg(Gpr::kEsp, new_esp);
}

// interp Execute, POP: the destination's address uses the incremented ESP.
void Lowerer::LowerPop()
{
    const Value esp = b_.GetReg(Gpr::kEsp);
    const Value new_esp = b_.Add(esp, b_.Const(4));
    b_.Check(Segment::kSs, esp, 4, false);
    CheckOperand(ops_[0], false, true, new_esp);
    const Value value = b_.Load(Segment::kSs, esp, 4);
    b_.SetReg(Gpr::kEsp, new_esp);
    Write(ops_[0], value);
}

// interp ExecuteArith2, SETcc.
void Lowerer::LowerSetcc()
{
    CheckOperand(ops_[0], false, true);
    Write(ops_[0], Condition(d_.instruction.opcode));
}

// interp ExecCmov: the source is read whatever the condition.
void Lowerer::LowerCmovcc()
{
    CheckOperand(ops_[1], true, false);
    const Value source = Read(ops_[1]);
    const Value condition = Condition(d_.instruction.opcode);
    Write(ops_[0], b_.Select(condition, source, ReadRegister(ops_[0].reg.value)));
}

// interp Execute, Jcc.
void Lowerer::LowerJcc()
{
    std::uint32_t target = 0;
    d_.DirectTarget(&target);
    b_.ExitIf(Condition(d_.instruction.opcode), ExitKind::kContinue, target, index_ + 1u);
    b_.Exit(ExitKind::kContinue, next_eip_, index_ + 1u);
}

// interp Execute, near JMP.
void Lowerer::LowerJmp()
{
    std::uint32_t target = 0;
    if (d_.DirectTarget(&target))
    {
        b_.Exit(ExitKind::kContinue, target, index_ + 1u);
        return;
    }
    CheckOperand(ops_[0], true, false);
    b_.ExitTo(Read(ops_[0]), index_ + 1u);
}

// interp Execute, near CALL: the target is read before the push.
void Lowerer::LowerCall()
{
    const Value esp = b_.GetReg(Gpr::kEsp);
    const Value new_esp = b_.Sub(esp, b_.Const(4));
    std::uint32_t direct = 0;
    const bool is_direct = d_.DirectTarget(&direct);
    if (!is_direct)
    {
        CheckOperand(ops_[0], true, false);
    }
    b_.Check(Segment::kSs, new_esp, 4, true);
    const Value target = is_direct ? b_.Const(direct) : Read(ops_[0]);
    b_.Store(Segment::kSs, new_esp, 4, b_.Const(next_eip_));
    b_.SetReg(Gpr::kEsp, new_esp);
    b_.ExitTo(target, index_ + 1u);
}

// interp Execute, near RET with its optional immediate.
void Lowerer::LowerRet()
{
    const Value esp = b_.GetReg(Gpr::kEsp);
    b_.Check(Segment::kSs, esp, 4, false);
    const Value target = b_.Load(Segment::kSs, esp, 4);
    std::uint32_t release = 4;
    if (d_.instruction.operand_count_visible > 0 && ops_[0].type == ZYDIS_OPERAND_TYPE_IMMEDIATE)
    {
        release += static_cast<std::uint32_t>(ops_[0].imm.value.u);
    }
    b_.SetReg(Gpr::kEsp, b_.Add(esp, b_.Const(release)));
    b_.ExitTo(target, index_ + 1u);
}

bool Lowerer::Lower()
{
    b_.Begin(index_, eip_);
    const ZydisMnemonic m = d_.instruction.mnemonic;
    switch (m)
    {
        case ZYDIS_MNEMONIC_NOP: break;
        case ZYDIS_MNEMONIC_ADD: case ZYDIS_MNEMONIC_ADC: case ZYDIS_MNEMONIC_SUB:
        case ZYDIS_MNEMONIC_SBB: case ZYDIS_MNEMONIC_CMP: case ZYDIS_MNEMONIC_AND:
        case ZYDIS_MNEMONIC_OR: case ZYDIS_MNEMONIC_XOR: case ZYDIS_MNEMONIC_TEST:
            LowerAluBinary(m);
            break;
        case ZYDIS_MNEMONIC_INC: LowerIncDec(true); break;
        case ZYDIS_MNEMONIC_DEC: LowerIncDec(false); break;
        case ZYDIS_MNEMONIC_NEG: LowerNeg(); break;
        case ZYDIS_MNEMONIC_NOT: LowerNot(); break;
        case ZYDIS_MNEMONIC_MOV: LowerMov(); break;
        case ZYDIS_MNEMONIC_MOVZX: LowerMovExtend(false); break;
        case ZYDIS_MNEMONIC_MOVSX: LowerMovExtend(true); break;
        case ZYDIS_MNEMONIC_LEA: LowerLea(); break;
        case ZYDIS_MNEMONIC_XCHG: LowerXchg(); break;
        case ZYDIS_MNEMONIC_SHL: case ZYDIS_MNEMONIC_SHR: case ZYDIS_MNEMONIC_SAR:
        case ZYDIS_MNEMONIC_ROL: case ZYDIS_MNEMONIC_ROR:
            LowerShift(m);
            break;
        case ZYDIS_MNEMONIC_IMUL: LowerImul(); break;
        case ZYDIS_MNEMONIC_PUSH: LowerPush(); break;
        case ZYDIS_MNEMONIC_POP: LowerPop(); break;
        case ZYDIS_MNEMONIC_JMP: LowerJmp(); return true;
        case ZYDIS_MNEMONIC_CALL: LowerCall(); return true;
        case ZYDIS_MNEMONIC_RET: LowerRet(); return true;
        default:
            if (IsConditionalBranch(m))
            {
                LowerJcc();
                return true;
            }
            if (IsSetcc(m))
            {
                LowerSetcc();
            }
            else
            {
                LowerCmovcc();
            }
            break;
    }
    return false;
}

bool CoveredMnemonic(const decode::DecodedInstruction& d)
{
    const ZydisMnemonic m = d.instruction.mnemonic;
    const ZydisDecodedOperand* ops = d.operands;
    const unsigned visible = d.instruction.operand_count_visible;
    const bool width32 = d.instruction.operand_width == 32;
    switch (m)
    {
        case ZYDIS_MNEMONIC_NOP:
        case ZYDIS_MNEMONIC_ADD: case ZYDIS_MNEMONIC_ADC: case ZYDIS_MNEMONIC_SUB:
        case ZYDIS_MNEMONIC_SBB: case ZYDIS_MNEMONIC_CMP: case ZYDIS_MNEMONIC_AND:
        case ZYDIS_MNEMONIC_OR: case ZYDIS_MNEMONIC_XOR: case ZYDIS_MNEMONIC_TEST:
        case ZYDIS_MNEMONIC_INC: case ZYDIS_MNEMONIC_DEC: case ZYDIS_MNEMONIC_NEG:
        case ZYDIS_MNEMONIC_NOT: case ZYDIS_MNEMONIC_MOV: case ZYDIS_MNEMONIC_MOVZX:
        case ZYDIS_MNEMONIC_MOVSX: case ZYDIS_MNEMONIC_XCHG:
            return true;
        case ZYDIS_MNEMONIC_LEA:
            return visible == 2 && ops[1].type == ZYDIS_OPERAND_TYPE_MEMORY;
        case ZYDIS_MNEMONIC_SHL: case ZYDIS_MNEMONIC_SHR: case ZYDIS_MNEMONIC_SAR:
        case ZYDIS_MNEMONIC_ROL: case ZYDIS_MNEMONIC_ROR:
            // The count of the D0/D1 forms is an operand Zydis may not count
            // as visible; a CL count stays with the interpreter.
            return visible >= 1 && d.instruction.operand_count >= 2 &&
                   ops[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE;
        case ZYDIS_MNEMONIC_IMUL:
            return visible >= 1 && visible <= 3;
        case ZYDIS_MNEMONIC_PUSH:
        case ZYDIS_MNEMONIC_POP:
            return width32 && visible == 1;
        case ZYDIS_MNEMONIC_JMP:
        case ZYDIS_MNEMONIC_CALL:
            return width32 && d.instruction.meta.branch_type != ZYDIS_BRANCH_TYPE_FAR &&
                   visible == 1 && ops[0].type != ZYDIS_OPERAND_TYPE_POINTER;
        case ZYDIS_MNEMONIC_RET:
            return width32 && d.instruction.meta.branch_type != ZYDIS_BRANCH_TYPE_FAR;
        default:
            if (IsConditionalBranch(m))
            {
                return width32;
            }
            return IsSetcc(m) || IsCmovcc(m);
    }
}

}  // namespace

bool Covered(const decode::DecodedInstruction& d, const Features& features)
{
    const ZydisDecodedInstruction& in = d.instruction;
    if (!interp::FeatureEnabled(d, features) || in.address_width != 32 ||
        (in.attributes & ZYDIS_ATTRIB_HAS_LOCK) != 0 || HasRawPrefix(d, 0xF2) ||
        HasRawPrefix(d, 0xF3))
    {
        return false;
    }
    return OperandsSupported(d) && CoveredMnemonic(d);
}

bool ModesSupported(const CpuState& state)
{
    const SegmentRegister& cs = state.Seg(Segment::kCs);
    return cs.IsFlat() && cs.default_32bit && state.Seg(Segment::kSs).default_32bit;
}

bool FormBlock(const CpuState& state, const GuestMemory& memory, const Features& features,
               const FrontendOptions& options, Block* block)
{
    if (!ModesSupported(state))
    {
        return false;
    }
    static const decode::Decoder decoder(decode::Decoder::Mode::kLegacy32);
    *block = Block{};
    block->start_eip = state.eip;
    Builder b(block);
    CpuState probe = state;
    std::uint32_t eip = state.eip;
    std::uint32_t index = 0;

    const auto add_page = [&](const std::uint32_t page) {
        for (std::uint32_t i = 0; i < block->page_count; ++i)
        {
            if (block->pages[i] == page)
            {
                return true;
            }
        }
        if (block->page_count == 2)
        {
            return false;
        }
        block->pages[block->page_count] = page;
        block->generations[block->page_count] = memory.pages().Generation(page);
        ++block->page_count;
        return true;
    };

    while (true)
    {
        if (index == options.max_instructions ||
            (index > 0 && options.gates != nullptr && options.gates->MayContain(eip)))
        {
            b.Exit(ExitKind::kContinue, eip, index);
            break;
        }
        std::uint8_t bytes[interp::kFetchWindow] = {};
        bool stopped_at_limit = false;
        probe.eip = eip;
        const unsigned fetched = interp::Fetch(probe, memory, bytes, &stopped_at_limit);
        decode::DecodedInstruction d;
        if (fetched == 0 || !decoder.Decode(bytes, fetched, eip, &d) || !Covered(d, features))
        {
            if (index == 0)
            {
                return false;
            }
            b.Exit(ExitKind::kInterpret, eip, index);
            break;
        }
        // The pages of the instruction's bytes: a block keeps at most two.
        const std::uint32_t first_page = eip & ~(kGuestPageSize - 1u);
        const std::uint32_t last_page = (eip + d.Length() - 1u) & ~(kGuestPageSize - 1u);
        const std::uint32_t saved_page_count = block->page_count;
        if (!add_page(first_page) || !add_page(last_page))
        {
            block->page_count = saved_page_count;
            if (index == 0)
            {
                return false;
            }
            b.Exit(ExitKind::kContinue, eip, index);
            break;
        }
        Lowerer lowerer(b, d, eip, index);
        const bool ends = lowerer.Lower();
        ++index;
        eip += d.Length();
        if (ends)
        {
            break;
        }
    }
    block->instruction_count = index;
    return true;
}

}  // namespace rex86::translate::ir
