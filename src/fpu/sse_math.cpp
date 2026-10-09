#include "fpu/sse_math.h"

extern "C" {
#include "platform.h"
#include "softfloat.h"
}

namespace rex86::fpu::sse
{

namespace
{

bool IsNaN(const std::uint32_t v)
{
    return (v & 0x7F800000u) == 0x7F800000u && (v & 0x007FFFFFu) != 0;
}

bool IsSignaling(const std::uint32_t v)
{
    return IsNaN(v) && (v & 0x00400000u) == 0;
}

bool IsDenormal(const std::uint32_t v)
{
    return (v & 0x7F800000u) == 0 && (v & 0x007FFFFFu) != 0;
}

bool IsZero(const std::uint32_t v)
{
    return (v & 0x7FFFFFFFu) == 0;
}

bool IsInfinity(const std::uint32_t v)
{
    return (v & 0x7FFFFFFFu) == 0x7F800000u;
}

bool Negative(const std::uint32_t v)
{
    return (v >> 31) != 0;
}

std::uint32_t Quiet(const std::uint32_t v)
{
    return v | 0x00400000u;
}

float32_t F(const std::uint32_t v)
{
    float32_t f;
    f.v = v;
    return f;
}

std::uint_fast8_t Rounding(const std::uint32_t mxcsr)
{
    switch ((mxcsr >> 13) & 3u)
    {
        case 0: return softfloat_round_near_even;
        case 1: return softfloat_round_min;
        case 2: return softfloat_round_max;
        default: return softfloat_round_minMag;
    }
}

// SoftFloat's globals for one SSE call: MXCSR's rounding and SSE's
// tininess before rounding (measured: 0x007FFFFF.8 rounding up to the
// smallest normal still raises #U).
void Begin(const std::uint32_t mxcsr)
{
    softfloat_roundingMode = Rounding(mxcsr);
    softfloat_detectTininess = softfloat_tininess_beforeRounding;
    softfloat_exceptionFlags = 0;
}

// The post-computation flags of a computed result, with flush-to-zero.
// Tiny means below 2^-126 before rounding: SoftFloat's underflow (raised
// only when inexact) or an exact denormal result.
void Finish(Lane* lane, const std::uint32_t mxcsr)
{
    const std::uint_fast8_t raised = softfloat_exceptionFlags;
    const bool inexact = (raised & softfloat_flag_inexact) != 0;
    const bool tiny = (raised & softfloat_flag_underflow) != 0 || IsDenormal(lane->value);
    if ((raised & softfloat_flag_overflow) != 0)
    {
        lane->post |= kOverflow;
    }
    if (tiny)
    {
        const bool unmasked = (mxcsr & (kUnderflow << kMaskShift)) == 0;
        if (unmasked)
        {
            lane->post |= kUnderflow;
        }
        else if ((mxcsr & kFlushToZero) != 0)
        {
            lane->value &= 0x80000000u;
            lane->post |= kUnderflow | kPrecision;
            return;
        }
        else if (inexact)
        {
            lane->post |= kUnderflow;
        }
    }
    if (inexact)
    {
        lane->post |= kPrecision;
    }
}

// The NaN rule shared by the arithmetic (SDM Vol. 1 table 4-7 for SSE):
// the first NaN operand, quieted; #I when either is signaling.
bool PropagateNaN(const std::uint32_t a, const std::uint32_t b, const bool binary, Lane* lane)
{
    const bool a_nan = binary && IsNaN(a);
    if (!a_nan && !IsNaN(b))
    {
        return false;
    }
    lane->value = Quiet(a_nan ? a : b);
    if ((binary && IsSignaling(a)) || IsSignaling(b))
    {
        lane->pre |= kInvalid;
    }
    return true;
}

// The denormal-operand flag: any denormal operand, NaNs absent (a NaN
// decides the lane first, measured).
void DenormalOperands(const std::uint32_t a, const std::uint32_t b, const bool binary, Lane* lane)
{
    if ((binary && IsDenormal(a)) || IsDenormal(b))
    {
        lane->pre |= kDenormal;
    }
}

Lane MinMax(const bool max, const std::uint32_t a, const std::uint32_t b)
{
    // Either NaN, or both zero: the second operand as it is (SDM MINPS).
    Lane lane;
    if (IsNaN(a) || IsNaN(b))
    {
        lane.value = b;
        lane.pre = kInvalid;
        return lane;
    }
    DenormalOperands(a, b, true, &lane);
    if (IsZero(a) && IsZero(b))
    {
        lane.value = b;
        return lane;
    }
    const bool a_less = f32_lt(F(a), F(b));
    const bool pick_a = max ? (!a_less && !f32_eq(F(a), F(b))) : a_less;
    lane.value = pick_a ? a : b;
    return lane;
}

// RCPPS/RSQRTPS (decision 6): a deterministic model, inside the SDM's
// 1.5 * 2^-12 bound, with the special values measured on a Zen 3:
// denormals read as zero, zero gives infinity, infinity zero, a negative
// RSQRT operand the default NaN, NaNs quieted, no flags at all.
Lane Approximate(const bool rsqrt, const std::uint32_t b)
{
    Lane lane;
    const std::uint32_t sign = b & 0x80000000u;
    if (IsNaN(b))
    {
        lane.value = Quiet(b);
        return lane;
    }
    if (IsZero(b) || IsDenormal(b))
    {
        lane.value = sign | 0x7F800000u;
        return lane;
    }
    if (rsqrt && Negative(b))
    {
        lane.value = kDefaultNaN;
        return lane;
    }
    if (IsInfinity(b))
    {
        lane.value = sign;
        return lane;
    }
    softfloat_roundingMode = softfloat_round_near_even;
    softfloat_exceptionFlags = 0;
    if (!rsqrt)
    {
        // A result below 2^-126 is flushed to zero (SDM RCPPS: tiny results
        // are always flushed); |b| > 2^126 exactly when it would be tiny.
        if ((b & 0x7FFFFFFFu) > 0x7E800000u)
        {
            lane.value = sign;
            return lane;
        }
        lane.value = f32_div(F(0x3F800000u), F(b)).v;
        return lane;
    }
    const float64_t one = {0x3FF0000000000000ull};
    lane.value = f64_to_f32(f64_div(one, f64_sqrt(f32_to_f64(F(b))))).v;
    return lane;
}

}  // namespace

Lane Arith(const Op op, const std::uint32_t a, const std::uint32_t b, const std::uint32_t mxcsr)
{
    switch (op)
    {
        case Op::kMin:
            return MinMax(false, a, b);
        case Op::kMax:
            return MinMax(true, a, b);
        case Op::kRcp:
            return Approximate(false, b);
        case Op::kRsqrt:
            return Approximate(true, b);
        default:
            break;
    }
    Lane lane;
    const bool binary = op != Op::kSqrt;
    if (PropagateNaN(a, b, binary, &lane))
    {
        return lane;
    }
    DenormalOperands(a, b, binary, &lane);
    Begin(mxcsr);
    switch (op)
    {
        case Op::kAdd: lane.value = f32_add(F(a), F(b)).v; break;
        case Op::kSub: lane.value = f32_sub(F(a), F(b)).v; break;
        case Op::kMul: lane.value = f32_mul(F(a), F(b)).v; break;
        case Op::kDiv: lane.value = f32_div(F(a), F(b)).v; break;
        default: lane.value = f32_sqrt(F(b)).v; break;
    }
    // Within a lane an invalid operation or a division by zero takes
    // precedence over the denormal operand (SDM Vol. 1 11.5.2, measured:
    // sqrt of a negative denormal and a denormal over zero raise no #D).
    const std::uint_fast8_t raised = softfloat_exceptionFlags;
    if ((raised & softfloat_flag_invalid) != 0)
    {
        lane.pre = kInvalid;
        lane.value = kDefaultNaN;
        return lane;
    }
    if ((raised & softfloat_flag_infinite) != 0)
    {
        lane.pre = kZeroDivide;
        return lane;
    }
    Finish(&lane, mxcsr);
    return lane;
}

Lane Compare(const unsigned predicate, const std::uint32_t a, const std::uint32_t b)
{
    Lane lane;
    const bool unordered = IsNaN(a) || IsNaN(b);
    const unsigned p = predicate & 7u;
    // LT, LE, NLT and NLE signal on quiet NaNs too; the others on SNaNs.
    const bool signals_quiet = p == 1 || p == 2 || p == 5 || p == 6;
    if (unordered)
    {
        if (IsSignaling(a) || IsSignaling(b) || signals_quiet)
        {
            lane.pre |= kInvalid;
        }
    }
    else
    {
        DenormalOperands(a, b, true, &lane);
    }
    bool result = false;
    if (unordered)
    {
        result = p == 3 || p == 4 || p == 5 || p == 6;
    }
    else
    {
        const bool eq = f32_eq(F(a), F(b));
        const bool lt = f32_lt(F(a), F(b));
        switch (p)
        {
            case 0: result = eq; break;
            case 1: result = lt; break;
            case 2: result = lt || eq; break;
            case 3: result = false; break;
            case 4: result = !eq; break;
            case 5: result = !lt; break;
            case 6: result = !(lt || eq); break;
            default: result = true; break;
        }
    }
    lane.value = result ? 0xFFFFFFFFu : 0u;
    return lane;
}

Lane OrderedCompare(const std::uint32_t a, const std::uint32_t b, const bool signal_quiet)
{
    Lane lane;
    if (IsNaN(a) || IsNaN(b))
    {
        if (signal_quiet || IsSignaling(a) || IsSignaling(b))
        {
            lane.pre |= kInvalid;
        }
        lane.value = 0x45;  // ZF PF CF
        return lane;
    }
    DenormalOperands(a, b, true, &lane);
    if (f32_eq(F(a), F(b)))
    {
        lane.value = 0x40;
    }
    else if (f32_lt(F(a), F(b)))
    {
        lane.value = 0x01;
    }
    return lane;
}

Lane ToInt32(const std::uint32_t a, const std::uint32_t mxcsr, const bool truncate)
{
    Lane lane;
    softfloat_exceptionFlags = 0;
    const std::uint_fast8_t mode =
        truncate ? static_cast<std::uint_fast8_t>(softfloat_round_minMag) : Rounding(mxcsr);
    const int_fast32_t value = f32_to_i32(F(a), mode, true);
    const std::uint_fast8_t raised = softfloat_exceptionFlags;
    if ((raised & softfloat_flag_invalid) != 0)
    {
        lane.value = kIntegerIndefinite;
        lane.pre |= kInvalid;
        return lane;
    }
    lane.value = static_cast<std::uint32_t>(value);
    if ((raised & softfloat_flag_inexact) != 0)
    {
        lane.post |= kPrecision;
    }
    return lane;
}

Lane FromInt32(const std::uint32_t value, const std::uint32_t mxcsr)
{
    Lane lane;
    Begin(mxcsr);
    lane.value = i32_to_f32(static_cast<std::int32_t>(value)).v;
    if ((softfloat_exceptionFlags & softfloat_flag_inexact) != 0)
    {
        lane.post |= kPrecision;
    }
    return lane;
}

}  // namespace rex86::fpu::sse
