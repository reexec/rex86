#include "fpu/x87_math.h"

#include <utility>

// SoftFloat's headers expect platform.h first, as its own sources do.
extern "C" {
#include "platform.h"
#include "softfloat.h"
#include "internals.h"
}

namespace rex86::fpu
{

namespace
{

// SoftFloat's operand. A pseudo-denormal (exponent 0, J = 1) has the
// value of the same significand at exponent 1, and SoftFloat mishandles
// it as given (an addition loses the carry out of the significand), so it
// is passed in that canonical form.
extFloat80_t ToSoft(const Float80& value)
{
    extFloat80_t soft;
    soft.signif = value.significand;
    soft.signExp = value.sign_exponent;
    if (value.Exponent() == 0 && value.IntegerBit())
    {
        soft.signExp = static_cast<std::uint16_t>(soft.signExp | 1u);
    }
    return soft;
}

Float80 FromSoft(const extFloat80_t& soft)
{
    return Float80{soft.signif, soft.signExp};
}

std::uint_fast8_t SoftRounding(const unsigned rounding_control)
{
    switch (rounding_control)
    {
        case 1: return softfloat_round_min;
        case 2: return softfloat_round_max;
        case 3: return softfloat_round_minMag;
        case 0:
        default: return softfloat_round_near_even;
    }
}

// CW.PC: 00 single (24 bits), 10 double (53), 11 extended (64). The
// reserved 01 behaves as extended.
std::uint_fast8_t SoftPrecision(const std::uint16_t control_word)
{
    switch ((control_word >> 8) & 3u)
    {
        case 0: return 32;
        case 2: return 64;
        default: return 80;
    }
}

std::uint16_t FromSoftFlags(const std::uint_fast8_t flags)
{
    std::uint16_t raised = 0;
    if ((flags & softfloat_flag_invalid) != 0) raised |= kInvalid;
    if ((flags & softfloat_flag_infinite) != 0) raised |= kZeroDivide;
    if ((flags & softfloat_flag_overflow) != 0) raised |= kOverflow;
    if ((flags & softfloat_flag_underflow) != 0) raised |= kUnderflow;
    if ((flags & softfloat_flag_inexact) != 0) raised |= kPrecision;
    return raised;
}

// Sets SoftFloat's globals for one call and collects its flags.
class SoftScope
{
public:
    SoftScope(const unsigned rounding_control, const std::uint_fast8_t precision)
    {
        softfloat_roundingMode = SoftRounding(rounding_control);
        extF80_roundingPrecision = precision;
        softfloat_detectTininess = softfloat_tininess_afterRounding;
        softfloat_exceptionFlags = 0;
    }

