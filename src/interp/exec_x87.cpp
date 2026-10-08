// The x87 instructions (#19): loads and stores, arithmetic, comparisons,
// FCMOVcc and the stack housekeeping; the control and environment
// instructions are in exec_x87_env.cpp. Every instruction reads its memory
// operand and writes its memory result before it touches the x87 state,
// so a memory fault leaves the x87 untouched (the integer state is
// restored by Step, design #17 decision 6). The numbers come from
// src/fpu/. See docs/design/20261008-i019-x87-increment-1.md.

#include "interp/exec.h"
#include "interp/flags.h"
#include "interp/x87_access.h"
#include "interp/x87_stack.h"

#include "fpu/x87_math.h"

namespace rex86::interp
{

namespace
{

using fpu::Float80;
using x87::kC0;
using x87::kC1;
using x87::kC2;
using x87::kC3;

// The ST(i) index of a register-form instruction: ModR/M.rm.
unsigned RegisterIndex(const decode::DecodedInstruction& d)
{
    return d.instruction.raw.modrm.rm;
}

using x87::Commit;
using x87::StackFault;

// A memory source operand (FLD, arithmetic, compare): fetched first, so a
// memory fault comes before anything else, and converted only after the
// stack checks, whose faults outrank the conversion's #IA/#D (SDM 4.9.2).
struct MemorySource
{
    std::uint8_t bytes[10] = {};
    unsigned size = 0;
    bool integer = false;
};

bool FetchMemorySource(Ctx* ctx, const ZydisDecodedOperand& operand,
                       const bool integer, MemorySource* source)
{
    source->size = operand.size / 8u;
    source->integer = integer;
    return x87::ReadOperandBytes(ctx, operand, 0, source->size, source->bytes);
}

std::uint64_t LittleEndian(const std::uint8_t* bytes, const unsigned size)
{
    std::uint64_t bits = 0;
    for (unsigned index = size; index-- > 0;)
    {
        bits = (bits << 8) | bytes[index];
    }
    return bits;
}

// FLD's conversion: a signaling NaN is quieted with #IA, a denormal
// raises #D -- and is loaded even with #D unmasked (measured, #19).
// Returns false when an unmasked #IA suppresses the load.
bool ConvertForLoad(const MemorySource& source, fpu::Status* status,
                    Float80* value)
{
    if (source.integer)
    {
        std::int64_t v = static_cast<std::int64_t>(LittleEndian(source.bytes, source.size));
        if (source.size == 2) v = static_cast<std::int16_t>(v);
        if (source.size == 4) v = static_cast<std::int32_t>(v);
        *value = fpu::FromInteger(v);
        return true;
    }
    fpu::Status conversion(
        static_cast<std::uint16_t>(status->control_word | fpu::kDenormalOperand));
    bool ok = true;
    switch (source.size)
    {
        case 4:
            ok = fpu::FromFloat32(&conversion,
                                  static_cast<std::uint32_t>(LittleEndian(source.bytes, 4)),
                                  value);
            break;
        case 8:
            ok = fpu::FromFloat64(&conversion, LittleEndian(source.bytes, 8), value);
            break;
        default:
            *value = fpu::FromBytes(source.bytes);
            break;
    }
    status->raised |= conversion.raised;
    return ok;
}

// An arithmetic or compare source: exact and silent; its #IA/#D are the
// operation's to raise in priority order.
Float80 ConvertForOperation(const MemorySource& source, bool* denormal)
{
    *denormal = false;
    if (source.integer)
    {
        std::int64_t v = static_cast<std::int64_t>(LittleEndian(source.bytes, source.size));
        if (source.size == 2) v = static_cast<std::int16_t>(v);
        if (source.size == 4) v = static_cast<std::int32_t>(v);
        return fpu::FromInteger(v);
    }
    if (source.size == 4)
    {
        return fpu::SourceFromFloat32(
            static_cast<std::uint32_t>(LittleEndian(source.bytes, 4)), denormal);
    }
    return fpu::SourceFromFloat64(LittleEndian(source.bytes, 8), denormal);
}

bool IsIntegerMnemonic(const ZydisMnemonic mnemonic)
{
    switch (mnemonic)
    {
        case ZYDIS_MNEMONIC_FIADD: case ZYDIS_MNEMONIC_FISUB:
        case ZYDIS_MNEMONIC_FISUBR: case ZYDIS_MNEMONIC_FIMUL:
        case ZYDIS_MNEMONIC_FIDIV: case ZYDIS_MNEMONIC_FIDIVR:
        case ZYDIS_MNEMONIC_FICOM: case ZYDIS_MNEMONIC_FICOMP:
        case ZYDIS_MNEMONIC_FILD:
            return true;
        default:
            return false;
    }
}

// --- arithmetic ----------------------------------------------------------

ExecStatus ExecArith(Ctx* ctx, const ZydisMnemonic mnemonic)
{
    X87State& x87 = ctx->state.x87;
    const decode::DecodedInstruction& d = ctx->decoded;
    fpu::Arithmetic op = fpu::Arithmetic::kAdd;
    bool reverse = false;
    bool pop = false;
    switch (mnemonic)
    {
        case ZYDIS_MNEMONIC_FADD: case ZYDIS_MNEMONIC_FIADD: break;
        case ZYDIS_MNEMONIC_FADDP: pop = true; break;
        case ZYDIS_MNEMONIC_FSUB: case ZYDIS_MNEMONIC_FISUB:
            op = fpu::Arithmetic::kSubtract; break;
        case ZYDIS_MNEMONIC_FSUBP: op = fpu::Arithmetic::kSubtract; pop = true; break;
        case ZYDIS_MNEMONIC_FSUBR: case ZYDIS_MNEMONIC_FISUBR:
            op = fpu::Arithmetic::kSubtract; reverse = true; break;
        case ZYDIS_MNEMONIC_FSUBRP:
            op = fpu::Arithmetic::kSubtract; reverse = true; pop = true; break;
        case ZYDIS_MNEMONIC_FMUL: case ZYDIS_MNEMONIC_FIMUL:
            op = fpu::Arithmetic::kMultiply; break;
        case ZYDIS_MNEMONIC_FMULP: op = fpu::Arithmetic::kMultiply; pop = true; break;
        case ZYDIS_MNEMONIC_FDIV: case ZYDIS_MNEMONIC_FIDIV:
            op = fpu::Arithmetic::kDivide; break;
        case ZYDIS_MNEMONIC_FDIVP: op = fpu::Arithmetic::kDivide; pop = true; break;
        case ZYDIS_MNEMONIC_FDIVR: case ZYDIS_MNEMONIC_FIDIVR:
            op = fpu::Arithmetic::kDivide; reverse = true; break;
        case ZYDIS_MNEMONIC_FDIVRP:
            op = fpu::Arithmetic::kDivide; reverse = true; pop = true; break;
        default: return ExecStatus::kUnimplemented;
    }

    fpu::Status status(x87.control_word);
    unsigned dest = 0;
    Float80 source;
    bool source_empty = false;
    MemorySource fetched;
    const ZydisDecodedOperand* mem = x87::MemoryOperand(d);
    if (mem != nullptr)
    {
        if (!FetchMemorySource(ctx, *mem, IsIntegerMnemonic(mnemonic), &fetched))
        {
            return ExecStatus::kFault;
        }
    }
    else
    {
        // D8: ST(0) = ST(0) op ST(i); DC and DE: ST(i) = ST(i) op ST(0).
        const unsigned i = RegisterIndex(d);
        const bool to_st0 = d.instruction.opcode == 0xD8;
        dest = to_st0 ? 0 : i;
        const unsigned src = to_st0 ? i : 0;
        source_empty = x87::IsEmpty(x87, src);
        source = x87::Read(x87, src);
    }
    if (x87::IsEmpty(x87, dest) || source_empty)
    {
        if (!StackFault(&x87, false))
        {
            return ExecStatus::kContinue;
        }
        x87::Write(&x87, dest, fpu::kIndefinite);
        if (pop) x87::Pop(&x87);
        return ExecStatus::kContinue;
    }
    bool source_denormal = false;
    if (mem != nullptr)
    {
        source = ConvertForOperation(fetched, &source_denormal);
    }
    const Float80 destination = x87::Read(x87, dest);
    Float80 result;
    const bool written = reverse
        ? fpu::Arith(&status, op, source, destination, &result,
                     fpu::OperandHints{source_denormal, false})
        : fpu::Arith(&status, op, destination, source, &result,
                     fpu::OperandHints{false, source_denormal});
    Commit(&x87, status, written);
    if (written)
    {
        x87::Write(&x87, dest, result);
        if (pop) x87::Pop(&x87);
    }
    return ExecStatus::kContinue;
}

// --- loads -----------------------------------------------------------------

// Pushes a value, or the indefinite on a masked stack overflow (the
// register below TOP is not empty).
void PushChecked(X87State* x87, const Float80& value, const fpu::Status& status)
{
    if (!x87::IsEmpty(*x87, 7))
    {
        if (StackFault(x87, true))
        {
            x87::Push(x87, fpu::kIndefinite);
        }
        return;
    }
    Commit(x87, status, true);
    x87::Push(x87, value);
}

ExecStatus ExecLoad(Ctx* ctx, const ZydisMnemonic mnemonic)
{
    X87State& x87 = ctx->state.x87;
    const decode::DecodedInstruction& d = ctx->decoded;
    fpu::Status status(x87.control_word);
    Float80 value;
    if (const ZydisDecodedOperand* mem = x87::MemoryOperand(d))
    {
        if (mnemonic == ZYDIS_MNEMONIC_FBLD)
        {
            std::uint8_t bcd[10] = {};
            if (!x87::ReadOperandBytes(ctx, *mem, 0, 10, bcd))
            {
                return ExecStatus::kFault;
            }
            value = fpu::FromPackedBcd(bcd);
        }
        else
        {
            MemorySource fetched;
            if (!FetchMemorySource(ctx, *mem, mnemonic == ZYDIS_MNEMONIC_FILD,
                                   &fetched))
            {
                return ExecStatus::kFault;
            }
            if (!x87::IsEmpty(x87, 7))
            {
                if (StackFault(&x87, true)) x87::Push(&x87, fpu::kIndefinite);
                return ExecStatus::kContinue;
            }
            if (!ConvertForLoad(fetched, &status, &value))
            {
                Commit(&x87, status, false);
                return ExecStatus::kContinue;
            }
        }
    }
    else
    {
        const unsigned i = RegisterIndex(d);
        if (x87::IsEmpty(x87, i))
        {
            // An empty source is reported ahead of a full stack.
            if (StackFault(&x87, false)) x87::Push(&x87, fpu::kIndefinite);
            return ExecStatus::kContinue;
        }
        value = x87::Read(x87, i);
    }
    PushChecked(&x87, value, status);
    return ExecStatus::kContinue;
}

ExecStatus ExecLoadConstant(Ctx* ctx, const fpu::Constant constant)
{
    X87State& x87 = ctx->state.x87;
    fpu::Status status(x87.control_word);
    const Float80 value = fpu::LoadConstant(&status, constant);
    // The constants report no rounding: C1 is 0 unless the stack overflows.
    status.round_up = false;
    PushChecked(&x87, value, status);
    return ExecStatus::kContinue;
}

// --- stores ----------------------------------------------------------------

ExecStatus ExecStore(Ctx* ctx, const ZydisMnemonic mnemonic)
{
    X87State& x87 = ctx->state.x87;
    const decode::DecodedInstruction& d = ctx->decoded;
    const bool pop = mnemonic == ZYDIS_MNEMONIC_FSTP ||
        mnemonic == ZYDIS_MNEMONIC_FSTPNCE || mnemonic == ZYDIS_MNEMONIC_FISTP ||
        mnemonic == ZYDIS_MNEMONIC_FBSTP;
    const bool empty = x87::IsEmpty(x87, 0);
    const Float80 value = x87::Read(x87, 0);
    fpu::Status status(x87.control_word);

    const ZydisDecodedOperand* mem = x87::MemoryOperand(d);
    if (mem == nullptr)
    {
        const unsigned i = RegisterIndex(d);
        // FSTPNCE (D9 D8+i) is FSTP with no check for an empty ST(0), as
        // Intel's XED names it: an empty ST(0) is popped without a fault
        // and without a copy (measured, #19).
        if (empty && mnemonic == ZYDIS_MNEMONIC_FSTPNCE)
        {
            x87::SetConditions(&x87, kC1, 0);
            x87::Pop(&x87);
            return ExecStatus::kContinue;
        }
        if (empty)
        {
            if (!StackFault(&x87, false)) return ExecStatus::kContinue;
            x87::Write(&x87, i, fpu::kIndefinite);
        }
        else
        {
            x87::SetConditions(&x87, kC1, 0);
            x87::Write(&x87, i, value);
        }
        if (pop) x87::Pop(&x87);
        return ExecStatus::kContinue;
    }

    // Masked stack underflow stores the destination format's indefinite.
    const bool masked_underflow =
        empty && (x87.control_word & fpu::kInvalid) != 0;
    if (empty && !masked_underflow)
    {
        StackFault(&x87, false);
        return ExecStatus::kContinue;
    }
    std::uint8_t bytes[10] = {};
    const unsigned size = mem->size / 8u;
    bool written = true;
    const bool integer = mnemonic == ZYDIS_MNEMONIC_FIST ||
        mnemonic == ZYDIS_MNEMONIC_FISTP;
    if (mnemonic == ZYDIS_MNEMONIC_FBSTP)
    {
        written = fpu::ToPackedBcd(&status, empty ? fpu::kIndefinite : value,
                                   bytes);
    }
    else if (integer)
    {
        std::int64_t v = 0;
        written = fpu::ToInteger(&status, empty ? fpu::kIndefinite : value,
                                 size * 8u, &v);
        for (unsigned index = 0; index < size; ++index)
        {
            bytes[index] = static_cast<std::uint8_t>(
                static_cast<std::uint64_t>(v) >> (8 * index));
        }
    }
    else if (size == 4)
    {
        std::uint32_t bits = 0;
        written = fpu::ToFloat32(&status, empty ? fpu::kIndefinite : value, &bits);
        for (unsigned index = 0; index < 4; ++index)
        {
            bytes[index] = static_cast<std::uint8_t>(bits >> (8 * index));
        }
    }
    else if (size == 8)
    {
        std::uint64_t bits = 0;
        written = fpu::ToFloat64(&status, empty ? fpu::kIndefinite : value, &bits);
        for (unsigned index = 0; index < 8; ++index)
        {
            bytes[index] = static_cast<std::uint8_t>(bits >> (8 * index));
        }
    }
    else
    {
        fpu::ToBytes(empty ? fpu::kIndefinite : value, bytes);
    }
    if (empty)
    {
        // The masked underflow's own flags replace the conversion's.
        status = fpu::Status(x87.control_word);
    }
    if (written && !x87::WriteOperandBytes(ctx, *mem, 0, size, bytes))
    {
        return ExecStatus::kFault;
    }
    if (empty)
    {
        StackFault(&x87, false);
    }
    else
    {
        Commit(&x87, status, written);
    }
    if (written && pop)
    {
        x87::Pop(&x87);
    }
    return ExecStatus::kContinue;
}

// --- unary operations on ST(0) -----------------------------------------------

template <typename Operation>
ExecStatus ExecUnary(Ctx* ctx, const Operation& operation)
{
    X87State& x87 = ctx->state.x87;
    if (x87::IsEmpty(x87, 0))
    {
        if (StackFault(&x87, false)) x87::Write(&x87, 0, fpu::kIndefinite);
        return ExecStatus::kContinue;
    }
    fpu::Status status(x87.control_word);
    Float80 result;
    const bool written = operation(&status, x87::Read(x87, 0), &result);
    Commit(&x87, status, written);
    if (written)
    {
        x87::Write(&x87, 0, result);
    }
    return ExecStatus::kContinue;
}

// --- comparisons -------------------------------------------------------------

std::uint16_t ConditionsFor(const fpu::Relation relation)
{
    switch (relation)
    {
        case fpu::Relation::kLess: return kC0;
        case fpu::Relation::kEqual: return kC3;
        case fpu::Relation::kGreater: return 0;
        case fpu::Relation::kUnordered:
        default: return kC0 | kC2 | kC3;
    }
}

ExecStatus ExecCompare(Ctx* ctx, const ZydisMnemonic mnemonic)
{
    X87State& x87 = ctx->state.x87;
    const decode::DecodedInstruction& d = ctx->decoded;
    bool quiet = false;
    unsigned pops = 0;
    bool to_eflags = false;
    switch (mnemonic)
    {
        case ZYDIS_MNEMONIC_FCOM: case ZYDIS_MNEMONIC_FICOM: break;
        case ZYDIS_MNEMONIC_FCOMP: case ZYDIS_MNEMONIC_FICOMP: pops = 1; break;
        case ZYDIS_MNEMONIC_FCOMPP: pops = 2; break;
        case ZYDIS_MNEMONIC_FUCOM: quiet = true; break;
        case ZYDIS_MNEMONIC_FUCOMP: quiet = true; pops = 1; break;
        case ZYDIS_MNEMONIC_FUCOMPP: quiet = true; pops = 2; break;
        case ZYDIS_MNEMONIC_FCOMI: to_eflags = true; break;
        case ZYDIS_MNEMONIC_FCOMIP: to_eflags = true; pops = 1; break;
        case ZYDIS_MNEMONIC_FUCOMI: to_eflags = true; quiet = true; break;
        case ZYDIS_MNEMONIC_FUCOMIP: to_eflags = true; quiet = true; pops = 1; break;
        case ZYDIS_MNEMONIC_FTST: break;
        default: return ExecStatus::kUnimplemented;
    }
    fpu::Status status(x87.control_word);
    Float80 other;
    bool other_empty = false;
    bool other_denormal = false;
    if (mnemonic == ZYDIS_MNEMONIC_FTST)
    {
        other = Float80{};
    }
    else if (const ZydisDecodedOperand* mem = x87::MemoryOperand(d))
    {
        MemorySource fetched;
        if (!FetchMemorySource(ctx, *mem, IsIntegerMnemonic(mnemonic), &fetched))
        {
            return ExecStatus::kFault;
        }
        other = ConvertForOperation(fetched, &other_denormal);
    }
    else
    {
        const unsigned i = mnemonic == ZYDIS_MNEMONIC_FCOMPP ||
                mnemonic == ZYDIS_MNEMONIC_FUCOMPP
            ? 1u
            : RegisterIndex(d);
        other_empty = x87::IsEmpty(x87, i);
        other = x87::Read(x87, i);
    }

    // A compare that faults withholds its pops when the exception is
    // unmasked. The condition codes follow the SDM's tables: "flags not
    // set if unmasked #IA" -- the AMD Zen 3 host sets them to unordered
    // anyway, a deviation the host fuzz counts apart. An unmasked stack
    // fault (#IS) and an unmasked #D still report the relation (measured,
    // design #19).
    fpu::Relation relation = fpu::Relation::kUnordered;
    bool completed = true;
    bool report = true;
    if (x87::IsEmpty(x87, 0) || other_empty)
    {
        completed = StackFault(&x87, false);
    }
    else
    {
        completed = fpu::Compare(&status, x87::Read(x87, 0), other, quiet,
                                 &relation,
                                 fpu::OperandHints{false, other_denormal});
        report = (status.raised & fpu::kInvalid) == 0 ||
            !status.Unmasked(fpu::kInvalid);
        if (!to_eflags)
        {
            // FCOMI and FUCOMI leave C1 alone.
            x87::SetConditions(&x87, kC1, 0);
        }
        x87::Raise(&x87, status.raised);
    }
    if (!report)
    {
        return ExecStatus::kContinue;
    }
    if (to_eflags)
    {
        CpuState& s = ctx->state;
        SetFlag(s, kEflagsZero, relation == fpu::Relation::kEqual ||
                                    relation == fpu::Relation::kUnordered);
        SetFlag(s, kEflagsParity, relation == fpu::Relation::kUnordered);
        SetFlag(s, kEflagsCarry, relation == fpu::Relation::kLess ||
                                     relation == fpu::Relation::kUnordered);
        SetFlag(s, kEflagsOverflow, false);
        SetFlag(s, kEflagsSign, false);
        SetFlag(s, kEflagsAdjust, false);
    }
    else
    {
        x87::SetConditions(&x87, kC0 | kC2 | kC3, ConditionsFor(relation));
    }
    if (!completed)
    {
        return ExecStatus::kContinue;
    }
    for (unsigned index = 0; index < pops; ++index)
    {
        x87::Pop(&x87);
    }
    return ExecStatus::kContinue;
}

ExecStatus ExecExamine(Ctx* ctx)
{
    X87State& x87 = ctx->state.x87;
    const Float80 value = x87::Read(x87, 0);
    std::uint16_t c = 0;
    if (x87::IsEmpty(x87, 0))
    {
        c = kC3 | kC0;
    }
    else
    {
        switch (fpu::Classify(value))
        {
            case fpu::Kind::kUnsupported: c = 0; break;
            case fpu::Kind::kQuietNaN:
            case fpu::Kind::kSignalingNaN: c = kC0; break;
            case fpu::Kind::kNormal: c = kC2; break;
            case fpu::Kind::kInfinity: c = kC2 | kC0; break;
            case fpu::Kind::kZero: c = kC3; break;
            case fpu::Kind::kDenormal: c = kC3 | kC2; break;
        }
    }
    if (value.Sign()) c |= kC1;
    x87::SetConditions(&x87, x87::kConditionMask, c);
    return ExecStatus::kContinue;
}

// --- two-register operations -------------------------------------------------

ExecStatus ExecExchange(Ctx* ctx)
{
    X87State& x87 = ctx->state.x87;
    const unsigned i = RegisterIndex(ctx->decoded);
    if (x87::IsEmpty(x87, 0) || x87::IsEmpty(x87, i))
    {
        if (!StackFault(&x87, false)) return ExecStatus::kContinue;
        if (x87::IsEmpty(x87, 0)) x87::Write(&x87, 0, fpu::kIndefinite);
        if (x87::IsEmpty(x87, i)) x87::Write(&x87, i, fpu::kIndefinite);
    }
    else
    {
        x87::SetConditions(&x87, kC1, 0);
    }
    const Float80 a = x87::Read(x87, 0);
    const Float80 b = x87::Read(x87, i);
    x87::Write(&x87, 0, b);
    x87::Write(&x87, i, a);
    return ExecStatus::kContinue;
}

bool ConditionalMoveHolds(const ZydisMnemonic mnemonic, const CpuState& s)
{
    const bool cf = GetFlag(s, kEflagsCarry);
    const bool zf = GetFlag(s, kEflagsZero);
    const bool pf = GetFlag(s, kEflagsParity);
    switch (mnemonic)
    {
        case ZYDIS_MNEMONIC_FCMOVB: return cf;
        case ZYDIS_MNEMONIC_FCMOVE: return zf;
        case ZYDIS_MNEMONIC_FCMOVBE: return cf || zf;
        case ZYDIS_MNEMONIC_FCMOVU: return pf;
        case ZYDIS_MNEMONIC_FCMOVNB: return !cf;
        case ZYDIS_MNEMONIC_FCMOVNE: return !zf;
        case ZYDIS_MNEMONIC_FCMOVNBE: return !cf && !zf;
        case ZYDIS_MNEMONIC_FCMOVNU:
        default: return !pf;
    }
}

ExecStatus ExecConditionalMove(Ctx* ctx, const ZydisMnemonic mnemonic)
{
    X87State& x87 = ctx->state.x87;
    const unsigned i = RegisterIndex(ctx->decoded);
    if (x87::IsEmpty(x87, 0) || x87::IsEmpty(x87, i))
    {
        // The masked response writes the indefinite whatever the
        // condition (measured, #19).
        if (StackFault(&x87, false))
        {
            x87::Write(&x87, 0, fpu::kIndefinite);
        }
        return ExecStatus::kContinue;
    }
    // FCMOVcc leaves C1 alone (measured, #19).
    if (ConditionalMoveHolds(mnemonic, ctx->state))
    {
        x87::Write(&x87, 0, x87::Read(x87, i));
    }
    return ExecStatus::kContinue;
}

// ST(0) op ST(1) -> ST(0): FSCALE and FPREM/FPREM1.
ExecStatus ExecScale(Ctx* ctx)
{
    X87State& x87 = ctx->state.x87;
    if (x87::IsEmpty(x87, 0) || x87::IsEmpty(x87, 1))
    {
        if (StackFault(&x87, false)) x87::Write(&x87, 0, fpu::kIndefinite);
        return ExecStatus::kContinue;
    }
    fpu::Status status(x87.control_word);
    Float80 result;
    const bool written =
        fpu::Scale(&status, x87::Read(x87, 0), x87::Read(x87, 1), &result);
    Commit(&x87, status, written);
    if (written) x87::Write(&x87, 0, result);
    return ExecStatus::kContinue;
}

ExecStatus ExecRemainder(Ctx* ctx, const bool ieee)
{
    X87State& x87 = ctx->state.x87;
    if (x87::IsEmpty(x87, 0) || x87::IsEmpty(x87, 1))
    {
        x87::SetConditions(&x87, kC2, 0);
        if (StackFault(&x87, false)) x87::Write(&x87, 0, fpu::kIndefinite);
        return ExecStatus::kContinue;
    }
    fpu::Status status(x87.control_word);
    Float80 result;
    bool complete = true;
    unsigned q = 0;
    const bool written = fpu::Remainder(&status, ieee, x87::Read(x87, 0),
                                        x87::Read(x87, 1), &result, &complete,
                                        &q);
    x87::Raise(&x87, status.raised);
    const fpu::Kind k0 = fpu::Classify(x87::Read(x87, 0));
    const fpu::Kind k1 = fpu::Classify(x87::Read(x87, 1));
    const bool numeric = k0 != fpu::Kind::kUnsupported &&
        k1 != fpu::Kind::kUnsupported && !fpu::IsNaN(k0) && !fpu::IsNaN(k1) &&
        (status.raised & fpu::kInvalid) == 0;
    if (!numeric || !written)
    {
        // A NaN or invalid operand, or an unmasked exception that stops
        // the reduction, clears C1 and C2 and leaves C0/C3 (measured, #19).
        x87::SetConditions(&x87, kC1 | kC2, 0);
    }
    else if (!complete)
    {
        // A partial reduction reports C2 alone (measured, #19).
        x87::SetConditions(&x87, x87::kConditionMask, kC2);
    }
    else
    {
        std::uint16_t c = 0;
        if ((q & 4u) != 0) c |= kC0;
        if ((q & 2u) != 0) c |= kC3;
        if ((q & 1u) != 0) c |= kC1;
        x87::SetConditions(&x87, x87::kConditionMask, c);
    }
    if (written)
    {
        x87::Write(&x87, 0, result);
    }
    return ExecStatus::kContinue;
}

ExecStatus ExecExtract(Ctx* ctx)
{
    X87State& x87 = ctx->state.x87;
    if (x87::IsEmpty(x87, 0))
    {
        // Underflow is reported ahead of a full stack, as for FLD.
        if (StackFault(&x87, false))
        {
            x87::Write(&x87, 0, fpu::kIndefinite);
            x87::Push(&x87, fpu::kIndefinite);
        }
        return ExecStatus::kContinue;
    }
    if (!x87::IsEmpty(x87, 7))
    {
        if (StackFault(&x87, true))
        {
            x87::Write(&x87, 0, fpu::kIndefinite);
            x87::Push(&x87, fpu::kIndefinite);
        }
        return ExecStatus::kContinue;
    }
    fpu::Status status(x87.control_word);
    Float80 exponent;
    Float80 significand;
    const bool written =
        fpu::Extract(&status, x87::Read(x87, 0), &exponent, &significand);
    Commit(&x87, status, written);
    if (written)
    {
        x87::Write(&x87, 0, exponent);
        x87::Push(&x87, significand);
    }
    return ExecStatus::kContinue;
}

bool IsControlMnemonic(const ZydisMnemonic mnemonic)
{
    switch (mnemonic)
    {
        case ZYDIS_MNEMONIC_FNINIT: case ZYDIS_MNEMONIC_FNCLEX:
        case ZYDIS_MNEMONIC_FLDCW: case ZYDIS_MNEMONIC_FNSTCW:
        case ZYDIS_MNEMONIC_FNSTSW: case ZYDIS_MNEMONIC_FNSTENV:
        case ZYDIS_MNEMONIC_FLDENV: case ZYDIS_MNEMONIC_FNSAVE:
        case ZYDIS_MNEMONIC_FRSTOR: case ZYDIS_MNEMONIC_FWAIT:
        case ZYDIS_MNEMONIC_FENI8087_NOP: case ZYDIS_MNEMONIC_FDISI8087_NOP:
        case ZYDIS_MNEMONIC_FSETPM287_NOP:
            return true;
        default:
            return false;
    }
}

// The non-waiting forms never check for a pending exception.
bool IsNonWaiting(const ZydisMnemonic mnemonic)
{
    switch (mnemonic)
    {
        case ZYDIS_MNEMONIC_FNINIT: case ZYDIS_MNEMONIC_FNCLEX:
        case ZYDIS_MNEMONIC_FNSTCW: case ZYDIS_MNEMONIC_FNSTSW:
        case ZYDIS_MNEMONIC_FNSTENV: case ZYDIS_MNEMONIC_FNSAVE:
        case ZYDIS_MNEMONIC_FENI8087_NOP: case ZYDIS_MNEMONIC_FDISI8087_NOP:
        case ZYDIS_MNEMONIC_FSETPM287_NOP:
            return true;
        default:
            return false;
    }
}

}  // namespace

ExecStatus ExecuteX87(Ctx* ctx)
{
    const decode::DecodedInstruction& d = ctx->decoded;
    const ZydisMnemonic mnemonic = d.instruction.mnemonic;
    const ZydisISASet isa = d.instruction.meta.isa_set;
    if (isa != ZYDIS_ISA_SET_X87 && isa != ZYDIS_ISA_SET_FCMOV &&
        isa != ZYDIS_ISA_SET_FCOMI && mnemonic != ZYDIS_MNEMONIC_FWAIT)
    {
        return ExecStatus::kUnimplemented;
    }
    CpuState& s = ctx->state;
    X87State& x87 = s.x87;

    // #MF: an unmasked exception is pending and this instruction waits
    // (design #19, decision 3). Nothing has changed yet.
    if (!IsNonWaiting(mnemonic) && (x87.status_word & x87::kErrorSummary) != 0)
    {
        ctx->Fault(FaultKind::kFloatingPoint,
                   s.Seg(Segment::kCs).base + s.eip, false);
        return ExecStatus::kFault;
    }
    if (mnemonic == ZYDIS_MNEMONIC_FWAIT)
    {
        return ExecStatus::kContinue;
    }
    if (IsControlMnemonic(mnemonic))
    {
        return ExecuteX87Control(ctx);
    }

    // The last-instruction and last-operand pointers (decision 2). Saved
    // first so a memory fault cannot leave them half-updated; restored on
    // a fault below.
    const std::uint32_t saved_ip = x87.last_instruction_pointer;
    const std::uint16_t saved_cs = x87.last_instruction_selector;
    const std::uint16_t saved_op = x87.last_opcode;
    const std::uint32_t saved_dp = x87.last_operand_pointer;
    const std::uint16_t saved_ds = x87.last_operand_selector;
    x87.last_instruction_pointer = s.eip;
    x87.last_instruction_selector = s.Seg(Segment::kCs).selector;
    x87.last_opcode = static_cast<std::uint16_t>(
        ((d.instruction.opcode & 7u) << 8) |
        (d.instruction.raw.modrm.mod << 6) | (d.instruction.raw.modrm.reg << 3) |
        d.instruction.raw.modrm.rm);
    if (const ZydisDecodedOperand* mem = x87::MemoryOperand(d))
    {
        x87.last_operand_pointer = EffectiveAddress(*ctx, *mem);
        x87.last_operand_selector = s.Seg(SegmentOf(d, *mem)).selector;
    }

    ExecStatus status = ExecStatus::kUnimplemented;
    switch (mnemonic)
    {
        case ZYDIS_MNEMONIC_FADD: case ZYDIS_MNEMONIC_FADDP:
        case ZYDIS_MNEMONIC_FIADD: case ZYDIS_MNEMONIC_FSUB:
        case ZYDIS_MNEMONIC_FSUBP: case ZYDIS_MNEMONIC_FISUB:
        case ZYDIS_MNEMONIC_FSUBR: case ZYDIS_MNEMONIC_FSUBRP:
        case ZYDIS_MNEMONIC_FISUBR: case ZYDIS_MNEMONIC_FMUL:
        case ZYDIS_MNEMONIC_FMULP: case ZYDIS_MNEMONIC_FIMUL:
        case ZYDIS_MNEMONIC_FDIV: case ZYDIS_MNEMONIC_FDIVP:
        case ZYDIS_MNEMONIC_FIDIV: case ZYDIS_MNEMONIC_FDIVR:
        case ZYDIS_MNEMONIC_FDIVRP: case ZYDIS_MNEMONIC_FIDIVR:
            status = ExecArith(ctx, mnemonic);
            break;
        case ZYDIS_MNEMONIC_FLD: case ZYDIS_MNEMONIC_FILD:
        case ZYDIS_MNEMONIC_FBLD:
            status = ExecLoad(ctx, mnemonic);
            break;
        case ZYDIS_MNEMONIC_FLDZ: status = ExecLoadConstant(ctx, fpu::Constant::kZero); break;
        case ZYDIS_MNEMONIC_FLD1: status = ExecLoadConstant(ctx, fpu::Constant::kOne); break;
        case ZYDIS_MNEMONIC_FLDPI: status = ExecLoadConstant(ctx, fpu::Constant::kPi); break;
        case ZYDIS_MNEMONIC_FLDL2T: status = ExecLoadConstant(ctx, fpu::Constant::kLog2Ten); break;
        case ZYDIS_MNEMONIC_FLDL2E: status = ExecLoadConstant(ctx, fpu::Constant::kLog2E); break;
        case ZYDIS_MNEMONIC_FLDLG2: status = ExecLoadConstant(ctx, fpu::Constant::kLog10Two); break;
        case ZYDIS_MNEMONIC_FLDLN2: status = ExecLoadConstant(ctx, fpu::Constant::kLnTwo); break;
        case ZYDIS_MNEMONIC_FST: case ZYDIS_MNEMONIC_FSTP:
        case ZYDIS_MNEMONIC_FSTPNCE: case ZYDIS_MNEMONIC_FIST:
        case ZYDIS_MNEMONIC_FISTP: case ZYDIS_MNEMONIC_FBSTP:
            status = ExecStore(ctx, mnemonic);
            break;
        case ZYDIS_MNEMONIC_FCOM: case ZYDIS_MNEMONIC_FCOMP:
        case ZYDIS_MNEMONIC_FCOMPP: case ZYDIS_MNEMONIC_FICOM:
        case ZYDIS_MNEMONIC_FICOMP: case ZYDIS_MNEMONIC_FUCOM:
        case ZYDIS_MNEMONIC_FUCOMP: case ZYDIS_MNEMONIC_FUCOMPP:
        case ZYDIS_MNEMONIC_FCOMI: case ZYDIS_MNEMONIC_FCOMIP:
        case ZYDIS_MNEMONIC_FUCOMI: case ZYDIS_MNEMONIC_FUCOMIP:
        case ZYDIS_MNEMONIC_FTST:
            status = ExecCompare(ctx, mnemonic);
            break;
        case ZYDIS_MNEMONIC_FXAM: status = ExecExamine(ctx); break;
        case ZYDIS_MNEMONIC_FXCH: status = ExecExchange(ctx); break;
        case ZYDIS_MNEMONIC_FCMOVB: case ZYDIS_MNEMONIC_FCMOVE:
        case ZYDIS_MNEMONIC_FCMOVBE: case ZYDIS_MNEMONIC_FCMOVU:
        case ZYDIS_MNEMONIC_FCMOVNB: case ZYDIS_MNEMONIC_FCMOVNE:
        case ZYDIS_MNEMONIC_FCMOVNBE: case ZYDIS_MNEMONIC_FCMOVNU:
            status = ExecConditionalMove(ctx, mnemonic);
            break;
        case ZYDIS_MNEMONIC_FCHS:
            status = ExecUnary(ctx, [](fpu::Status*, const Float80& a, Float80* r) {
                *r = a;
                r->sign_exponent ^= 0x8000u;
                return true;
            });
            break;
        case ZYDIS_MNEMONIC_FABS:
            status = ExecUnary(ctx, [](fpu::Status*, const Float80& a, Float80* r) {
                *r = a;
                r->sign_exponent &= 0x7FFFu;
                return true;
            });
            break;
        case ZYDIS_MNEMONIC_FSQRT: status = ExecUnary(ctx, fpu::SquareRoot); break;
        case ZYDIS_MNEMONIC_FRNDINT: status = ExecUnary(ctx, fpu::RoundToInteger); break;
        case ZYDIS_MNEMONIC_FSCALE: status = ExecScale(ctx); break;
        case ZYDIS_MNEMONIC_FPREM: status = ExecRemainder(ctx, false); break;
        case ZYDIS_MNEMONIC_FPREM1: status = ExecRemainder(ctx, true); break;
        case ZYDIS_MNEMONIC_FXTRACT: status = ExecExtract(ctx); break;
        case ZYDIS_MNEMONIC_FFREE:
        case ZYDIS_MNEMONIC_FFREEP:
            x87::SetTag(&x87, x87::Physical(x87, RegisterIndex(d)), x87::kTagEmpty);
            x87::SetConditions(&x87, kC1, 0);
            if (mnemonic == ZYDIS_MNEMONIC_FFREEP)
            {
                x87::Pop(&x87);
            }
            status = ExecStatus::kContinue;
            break;
        case ZYDIS_MNEMONIC_FINCSTP:
            x87::SetTop(&x87, x87::Top(x87) + 1u);
            x87::SetConditions(&x87, kC1, 0);
            status = ExecStatus::kContinue;
            break;
        case ZYDIS_MNEMONIC_FDECSTP:
            x87::SetTop(&x87, x87::Top(x87) - 1u);
            x87::SetConditions(&x87, kC1, 0);
            status = ExecStatus::kContinue;
            break;
        case ZYDIS_MNEMONIC_FNOP:
            status = ExecStatus::kContinue;
            break;
        case ZYDIS_MNEMONIC_FSIN: case ZYDIS_MNEMONIC_FCOS:
        case ZYDIS_MNEMONIC_FSINCOS: case ZYDIS_MNEMONIC_FPTAN:
        case ZYDIS_MNEMONIC_FPATAN: case ZYDIS_MNEMONIC_F2XM1:
        case ZYDIS_MNEMONIC_FYL2X: case ZYDIS_MNEMONIC_FYL2XP1:
            status = ExecuteX87Transcendental(ctx);
            break;
        default:
            status = ExecStatus::kUnimplemented;
            break;
    }
    if (status != ExecStatus::kContinue)
    {
        x87.last_instruction_pointer = saved_ip;
        x87.last_instruction_selector = saved_cs;
        x87.last_opcode = saved_op;
        x87.last_operand_pointer = saved_dp;
        x87.last_operand_selector = saved_ds;
    }
    return status;
}

}  // namespace rex86::interp
