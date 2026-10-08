// The x87 transcendentals (#25): FSIN, FCOS, FSINCOS, FPTAN, FPATAN,
// F2XM1, FYL2X and FYL2XP1 on the register stack. The numbers come from
// src/fpu/x87_transcendental.h; this file owns the stack faults, the
// pushes and pops, and C1/C2. C0 and C3 are left as they were, as is C2
// for the four that do not reduce an argument (measured on an Intel Kaby
// Lake). See docs/design/20261008-i025-x87-transcendentals.md.

#include "interp/exec.h"
#include "interp/x87_stack.h"

#include "fpu/x87_transcendental.h"

namespace rex86::interp
{

namespace
{

using fpu::Float80;
using fpu::TrigOutcome;
using x87::kC1;
using x87::kC2;

constexpr Float80 kOne{0x8000000000000000ull, 0x3FFF};

// FSIN and FCOS: ST(0) in place, C2 reporting an out-of-range argument.
template <typename Operation>
ExecStatus ExecTrigInPlace(Ctx* ctx, const Operation& operation)
{
    X87State& x87 = ctx->state.x87;
    if (x87::IsEmpty(x87, 0))
    {
        x87::SetConditions(&x87, kC2, 0);
        if (x87::StackFault(&x87, false)) x87::Write(&x87, 0, fpu::kIndefinite);
        return ExecStatus::kContinue;
    }
    fpu::Status status(x87.control_word);
    Float80 result;
    const TrigOutcome outcome = operation(&status, x87::Read(x87, 0), &result);
    const bool written = outcome == TrigOutcome::kWritten;
    x87::Commit(&x87, status, written);
    x87::SetConditions(&x87, kC2, outcome == TrigOutcome::kOutOfRange ? kC2 : 0);
    if (written)
    {
        x87::Write(&x87, 0, result);
    }
    return ExecStatus::kContinue;
}

// FSINCOS and FPTAN: ST(0) replaced and a second value pushed, FXTRACT's
// stack checks (an empty ST(0) ahead of a full stack).
ExecStatus ExecTrigPush(Ctx* ctx, const bool tangent)
{
    X87State& x87 = ctx->state.x87;
    const bool underflow = x87::IsEmpty(x87, 0);
    if (underflow || !x87::IsEmpty(x87, 7))
    {
        x87::SetConditions(&x87, kC2, 0);
        if (x87::StackFault(&x87, !underflow))
        {
            x87::Write(&x87, 0, fpu::kIndefinite);
            x87::Push(&x87, fpu::kIndefinite);
        }
        return ExecStatus::kContinue;
    }
    fpu::Status status(x87.control_word);
    Float80 first;
    Float80 pushed;
    TrigOutcome outcome;
    if (tangent)
    {
        outcome = fpu::Tangent(&status, x87::Read(x87, 0), &first);
        // A NaN result fills both registers; a number pushes 1.0.
        const fpu::Kind kind = fpu::Classify(first);
        pushed = outcome == TrigOutcome::kWritten && fpu::IsNaN(kind) ? first : kOne;
    }
    else
    {
        outcome = fpu::SineCosine(&status, x87::Read(x87, 0), &first, &pushed);
    }
    const bool written = outcome == TrigOutcome::kWritten;
    x87::Commit(&x87, status, written);
    x87::SetConditions(&x87, kC2, outcome == TrigOutcome::kOutOfRange ? kC2 : 0);
    if (written)
    {
        x87::Write(&x87, 0, first);
        x87::Push(&x87, pushed);
    }
    return ExecStatus::kContinue;
}

// F2XM1: ST(0) in place.
ExecStatus ExecExp2Minus1(Ctx* ctx)
{
    X87State& x87 = ctx->state.x87;
    if (x87::IsEmpty(x87, 0))
    {
        if (x87::StackFault(&x87, false)) x87::Write(&x87, 0, fpu::kIndefinite);
        return ExecStatus::kContinue;
    }
    fpu::Status status(x87.control_word);
    Float80 result;
    const bool written = fpu::Exp2Minus1(&status, x87::Read(x87, 0), &result);
    x87::Commit(&x87, status, written);
    if (written)
    {
        x87::Write(&x87, 0, result);
    }
    return ExecStatus::kContinue;
}

// FPATAN, FYL2X and FYL2XP1: ST(1) := f(ST(1), ST(0)), then pop. An
// unmasked exception that writes nothing pops nothing.
template <typename Operation>
ExecStatus ExecBinaryPop(Ctx* ctx, const Operation& operation)
{
    X87State& x87 = ctx->state.x87;
    if (x87::IsEmpty(x87, 0) || x87::IsEmpty(x87, 1))
    {
        if (x87::StackFault(&x87, false))
        {
            x87::Write(&x87, 1, fpu::kIndefinite);
            x87::Pop(&x87);
        }
        return ExecStatus::kContinue;
    }
    fpu::Status status(x87.control_word);
    Float80 result;
    const bool written = operation(&status, x87::Read(x87, 1), x87::Read(x87, 0), &result);
    x87::Commit(&x87, status, written);
    if (written)
    {
        x87::Write(&x87, 1, result);
        x87::Pop(&x87);
    }
    return ExecStatus::kContinue;
}

}  // namespace

ExecStatus ExecuteX87Transcendental(Ctx* ctx)
{
    switch (ctx->decoded.instruction.mnemonic)
    {
        case ZYDIS_MNEMONIC_FSIN: return ExecTrigInPlace(ctx, fpu::Sine);
        case ZYDIS_MNEMONIC_FCOS: return ExecTrigInPlace(ctx, fpu::Cosine);
        case ZYDIS_MNEMONIC_FSINCOS: return ExecTrigPush(ctx, false);
        case ZYDIS_MNEMONIC_FPTAN: return ExecTrigPush(ctx, true);
        case ZYDIS_MNEMONIC_F2XM1: return ExecExp2Minus1(ctx);
        case ZYDIS_MNEMONIC_FPATAN: return ExecBinaryPop(ctx, fpu::Arctangent);
        case ZYDIS_MNEMONIC_FYL2X: return ExecBinaryPop(ctx, fpu::YLog2X);
        case ZYDIS_MNEMONIC_FYL2XP1: return ExecBinaryPop(ctx, fpu::YLog2XPlus1);
        default: return ExecStatus::kUnimplemented;
    }
}

}  // namespace rex86::interp