    [[nodiscard]] std::uint16_t Raised() const
    {
        return FromSoftFlags(softfloat_exceptionFlags);
    }
};

// |a| > |b| for canonical values (no unnormals): the exponent then the
// significand order the magnitudes.
bool MagnitudeGreater(const Float80& a, const Float80& b)
{
    if (a.Exponent() != b.Exponent())
    {
        return a.Exponent() > b.Exponent();
    }
    return a.significand > b.significand;
}

// A finite nonzero value as an exact (unbiased exponent, significand with
// J set) pair; denormals and pseudo-denormals normalized.
struct Unpacked
{
    bool sign = false;
    std::int32_t exponent = 0;  // value = significand * 2^(exponent - 63)
    std::uint64_t significand = 0;
};

Unpacked Unpack(const Float80& value)
{
    Unpacked out;
    out.sign = value.Sign();
    std::int32_t exponent = value.Exponent();
    std::uint64_t significand = value.significand;
    if (exponent == 0)
    {
        exponent = 1;  // denormals and pseudo-denormals share emin
    }
    while ((significand >> 63) == 0)
    {
        significand <<= 1;
        --exponent;
    }
    out.exponent = exponent - kExponentBias;
    out.significand = significand;
    return out;
}

// Multiplies a finite value by 2^k exactly where the result stays normal.
// Below the normal range the result becomes the smallest denormal of the
// same sign: the scaled addends of a bias-adjusted overflow, where such an
// operand only contributes a sticky bit. Zeros stay zeros.
Float80 ScaleForAdjustedResult(const Float80& value, const std::int32_t k)
{
    const Kind kind = Classify(value);
    if (kind == Kind::kZero)
    {
        return value;
    }
    const Unpacked u = Unpack(value);
    const std::int32_t biased = u.exponent + k + kExponentBias;
    if (biased < 1)
    {
        return Float80{1, static_cast<std::uint16_t>(u.sign ? 0x8000u : 0u)};
    }
    return Float80{u.significand,
                   static_cast<std::uint16_t>((u.sign ? 0x8000u : 0u) |
                                              static_cast<std::uint32_t>(
                                                  biased))};
}

// The pre-computation checks every arithmetic operation shares: invalid
// operands (unsupported encodings, signaling NaNs) and denormals.
enum class Precheck : std::uint8_t
{
    kProceed,     // compute normally
    kNaNResult,   // a NaN operand decides the result; SoftFloat propagates
    kDone,        // *result already set (masked invalid response)
    kSuppressed,  // unmasked exception: nothing is written
};

Precheck CheckOperands(Status* status, const Float80& a, const Float80* b,
                       Float80* result, const OperandHints hints = {})
{
    const Kind ka = Classify(a);
    const Kind kb = b != nullptr ? Classify(*b) : Kind::kNormal;
    if (ka == Kind::kUnsupported || kb == Kind::kUnsupported)
    {
        status->raised |= kInvalid;
        if (status->Unmasked(kInvalid))
        {
            return Precheck::kSuppressed;
        }
        *result = kIndefinite;
        return Precheck::kDone;
    }
    if (IsNaN(ka) || IsNaN(kb))
    {
        if (ka == Kind::kSignalingNaN || kb == Kind::kSignalingNaN)
        {
            status->raised |= kInvalid;
            if (status->Unmasked(kInvalid))
            {
                return Precheck::kSuppressed;
            }
        }
        return Precheck::kNaNResult;
    }
    if (ka == Kind::kDenormal || kb == Kind::kDenormal || hints.a_denormal ||
        hints.b_denormal)
    {
        status->raised |= kDenormalOperand;
        if (status->Unmasked(kDenormalOperand))
        {
            return Precheck::kSuppressed;
        }
    }
    return Precheck::kProceed;
}

extFloat80_t SoftArith(const Arithmetic op, const extFloat80_t a,
                       const extFloat80_t b)
{
    switch (op)
    {
        case Arithmetic::kAdd: return extF80_add(a, b);
        case Arithmetic::kSubtract: return extF80_sub(a, b);
        case Arithmetic::kMultiply: return extF80_mul(a, b);
        case Arithmetic::kDivide:
        default: return extF80_div(a, b);
    }
}

// Runs `compute` (rounding control, precision) -> value under the rounding
// the caller asks for, returning the value and its flags.
template <typename Compute>
Float80 Run(const Compute& compute, const unsigned rounding_control,
            const std::uint_fast8_t precision, std::uint16_t* raised)
{
    const SoftScope scope(rounding_control, precision);
    const Float80 value = compute();
    *raised = scope.Raised();
    return value;
}

// The common tail of a rounded operation: masked or bias-adjusted
// overflow and underflow, then C1 for inexact results. `compute(k_a, k_b)`
// evaluates the operation on operands scaled by 2^k_a and 2^k_b.
template <typename Compute>
void Finish(Status* status, const Compute& compute,
            const std::int32_t overflow_scale_a,
            const std::int32_t overflow_scale_b,
            const std::int32_t underflow_scale_a,
            const std::int32_t underflow_scale_b,
            const std::uint_fast8_t precision, Float80* result)
{
    const unsigned rc = status->RoundingControl();
    std::uint16_t raised = 0;
    Float80 value = Run([&] { return compute(0, 0); }, rc, precision, &raised);
    std::int32_t used_a = 0;
    std::int32_t used_b = 0;

    if ((raised & kOverflow) != 0 && status->Unmasked(kOverflow))
    {
        // SDM 8.4.4 / 4.9.1.4: the true result divided by 2^24576, rounded.
        used_a = overflow_scale_a;
        used_b = overflow_scale_b;
        value = Run([&] { return compute(used_a, used_b); }, rc, precision,
                    &raised);
        raised = static_cast<std::uint16_t>((raised & kPrecision) | kOverflow);
    }
    else if (status->Unmasked(kUnderflow) && Classify(value) != Kind::kNormal &&
             (Classify(value) != Kind::kZero || (raised & kPrecision) != 0) &&
             Classify(value) != Kind::kInfinity && !IsNaN(Classify(value)))
    {
        // Unmasked underflow is signaled for any tiny result, exact or not:
        // the true result times 2^24576, rounded, when it is tiny.
        std::uint16_t scaled_raised = 0;
        const Float80 scaled =
            Run([&] { return compute(underflow_scale_a, underflow_scale_b); },
                rc, precision, &scaled_raised);
        if (scaled.Exponent() < 24576 + 1)
        {
            value = scaled;
            used_a = underflow_scale_a;
            used_b = underflow_scale_b;
            raised = static_cast<std::uint16_t>((scaled_raised & kPrecision) |
                                                kUnderflow);
        }
    }

    if ((raised & kPrecision) != 0)
    {
        std::uint16_t ignored = 0;
        const Float80 toward_zero =
            Run([&] { return compute(used_a, used_b); }, 3, precision,
                &ignored);
        status->round_up = MagnitudeGreater(value, toward_zero);
    }
    status->raised |= raised;
    *result = value;
}

}  // namespace

bool Arith(Status* status, const Arithmetic op, const Float80& a,
           const Float80& b, Float80* result, const OperandHints hints)
{
    // Invalid combinations and division by zero outrank a denormal
    // operand (SDM 4.9.2, priority 3 over 4).
    const Kind first_a = Classify(a);
    const Kind first_b = Classify(b);
    const bool plain = first_a != Kind::kUnsupported &&
        first_b != Kind::kUnsupported && !IsNaN(first_a) && !IsNaN(first_b);
    if (plain && op == Arithmetic::kDivide && first_b == Kind::kZero &&
        first_a != Kind::kZero && first_a != Kind::kInfinity)
    {
        status->raised |= kZeroDivide;
        if (status->Unmasked(kZeroDivide))
        {
            return false;
        }
        *result = Float80{0x8000000000000000ull,
                          static_cast<std::uint16_t>(
                              ((a.Sign() != b.Sign()) ? 0x8000u : 0u) |
                              kExponentMax)};
        return true;
    }
    switch (CheckOperands(status, a, &b, result, hints))
    {
        case Precheck::kSuppressed: return false;
        case Precheck::kDone: return true;
        case Precheck::kNaNResult:
        {
            const SoftScope scope(0, 80);
            *result = FromSoft(SoftArith(op, ToSoft(a), ToSoft(b)));
            return true;
        }
        case Precheck::kProceed: break;
    }

    // Invalid combinations (inf - inf, 0 * inf, 0/0, inf/inf) and division
    // by zero are pre-computation exceptions.
    const Kind ka = Classify(a);
    const Kind kb = Classify(b);
    const bool a_zero = ka == Kind::kZero;
    const bool b_zero = kb == Kind::kZero;
    const bool a_inf = ka == Kind::kInfinity;
    const bool b_inf = kb == Kind::kInfinity;
    bool invalid = false;
    switch (op)
    {
        case Arithmetic::kAdd:
            invalid = a_inf && b_inf && a.Sign() != b.Sign();
            break;
        case Arithmetic::kSubtract:
            invalid = a_inf && b_inf && a.Sign() == b.Sign();
            break;
        case Arithmetic::kMultiply:
            invalid = (a_zero && b_inf) || (a_inf && b_zero);
            break;
        case Arithmetic::kDivide:
            invalid = (a_zero && b_zero) || (a_inf && b_inf);
            break;
    }
    if (invalid)
    {
        status->raised |= kInvalid;
        if (status->Unmasked(kInvalid))
        {
            return false;
        }
        *result = kIndefinite;
        return true;
    }
    if (op == Arithmetic::kDivide && b_zero && !a_inf)
    {
        status->raised |= kZeroDivide;
        if (status->Unmasked(kZeroDivide))
        {
            return false;
        }
        *result = Float80{0x8000000000000000ull,
                          static_cast<std::uint16_t>(
                              ((a.Sign() != b.Sign()) ? 0x8000u : 0u) |
                              kExponentMax)};
        return true;
    }

    const std::uint_fast8_t precision = SoftPrecision(status->control_word);
    const auto compute = [&](const std::int32_t ka_scale,
                             const std::int32_t kb_scale) {
        const Float80 sa = ka_scale == 0 ? a : ScaleForAdjustedResult(a, ka_scale);
        const Float80 sb = kb_scale == 0 ? b : ScaleForAdjustedResult(b, kb_scale);
        return FromSoft(SoftArith(op, ToSoft(sa), ToSoft(sb)));
    };
    // Scalings that keep every intermediate exact (design #19, decision 3).
    switch (op)
    {
        case Arithmetic::kAdd:
        case Arithmetic::kSubtract:
            Finish(status, compute, -24576, -24576, 24576, 24576, precision,
                   result);
            break;
        case Arithmetic::kMultiply:
            Finish(status, compute, -12288, -12288, 12288, 12288, precision,
                   result);
            break;
        case Arithmetic::kDivide:
            Finish(status, compute, -12288, 12288, 12288, -12288, precision,
                   result);
            break;
    }
    return true;
}

bool SquareRoot(Status* status, const Float80& a, Float80* result)
{
    // A negative operand is invalid ahead of being denormal.
    const Kind first = Classify(a);
    if (a.Sign() && (first == Kind::kDenormal || first == Kind::kNormal ||
                     first == Kind::kInfinity))
    {
        status->raised |= kInvalid;
        if (status->Unmasked(kInvalid))
        {
            return false;
        }
        *result = kIndefinite;
        return true;
    }
    switch (CheckOperands(status, a, nullptr, result))
    {
        case Precheck::kSuppressed: return false;
        case Precheck::kDone: return true;
        case Precheck::kNaNResult:
        {
            const SoftScope scope(0, 80);
            *result = FromSoft(extF80_sqrt(ToSoft(a)));
            return true;
        }
        case Precheck::kProceed: break;
    }
    const Kind kind = Classify(a);
    if (a.Sign() && kind != Kind::kZero)
    {
        status->raised |= kInvalid;
        if (status->Unmasked(kInvalid))
        {
            return false;
        }
        *result = kIndefinite;
        return true;
    }
    const std::uint_fast8_t precision = SoftPrecision(status->control_word);
    Finish(
        status,
        [&](std::int32_t, std::int32_t) {
            return FromSoft(extF80_sqrt(ToSoft(a)));
        },
        0, 0, 0, 0, precision, result);
    return true;
}

bool RoundToInteger(Status* status, const Float80& a, Float80* result)
{
    switch (CheckOperands(status, a, nullptr, result))
    {
        case Precheck::kSuppressed: return false;
        case Precheck::kDone: return true;
        case Precheck::kNaNResult:
        {
            const SoftScope scope(0, 80);
            *result = FromSoft(extF80_roundToInt(ToSoft(a),
                                                 softfloat_round_near_even,
                                                 false));
            return true;
        }
        case Precheck::kProceed: break;
    }
    const unsigned rc = status->RoundingControl();
    std::uint16_t raised = 0;
    const Float80 value = Run(
        [&] {
            return FromSoft(
                extF80_roundToInt(ToSoft(a), SoftRounding(rc), true));
        },
        rc, 80, &raised);
    if ((raised & kPrecision) != 0)
    {
        status->round_up = MagnitudeGreater(value, a);
    }
    status->raised |= raised;
    *result = value;
    return true;
}

namespace
{

// Packs an exact finite value (sign, exponent, J-normalized significand,
// value = significand * 2^(exponent - 63)) as the x87 stores a result that
// may be tiny: denormalized when masked, bias-adjusted by 2^24576 with #U
// when underflow is unmasked, and rounded by CW.RC either way.
Float80 PackScaled(Status* status, const bool sign, const std::int32_t exponent,
                   const std::uint64_t significand, const std::uint64_t extra)
{
    const unsigned rc = status->RoundingControl();
    std::int32_t biased = exponent + kExponentBias;
    std::uint16_t raised = 0;
    const auto pack = [&](const std::int32_t e, const unsigned mode) {
        std::uint16_t r = 0;
        const Float80 v = Run(
            [&] {
                return FromSoft(softfloat_roundPackToExtF80(sign, e,
                                                            significand,
                                                            extra, 80));
            },
            mode, 80, &r);
        return std::pair<Float80, std::uint16_t>(v, r);
    };
    std::int32_t used = biased;
    // A result still out of range after the 2^24576 adjustment saturates
    // to a signed infinity (overflow, C1 = 1) or zero (underflow, C1 = 0)
    // whatever the rounding mode (measured on FSCALE, #19).
    const std::uint16_t sign_bit = sign ? 0x8000u : 0u;
    if (biased - 24576 >= 0x7FFF && status->Unmasked(kOverflow))
    {
        status->raised |= kOverflow | kPrecision;
        status->round_up = true;
        return Float80{0x8000000000000000ull,
                       static_cast<std::uint16_t>(sign_bit | kExponentMax)};
    }
    if (biased + 24576 < 1 && status->Unmasked(kUnderflow))
    {
        status->raised |= kUnderflow | kPrecision;
        status->round_up = false;
        return Float80{0, sign_bit};
    }
    if (biased >= 0x7FFF && status->Unmasked(kOverflow))
    {
        used = biased - 24576;
        auto [v, r] = pack(used, rc);
        raised = static_cast<std::uint16_t>(kOverflow | (r & kPrecision));
        if ((raised & kPrecision) != 0)
        {
            status->round_up = MagnitudeGreater(v, pack(used, 3).first);
        }
        status->raised |= raised;
        return v;
    }
    if (biased < 1 && status->Unmasked(kUnderflow))
    {
        used = biased + 24576;
        auto [v, r] = pack(used, rc);
        raised = static_cast<std::uint16_t>(kUnderflow | (r & kPrecision));
        if ((raised & kPrecision) != 0)
        {
            status->round_up = MagnitudeGreater(v, pack(used, 3).first);
        }
        status->raised |= raised;
        return v;
    }
    auto [v, r] = pack(used, rc);
    if ((r & kPrecision) != 0)
    {
        status->round_up = MagnitudeGreater(v, pack(used, 3).first);
    }
    status->raised |= r;
    return v;
}

// The operand returned as the result (x rem inf, FSCALE by 0, the SDM
// tables' "ST(0)"): in canonical form (a pseudo-denormal at exponent 1),
// and an exact denormal raises no underflow even with #U unmasked
// (measured on an Intel Cascade Lake, #22). The AMD Zen 3 raises #U with
// the bias-adjusted value instead; the x87 fuzz counts that apart.
Float80 PassThrough(Status* status, const Float80& value)
{
    Status quiet(static_cast<std::uint16_t>(status->control_word | kUnderflow));
    const Unpacked u = Unpack(value);
    const Float80 packed = PackScaled(&quiet, u.sign, u.exponent, u.significand, 0);
    status->raised |= quiet.raised;
    status->round_up = quiet.round_up;
    return packed;
}

}  // namespace

bool Scale(Status* status, const Float80& st0, const Float80& st1,
           Float80* result)
{
    switch (CheckOperands(status, st0, &st1, result))
    {
        case Precheck::kSuppressed: return false;
        case Precheck::kDone: return true;
        case Precheck::kNaNResult:
        {
            const SoftScope scope(0, 80);
            *result = FromSoft(extF80_add(ToSoft(st0), ToSoft(st1)));
            return true;
        }
        case Precheck::kProceed: break;
    }
    const Kind k0 = Classify(st0);
    const Kind k1 = Classify(st1);
    if (k1 == Kind::kInfinity)
    {
        // SDM's FSCALE table: 0 * 2^+inf and inf * 2^-inf are invalid.
        if ((!st1.Sign() && k0 == Kind::kZero) ||
            (st1.Sign() && k0 == Kind::kInfinity))
        {
            status->raised |= kInvalid;
            if (status->Unmasked(kInvalid))
            {
                return false;
            }
            *result = kIndefinite;
            return true;
        }
        if (k0 == Kind::kZero || k0 == Kind::kInfinity)
        {
            *result = st0;
            return true;
        }
        *result = st1.Sign()
            ? Float80{0, static_cast<std::uint16_t>(st0.sign_exponent & 0x8000u)}
            : Float80{0x8000000000000000ull,
                      static_cast<std::uint16_t>((st0.sign_exponent & 0x8000u) |
                                                 kExponentMax)};
        return true;
    }
    if (k0 == Kind::kZero || k0 == Kind::kInfinity)
    {
        *result = st0;
        return true;
    }
    // trunc(st1), clamped: beyond +/-2^16 the result is far outside the
    // exponent range either way.
    std::int32_t n = 0;
    if (k1 != Kind::kZero)
    {
        const Unpacked u1 = Unpack(st1);
        if (u1.exponent >= 16)
        {
            n = 1 << 16;
        }
        else if (u1.exponent >= 0)
        {
            n = static_cast<std::int32_t>(u1.significand >> (63 - u1.exponent));
        }
        if (u1.sign)
        {
            n = -n;
        }
    }
    if (n == 0)
    {
        *result = PassThrough(status, st0);
        return true;
    }
    const Unpacked u0 = Unpack(st0);
    *result = PackScaled(status, u0.sign, u0.exponent + n, u0.significand, 0);
    return true;
}

bool Extract(Status* status, const Float80& x, Float80* exponent,
             Float80* significand)
{
    Float80 nan;
    switch (CheckOperands(status, x, nullptr, &nan))
    {
        case Precheck::kSuppressed: return false;
        case Precheck::kDone:
            *exponent = nan;
            *significand = nan;
            return true;
        case Precheck::kNaNResult:
        {
            const SoftScope scope(0, 80);
            *significand = FromSoft(extF80_add(ToSoft(x), ToSoft(x)));
            *exponent = *significand;
            return true;
        }
        case Precheck::kProceed: break;
    }
    const Kind kind = Classify(x);
    if (kind == Kind::kZero)
    {
        status->raised |= kZeroDivide;
        if (status->Unmasked(kZeroDivide))
        {
            return false;
        }
        *exponent = Float80{0x8000000000000000ull, 0xFFFF};  // -inf
        *significand = x;
        return true;
    }
    if (kind == Kind::kInfinity)
    {
        *exponent = Float80{0x8000000000000000ull, kExponentMax};  // +inf
        *significand = x;
        return true;
    }
    const Unpacked u = Unpack(x);
    *exponent = FromInteger(u.exponent);
    *significand = Float80{u.significand,
                           static_cast<std::uint16_t>((u.sign ? 0x8000u : 0u) |
                                                      kExponentBias)};
    return true;
}

namespace
{

// A 128-bit unsigned value for the exact remainder.
struct U128
{
    std::uint64_t hi = 0;
    std::uint64_t lo = 0;
};

U128 ShiftLeft(const U128 v, const unsigned n)
{
    if (n == 0) return v;
    if (n >= 128) return {};
    if (n >= 64) return {v.lo << (n - 64), 0};
    return {(v.hi << n) | (v.lo >> (64 - n)), v.lo << n};
}

bool LessThan(const U128 a, const U128 b)
{
    return a.hi < b.hi || (a.hi == b.hi && a.lo < b.lo);
}

U128 Subtract(const U128 a, const U128 b)
{
    U128 r;
    r.lo = a.lo - b.lo;
    r.hi = a.hi - b.hi - (a.lo < b.lo ? 1u : 0u);
    return r;
}

// a / b and a % b by restoring division; b is nonzero.
void DivMod(const U128 a, const U128 b, U128* quotient, U128* remainder)
{
    U128 q;
    U128 r;
    for (int bit = 127; bit >= 0; --bit)
    {
        r = ShiftLeft(r, 1);
        const std::uint64_t in = bit >= 64 ? (a.hi >> (bit - 64)) & 1u
                                           : (a.lo >> bit) & 1u;
        r.lo |= in;
        if (!LessThan(r, b))
        {
            r = Subtract(r, b);
            if (bit >= 64) q.hi |= 1ull << (bit - 64);
            else q.lo |= 1ull << bit;
        }
    }
    *quotient = q;
    *remainder = r;
}

// The number of bits FPREM/FPREM1 reduce the exponent by when a
// reduction is partial (exponent difference D >= 64): the SDM leaves it
// "between 32 and 63"; the hardware uses 32 + D mod 32 (measured on 800
// cases, #19), so the remaining difference is a multiple of 32.
std::int32_t PartialReductionBits(const std::int32_t difference)
{
    return 32 + difference % 32;
}

}  // namespace

bool Remainder(Status* status, const bool ieee, const Float80& dividend,
               const Float80& divisor, Float80* result, bool* complete,
               unsigned* quotient_low)
{
    *complete = true;
    *quotient_low = 0;
    // inf rem x and x rem 0 are invalid ahead of a denormal operand.
    {
        const Kind ka = Classify(dividend);
        const Kind kb = Classify(divisor);
        if (ka != Kind::kUnsupported && kb != Kind::kUnsupported &&
            !IsNaN(ka) && !IsNaN(kb) &&
            (ka == Kind::kInfinity || kb == Kind::kZero))
        {
            status->raised |= kInvalid;
            if (status->Unmasked(kInvalid))
            {
                return false;
            }
            *result = kIndefinite;
            return true;
        }
    }
    switch (CheckOperands(status, dividend, &divisor, result))
    {
        case Precheck::kSuppressed: return false;
        case Precheck::kDone: return true;
        case Precheck::kNaNResult:
        {
            const SoftScope scope(0, 80);
            *result = FromSoft(extF80_rem(ToSoft(dividend), ToSoft(divisor)));
            return true;
        }
        case Precheck::kProceed: break;
    }
    const Kind ka = Classify(dividend);
    const Kind kb = Classify(divisor);
    if (ka == Kind::kInfinity || kb == Kind::kZero)
    {
        status->raised |= kInvalid;
        if (status->Unmasked(kInvalid))
        {
            return false;
        }
        *result = kIndefinite;
        return true;
    }
    if (ka == Kind::kZero)
    {
        *result = dividend;
        return true;
    }
    if (kb == Kind::kInfinity)
    {
        *result = PassThrough(status, dividend);
        return true;
    }

    const Unpacked a = Unpack(dividend);
    const Unpacked b = Unpack(divisor);
    std::int32_t divisor_exponent = b.exponent;
    std::int32_t difference = a.exponent - b.exponent;
    if (difference >= 64)
    {
        *complete = false;
        const std::int32_t bits = PartialReductionBits(difference);
        divisor_exponent += difference - bits;
        difference = bits;
    }

    // value(a) / value(b') = (a.sig * 2^(d + 1)) / (b.sig * 2).
    bool negate = false;
    U128 remainder;
    U128 quotient;
    if (difference < -1)
    {
        // |a| < |b'| / 2: the quotient is 0 for both forms and r = a,
        // expressed in the half-units the packing below uses.
        remainder = {0, a.significand};
        divisor_exponent = a.exponent + 1;
    }
    else
    {
        const U128 numerator =
            ShiftLeft(U128{0, a.significand},
                      static_cast<unsigned>(difference + 1));
        const U128 denominator = ShiftLeft(U128{0, b.significand}, 1);
        DivMod(numerator, denominator, &quotient, &remainder);
        // A partial reduction truncates for FPREM1 too (measured, #19).
        if (ieee && *complete)
        {
            const U128 twice = ShiftLeft(remainder, 1);
            const bool above = LessThan(denominator, twice);
            const bool tie = !LessThan(twice, denominator) && !above;
            if (above || (tie && (quotient.lo & 1u) != 0))
            {
                quotient.lo += 1;
                if (quotient.lo == 0) ++quotient.hi;
                remainder = Subtract(denominator, remainder);
                negate = true;
            }
        }
    }
    *quotient_low = static_cast<unsigned>(quotient.lo & 7u);

    const bool sign = a.sign != negate;
    if (remainder.hi == 0 && remainder.lo == 0)
    {
        *result = Float80{0, static_cast<std::uint16_t>(a.sign ? 0x8000u : 0u)};
        return true;
    }
    // remainder * 2^(divisor_exponent - 64): normalize into 64 bits. The
    // value is exact (a multiple of the divisor's unit), so no bit is lost.
    std::int32_t exponent = divisor_exponent - 64 + 127;
    U128 r = remainder;
    while ((r.hi >> 63) == 0)
    {
        r = ShiftLeft(r, 1);
        --exponent;
    }
    *result = PackScaled(status, sign, exponent, r.hi, r.lo);
    return true;
}

bool Compare(Status* status, const Float80& a, const Float80& b,
             const bool quiet, Relation* relation, const OperandHints hints)
{
    const Kind ka = Classify(a);
    const Kind kb = Classify(b);
    if (ka == Kind::kUnsupported || kb == Kind::kUnsupported ||
        ka == Kind::kSignalingNaN || kb == Kind::kSignalingNaN ||
        (!quiet && (IsNaN(ka) || IsNaN(kb))))
    {
        status->raised |= kInvalid;
        *relation = Relation::kUnordered;
        return !status->Unmasked(kInvalid);
    }
    if (IsNaN(ka) || IsNaN(kb))
    {
        *relation = Relation::kUnordered;
        return true;
    }
    bool completed = true;
    if (ka == Kind::kDenormal || kb == Kind::kDenormal || hints.a_denormal ||
        hints.b_denormal)
    {
        status->raised |= kDenormalOperand;
        completed = !status->Unmasked(kDenormalOperand);
    }
    const SoftScope scope(0, 80);
    const extFloat80_t sa = ToSoft(a);
    const extFloat80_t sb = ToSoft(b);
    if (extF80_eq(sa, sb))
    {
        *relation = Relation::kEqual;
    }
    else if (extF80_lt_quiet(sa, sb))
    {
        *relation = Relation::kLess;
    }
    else
    {
        *relation = Relation::kGreater;
    }
    return completed;
}

bool FromFloat32(Status* status, const std::uint32_t bits, Float80* result)
{
    const std::uint32_t exponent = (bits >> 23) & 0xFFu;
    const std::uint32_t fraction = bits & 0x7FFFFFu;
    if (exponent == 0xFF && fraction != 0 && (fraction & 0x400000u) == 0)
    {
        status->raised |= kInvalid;  // signaling NaN
        if (status->Unmasked(kInvalid))
        {
            return false;
        }
    }
    else if (exponent == 0 && fraction != 0)
    {
        status->raised |= kDenormalOperand;
        if (status->Unmasked(kDenormalOperand))
        {
            return false;
        }
    }
    const SoftScope scope(0, 80);
    float32_t f;
    f.v = bits;
    *result = FromSoft(f32_to_extF80(f));
    return true;
}

bool FromFloat64(Status* status, const std::uint64_t bits, Float80* result)
{
    const std::uint64_t exponent = (bits >> 52) & 0x7FFu;
    const std::uint64_t fraction = bits & 0xFFFFFFFFFFFFFull;
    if (exponent == 0x7FF && fraction != 0 &&
        (fraction & 0x8000000000000ull) == 0)
    {
        status->raised |= kInvalid;
        if (status->Unmasked(kInvalid))
        {
            return false;
        }
    }
    else if (exponent == 0 && fraction != 0)
    {
        status->raised |= kDenormalOperand;
        if (status->Unmasked(kDenormalOperand))
        {
            return false;
        }
    }
    const SoftScope scope(0, 80);
    float64_t f;
    f.v = bits;
    *result = FromSoft(f64_to_extF80(f));
    return true;
}

Float80 SourceFromFloat32(const std::uint32_t bits, bool* denormal)
{
    const std::uint32_t exponent = (bits >> 23) & 0xFFu;
    const std::uint32_t fraction = bits & 0x7FFFFFu;
    *denormal = exponent == 0 && fraction != 0;
    if (exponent == 0xFF && fraction != 0)
    {
        // Any NaN, signaling ones kept signaling: the fraction moves to the
        // top of the extended significand under J.
        return Float80{(1ull << 63) | (static_cast<std::uint64_t>(fraction) << 40),
                       static_cast<std::uint16_t>(((bits >> 31) << 15) | kExponentMax)};
    }
    const SoftScope scope(0, 80);
    float32_t f;
    f.v = bits;
    return FromSoft(f32_to_extF80(f));
}

Float80 SourceFromFloat64(const std::uint64_t bits, bool* denormal)
{
    const std::uint64_t exponent = (bits >> 52) & 0x7FFu;
    const std::uint64_t fraction = bits & 0xFFFFFFFFFFFFFull;
    *denormal = exponent == 0 && fraction != 0;
    if (exponent == 0x7FF && fraction != 0)
    {
        return Float80{(1ull << 63) | (fraction << 11),
                       static_cast<std::uint16_t>(((bits >> 63) << 15) | kExponentMax)};
    }
    const SoftScope scope(0, 80);
    float64_t f;
    f.v = bits;
    return FromSoft(f64_to_extF80(f));
}

Float80 FromInteger(const std::int64_t value)
{
    const SoftScope scope(0, 80);
    return FromSoft(i64_to_extF80(value));
}

namespace
{

// FST m32/m64: precision and range come from the destination format, the
// rounding from CW.RC. Unmasked overflow or underflow stores nothing.
template <typename Convert, typename Bits>
bool StoreReal(Status* status, const Float80& a, const Convert& convert,
               const std::uint_fast8_t format_precision,
               const std::int32_t min_normal_exponent, Bits indefinite,
               Bits* bits)
{
    const Kind kind = Classify(a);
    if (kind == Kind::kUnsupported || kind == Kind::kSignalingNaN)
    {
        status->raised |= kInvalid;
        if (status->Unmasked(kInvalid))
        {
            return false;
        }
        if (kind == Kind::kUnsupported)
        {
            *bits = indefinite;
            return true;
        }
    }
    // Stores raise no #D (SDM: FST, FIST and FBSTP list none).
    const unsigned rc = status->RoundingControl();
    std::uint16_t raised = 0;
    Bits value{};
    {
        const SoftScope scope(rc, 80);
        value = convert(ToSoft(a));
        raised = scope.Raised();
    }
    if (kind == Kind::kNormal || kind == Kind::kDenormal)
    {
        if ((raised & kOverflow) != 0 && status->Unmasked(kOverflow))
        {
            status->raised |= kOverflow;
            return false;
        }
        if (status->Unmasked(kUnderflow))
        {
            // Tiny after rounding to the format's precision with an
            // unbounded exponent; extF80's range stands in for unbounded.
            std::uint16_t ignored = 0;
            const Float80 rounded = Run(
                [&] {
                    return FromSoft(extF80_add(ToSoft(a), ToSoft(Float80{})));
                },
                rc, format_precision, &ignored);
            if (Classify(rounded) != Kind::kZero &&
                Unpack(rounded).exponent < min_normal_exponent)
            {
                status->raised |= kUnderflow;
                return false;
            }
        }
        if ((raised & kPrecision) != 0)
        {
            Bits toward_zero{};
            {
                const SoftScope scope(3, 80);
                toward_zero = convert(ToSoft(a));
            }
            // Same sign, so the encodings order the magnitudes.
            const Bits magnitude_mask =
                static_cast<Bits>(~(static_cast<Bits>(1)
                                    << (sizeof(Bits) * 8 - 1)));
            status->round_up =
                (value & magnitude_mask) > (toward_zero & magnitude_mask);
        }
    }
    status->raised |= raised;
    *bits = value;
    return true;
}

}  // namespace

bool ToFloat32(Status* status, const Float80& a, std::uint32_t* bits)
{
    return StoreReal(
        status, a, [](const extFloat80_t v) { return extF80_to_f32(v).v; },
        32, -126, std::uint32_t{0xFFC00000u}, bits);
}

bool ToFloat64(Status* status, const Float80& a, std::uint64_t* bits)
{
    return StoreReal(
        status, a, [](const extFloat80_t v) { return extF80_to_f64(v).v; },
        64, -1022, std::uint64_t{0xFFF8000000000000ull}, bits);
}

bool ToInteger(Status* status, const Float80& a, const unsigned width_bits,
               std::int64_t* value)
{
    const std::int64_t indefinite =
        width_bits == 64 ? INT64_MIN
                         : -(static_cast<std::int64_t>(1) << (width_bits - 1));
    const Kind kind = Classify(a);
    if (kind == Kind::kUnsupported || IsNaN(kind) || kind == Kind::kInfinity)
    {
        status->raised |= kInvalid;
        if (status->Unmasked(kInvalid))
        {
            return false;
        }
        *value = indefinite;
        return true;
    }
    const unsigned rc = status->RoundingControl();
    std::int64_t converted = 0;
    std::uint16_t raised = 0;
    {
        const SoftScope scope(rc, 80);
        converted = extF80_to_i64(ToSoft(a), SoftRounding(rc), true);
        raised = scope.Raised();
    }
    const std::int64_t low = indefinite;
    const std::int64_t high =
        width_bits == 64 ? INT64_MAX
                         : (static_cast<std::int64_t>(1) << (width_bits - 1)) - 1;
    if ((raised & kInvalid) != 0 || converted < low || converted > high)
    {
        status->raised |= kInvalid;
        if (status->Unmasked(kInvalid))
        {
            return false;
        }
        *value = indefinite;
        return true;
    }
    if ((raised & kPrecision) != 0)
    {
        std::int64_t toward_zero = 0;
        {
            const SoftScope scope(3, 80);
            toward_zero = extF80_to_i64(ToSoft(a), softfloat_round_minMag, false);
        }
        status->round_up = converted != toward_zero;
    }
    status->raised |= raised;
    *value = converted;
    return true;
}

Float80 FromPackedBcd(const std::uint8_t bcd[10])
{
    // 18 digits, two per byte, least significant first; the sign is bit 7
    // of byte 9. Digits above 9 are undefined in the SDM; they enter the
    // sum with their nibble value.
    std::int64_t magnitude = 0;
    for (int index = 8; index >= 0; --index)
    {
        magnitude = magnitude * 100 + (bcd[index] >> 4) * 10 + (bcd[index] & 0xFu);
    }
    Float80 value = FromInteger(magnitude);
    if ((bcd[9] & 0x80u) != 0)
    {
        value.sign_exponent |= 0x8000u;
    }
    return value;
}

bool ToPackedBcd(Status* status, const Float80& a, std::uint8_t bcd[10])
{
    static constexpr std::uint8_t kBcdIndefinite[10] = {0, 0, 0, 0, 0, 0, 0,
                                                       0xC0, 0xFF, 0xFF};
    const Kind kind = Classify(a);
    std::int64_t value = 0;
    bool invalid = kind == Kind::kUnsupported || IsNaN(kind) ||
        kind == Kind::kInfinity;
    if (!invalid)
    {
        const unsigned rc = status->RoundingControl();
        std::uint16_t raised = 0;
        {
            const SoftScope scope(rc, 80);
            value = extF80_to_i64(ToSoft(a), SoftRounding(rc), true);
            raised = scope.Raised();
        }
        static constexpr std::int64_t kLimit = 999999999999999999LL;
        if ((raised & kInvalid) != 0 || value > kLimit || value < -kLimit)
        {
            invalid = true;
        }
        else
        {
            if ((raised & kPrecision) != 0)
            {
                std::int64_t toward_zero = 0;
                {
                    const SoftScope scope(3, 80);
                    toward_zero =
                        extF80_to_i64(ToSoft(a), softfloat_round_minMag, false);
                }
                status->round_up = value != toward_zero;
            }
            status->raised |= raised;
        }
    }
    if (invalid)
    {
        status->raised |= kInvalid;
        if (status->Unmasked(kInvalid))
        {
            return false;
        }
        for (int index = 0; index < 10; ++index)
        {
            bcd[index] = kBcdIndefinite[index];
        }
        return true;
    }
    const bool negative = a.Sign();
    std::uint64_t magnitude = static_cast<std::uint64_t>(value < 0 ? -value : value);
    for (int index = 0; index < 9; ++index)
    {
        const unsigned low = static_cast<unsigned>(magnitude % 10);
        magnitude /= 10;
        const unsigned high = static_cast<unsigned>(magnitude % 10);
        magnitude /= 10;
        bcd[index] = static_cast<std::uint8_t>((high << 4) | low);
    }
    bcd[9] = negative ? 0x80u : 0x00u;
    return true;
}

Float80 LoadConstant(Status* status, const Constant constant)
{
    // 128-bit significands of the true values (computed at high precision),
    // rounded to 64 bits by CW.RC. Every constant's bits 64-65 are nonzero,
    // so this agrees with rounding the x87's internal 66-bit constants.
    struct Wide
    {
        std::int32_t exponent;
        std::uint64_t hi;
        std::uint64_t lo;
    };
    Wide wide{};
    switch (constant)
    {
        case Constant::kZero: return Float80{};
        case Constant::kOne: return Float80{0x8000000000000000ull, kExponentBias};
        case Constant::kPi:
            wide = {1, 0xC90FDAA22168C234ull, 0xC4C6628B80DC1CD1ull};
            break;
        case Constant::kLog2Ten:
            wide = {1, 0xD49A784BCD1B8AFEull, 0x492BF6FF4DAFDB4Cull};
            break;
        case Constant::kLog2E:
            wide = {0, 0xB8AA3B295C17F0BBull, 0xBE87FED0691D3E88ull};
            break;
        case Constant::kLog10Two:
            wide = {-2, 0x9A209A84FBCFF798ull, 0x8F8959AC0B7C9178ull};
            break;
        case Constant::kLnTwo:
            wide = {-1, 0xB17217F7D1CF79ABull, 0xC9E3B39803F2F6AFull};
            break;
    }
    Status quiet(static_cast<std::uint16_t>(status->control_word | kExceptionMask));
    const Float80 value = PackScaled(&quiet, false, wide.exponent, wide.hi, wide.lo);
    status->round_up = quiet.round_up;
    return value;
}

}  // namespace rex86::fpu
