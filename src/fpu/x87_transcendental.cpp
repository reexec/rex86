#include "fpu/x87_transcendental.h"

#include "fpu/softfloat_bridge.h"
#include "fpu/transcendental_kernels.h"
#include "fpu/wide_float.h"

namespace rex86::fpu
{

namespace
{

using detail::CheckOperands;
using detail::FromSoft;
using detail::Normalize;
using detail::Normalized;
using detail::NearestScope;
using detail::Precheck;
using detail::RoundToExtended;
using detail::Series;
using detail::SoftScope;
using detail::ToSoft;
using detail::Wide;

constexpr Float80 kOne{0x8000000000000000ull, 0x3FFF};
constexpr Float80 kMinusHalf{0x8000000000000000ull, 0xBFFE};

Float80 Signed(const Float80& magnitude, const bool negative)
{
    return Float80{magnitude.significand,
                   static_cast<std::uint16_t>((magnitude.sign_exponent & 0x7FFFu) |
                                              (negative ? 0x8000u : 0u))};
}

Float80 Zero(const bool negative)
{
    return Float80{0, static_cast<std::uint16_t>(negative ? 0x8000u : 0u)};
}

Float80 Infinity(const bool negative)
{
    return Float80{0x8000000000000000ull,
                   static_cast<std::uint16_t>(kExponentMax | (negative ? 0x8000u : 0u))};
}

// A NaN operand's result: SoftFloat propagates by the x87's rules.
Float80 NaNResult(const Float80& a)
{
    const SoftScope scope(0, 80);
    return FromSoft(extF80_sqrt(ToSoft(a)));
}

Float80 NaNResult(const Float80& a, const Float80& b)
{
    const SoftScope scope(0, 80);
    return FromSoft(extF80_add(ToSoft(a), ToSoft(b)));
}

// A masked or suppressed #IA with the indefinite as the masked response.
bool Invalid(Status* status, Float80* result)
{
    status->raised |= kInvalid;
    if (status->Unmasked(kInvalid))
    {
        return false;
    }
    *result = kIndefinite;
    return true;
}

// #D for a denormal operand; false when it is unmasked.
bool DenormalCheck(Status* status, const Float80& a, const Float80* b)
{
    if (Classify(a) == Kind::kDenormal || (b != nullptr && Classify(*b) == Kind::kDenormal))
    {
        status->raised |= kDenormalOperand;
        return !status->Unmasked(kDenormalOperand);
    }
    return true;
}

// The source returned as the result, reported inexact with C1 = 0: the
// tiny-argument shortcut of FSIN/FPTAN and the out-of-domain response of
// F2XM1 and FYL2XP1 (measured on an Intel Kaby Lake, #25). A denormal
// result underflows as any tiny inexact result does; a pseudo-denormal
// (exponent 0, J = 1) is not tiny and comes back in its canonical form.
Float80 InexactUnchanged(Status* status, const Float80& value)
{
    status->raised |= kPrecision;
    status->round_up = false;
    if (value.Exponent() == 0 && value.IntegerBit())
    {
        return Float80{value.significand, static_cast<std::uint16_t>(value.sign_exponent | 1u)};
    }
    if (Classify(value) == Kind::kDenormal)
    {
        status->raised |= kUnderflow;
        if (status->Unmasked(kUnderflow))
        {
            const detail::Unpacked u = detail::Unpack(value);
            return Float80{u.significand,
                           static_cast<std::uint16_t>(
                               (u.sign ? 0x8000u : 0u) |
                               static_cast<std::uint32_t>(u.exponent + 24576 + kExponentBias))};
        }
    }
    return value;
}

// An exact constant result reported inexact with C1 = 0 (F2XM1 at +-1).
Float80 InexactExact(Status* status, const Float80& value)
{
    status->raised |= kPrecision;
    status->round_up = false;
    return value;
}

Float80 Round(Status* status, const Series& s, const std::int32_t scale = 0)
{
    Wide w;
    w.value = s.value;
    w.scale = scale;
    w.tail = s.tail;
    return RoundToExtended(status, w, true);
}

Float80 RoundConstant(Status* status, const float128_t value, const bool negative)
{
    Series s;
    s.value = negative ? detail::Negate(value) : value;
    return Round(status, s);
}

// The unbiased exponent of a finite nonzero value.
std::int32_t ExponentOf(const Float80& x)
{
    return detail::Unpack(x).exponent;
}

enum class Trig : std::uint8_t
{
    kSin,
    kCos,
    kTan,
};

// sin, cos or tan of a finite x with 2^-68 <= |x| < 2^63.
Series TrigSeries(const Trig which, const Normalized& n)
{
    const detail::Reduced r = detail::ReduceByPi66(n);
    Series s;
    switch (which)
    {
        case Trig::kSin:
            switch (r.quadrant)
            {
                case 0: s = detail::SinKernel(r.theta); break;
                case 1: s = detail::CosKernel(r.theta); break;
                case 2: s = detail::NegateSeries(detail::SinKernel(r.theta)); break;
                default: s = detail::NegateSeries(detail::CosKernel(r.theta)); break;
            }
            return n.sign ? detail::NegateSeries(s) : s;
        case Trig::kCos:
            switch (r.quadrant)
            {
                case 0: return detail::CosKernel(r.theta);
                case 1: return detail::NegateSeries(detail::SinKernel(r.theta));
                case 2: return detail::NegateSeries(detail::CosKernel(r.theta));
                default: return detail::SinKernel(r.theta);
            }
        case Trig::kTan:
        default:
            s = (r.quadrant & 1u) == 0 ? detail::TanKernel(r.theta)
                                       : detail::NegateSeries(detail::CotKernel(r.theta));
            return n.sign ? detail::NegateSeries(s) : s;
    }
}

// The shared front of the trigonometric instructions. Returns true with
// *outcome set when the operand decided the result (`special` holds it,
// for a NaN or indefinite in every destination); false to compute.
bool TrigFront(Status* status, const Float80& x, TrigOutcome* outcome, Float80* special)
{
    const Kind k = Classify(x);
    if (k == Kind::kUnsupported || k == Kind::kInfinity)
    {
        *outcome = Invalid(status, special) ? TrigOutcome::kWritten : TrigOutcome::kSuppressed;
        return true;
    }
    if (IsNaN(k))
    {
        if (k == Kind::kSignalingNaN)
        {
            status->raised |= kInvalid;
            if (status->Unmasked(kInvalid))
            {
                *outcome = TrigOutcome::kSuppressed;
                return true;
            }
        }
        *special = NaNResult(x);
        *outcome = TrigOutcome::kWritten;
        return true;
    }
    if (!DenormalCheck(status, x, nullptr))
    {
        *outcome = TrigOutcome::kSuppressed;
        return true;
    }
    if (k != Kind::kZero && k != Kind::kDenormal && ExponentOf(x) >= 63)
    {
        *outcome = TrigOutcome::kOutOfRange;
        return true;
    }
    return false;
}

// Below 2^-68 the x87 returns the argument (sin, tan) or 1 (cos),
// inexact with C1 = 0, whatever CW.RC says (measured, #25).
constexpr std::int32_t kShortcutExponent = -68;

bool Shortcut(const Float80& x)
{
    return Classify(x) == Kind::kDenormal || ExponentOf(x) < kShortcutExponent;
}

}  // namespace

TrigOutcome Sine(Status* status, const Float80& x, Float80* sine)
{
    TrigOutcome outcome = TrigOutcome::kWritten;
    Float80 special;
    if (TrigFront(status, x, &outcome, &special))
    {
        if (outcome == TrigOutcome::kWritten) *sine = special;
        return outcome;
    }
    if (Classify(x) == Kind::kZero)
    {
        *sine = x;
        return TrigOutcome::kWritten;
    }
    if (Shortcut(x))
    {
        *sine = InexactUnchanged(status, x);
        return TrigOutcome::kWritten;
    }
    Series s;
    {
        const NearestScope near;
        s = TrigSeries(Trig::kSin, Normalize(x));
    }
    *sine = Round(status, s);
    return TrigOutcome::kWritten;
}

TrigOutcome Cosine(Status* status, const Float80& x, Float80* cosine)
{
    TrigOutcome outcome = TrigOutcome::kWritten;
    Float80 special;
    if (TrigFront(status, x, &outcome, &special))
    {
        if (outcome == TrigOutcome::kWritten) *cosine = special;
        return outcome;
    }
    if (Classify(x) == Kind::kZero)
    {
        *cosine = kOne;
        return TrigOutcome::kWritten;
    }
    if (Shortcut(x))
    {
        *cosine = InexactExact(status, kOne);
        return TrigOutcome::kWritten;
    }
    Series s;
    {
        const NearestScope near;
        s = TrigSeries(Trig::kCos, Normalize(x));
    }
    *cosine = Round(status, s);
    return TrigOutcome::kWritten;
}

TrigOutcome Tangent(Status* status, const Float80& x, Float80* tangent)
{
    TrigOutcome outcome = TrigOutcome::kWritten;
    Float80 special;
    if (TrigFront(status, x, &outcome, &special))
    {
        if (outcome == TrigOutcome::kWritten) *tangent = special;
        return outcome;
    }
    if (Classify(x) == Kind::kZero)
    {
        *tangent = x;
        return TrigOutcome::kWritten;
    }
    if (Shortcut(x))
    {
        *tangent = InexactUnchanged(status, x);
        return TrigOutcome::kWritten;
    }
    Series s;
    {
        const NearestScope near;
        s = TrigSeries(Trig::kTan, Normalize(x));
    }
    *tangent = Round(status, s);
    return TrigOutcome::kWritten;
}

TrigOutcome SineCosine(Status* status, const Float80& x, Float80* sine, Float80* cosine)
{
    TrigOutcome outcome = TrigOutcome::kWritten;
    Float80 special;
    if (TrigFront(status, x, &outcome, &special))
    {
        if (outcome == TrigOutcome::kWritten)
        {
            *sine = special;
            *cosine = special;
        }
        return outcome;
    }
    if (Classify(x) == Kind::kZero)
    {
        *sine = x;
        *cosine = kOne;
        return TrigOutcome::kWritten;
    }
    if (Shortcut(x))
    {
        *sine = InexactUnchanged(status, x);
        *cosine = InexactExact(status, kOne);
        return TrigOutcome::kWritten;
    }
    Series s;
    Series c;
    {
        const NearestScope near;
        const Normalized n = Normalize(x);
        s = TrigSeries(Trig::kSin, n);
        c = TrigSeries(Trig::kCos, n);
    }
    *sine = Round(status, s);
    *cosine = Round(status, c);
    return TrigOutcome::kWritten;
}

bool Arctangent(Status* status, const Float80& y, const Float80& x, Float80* result)
{
    switch (CheckOperands(status, x, &y, result))
    {
        case Precheck::kSuppressed: return false;
        case Precheck::kDone: return true;
        case Precheck::kNaNResult: *result = NaNResult(x, y); return true;
        case Precheck::kProceed: break;
    }
    const Kind ky = Classify(y);
    const Kind kx = Classify(x);
    const bool ny = y.Sign();
    const bool nx = x.Sign();
    // The SDM's table: zeros and infinities give 0, pi, pi/2, pi/4 and
    // 3pi/4 with the sign of y.
    if (ky == Kind::kZero)
    {
        *result = nx ? RoundConstant(status, detail::Pi(), ny) : Zero(ny);
        return true;
    }
    if (ky == Kind::kInfinity)
    {
        if (kx == Kind::kInfinity)
        {
            float128_t v = detail::PiOver4();
            if (nx)
            {
                const NearestScope near;
                v = f128_mul(v, detail::FromInt(3));
            }
            *result = RoundConstant(status, v, ny);
        }
        else
        {
            *result = RoundConstant(status, detail::PiOver2(), ny);
        }
        return true;
    }
    if (kx == Kind::kZero)
    {
        *result = RoundConstant(status, detail::PiOver2(), ny);
        return true;
    }
    if (kx == Kind::kInfinity)
    {
        *result = nx ? RoundConstant(status, detail::Pi(), ny) : Zero(ny);
        return true;
    }

    Series s;
    std::int32_t scale = 0;
    {
        const NearestScope near;
        const Normalized a = Normalize(y);
        const Normalized b = Normalize(x);
        const bool swap = a.exponent > b.exponent ||
            (a.exponent == b.exponent && f128_lt(b.m, a.m));
        const Normalized& num = swap ? b : a;
        const Normalized& den = swap ? a : b;
        // t = (q + residual / den.m) * 2^shift exactly, t <= 1.
        const float128_t q = f128_div(num.m, den.m);
        const float128_t residual =
            f128_div(f128_mulAdd(detail::Negate(q), den.m, num.m), den.m);
        const std::int32_t shift = num.exponent - den.exponent;
        if (shift < -60 && !swap && !nx)
        {
            // atan(t) = t - t^3/3 + ...: q at scale `shift`, the cube
            // correction far below it (flushed when out of range).
            const float128_t q3 = detail::Ldexp(f128_mul(f128_mul(q, q), q), 2 * shift);
            const float128_t rest =
                f128_sub(residual, f128_div(q3, detail::FromInt(3)));
            float128_t sum;
            float128_t error;
            detail::FastTwoSum(q, rest, &sum, &error);
            s.value = sum;
            // What the sum leaves out; with the cube flushed and an exact
            // quotient, still -t^3/3 < 0.
            if (!detail::IsZero(error))
            {
                s.tail = detail::IsNegative(error) ? -1 : 1;
            }
            else if (!detail::IsZero(rest))
            {
                s.tail = detail::IsNegative(rest) ? -1 : 1;
            }
            else
            {
                s.tail = -1;
            }
            scale = shift;
        }
        else
        {
            const float128_t t = detail::Ldexp(q, shift);
            const float128_t tr = detail::Ldexp(residual, shift);
            if (!swap && !nx)
            {
                // atan(t) itself: keep the kernel's tail.
                s = detail::AtanKernel(t, tr);
            }
            else
            {
                float128_t base =
                    detail::IsZero(t) ? i32_to_f128(0) : detail::AtanKernel(t, tr).value;
                if (swap)
                {
                    base = f128_sub(detail::PiOver2(), base);
                }
                if (nx)
                {
                    base = f128_sub(detail::Pi(), base);
                }
                s.value = base;
            }
        }
        if (ny)
        {
            s = detail::NegateSeries(s);
        }
    }
    *result = Round(status, s, scale);
    return true;
}

bool Exp2Minus1(Status* status, const Float80& x, Float80* result)
{
    switch (CheckOperands(status, x, nullptr, result, {}, false))
    {
        case Precheck::kSuppressed: return false;
        case Precheck::kDone: return true;
        case Precheck::kNaNResult: *result = NaNResult(x); return true;
        case Precheck::kProceed: break;
    }
    const Kind k = Classify(x);
    if (k == Kind::kZero)
    {
        *result = x;
        return true;
    }
    if (k == Kind::kInfinity)
    {
        // 2^+inf - 1 and 2^-inf - 1, exact (measured, #25).
        *result = x.Sign() ? Signed(kOne, true) : x;
        return true;
    }
    if (!DenormalCheck(status, x, nullptr))
    {
        return false;
    }
    const std::int32_t e = ExponentOf(x);
    if (e >= 0 && !(e == 0 && x.significand == 0x8000000000000000ull))
    {
        // |x| > 1: the source unchanged (measured, #25).
        *result = InexactUnchanged(status, x);
        return true;
    }
    if (e == 0)
    {
        *result = InexactExact(status, x.Sign() ? kMinusHalf : kOne);
        return true;
    }
    Series s;
    std::int32_t scale = 0;
    {
        const NearestScope near;
        const Normalized n = Normalize(x);
        if (n.exponent < -60)
        {
            // 2^x - 1 = x ln2 (1 + x ln2 / 2 + ...): t = m ln2 at scale e.
            const float128_t t = f128_mul(n.m, detail::Ln2());
            const float128_t half = detail::Ldexp(t, n.exponent - 1);
            const float128_t factor = n.sign ? f128_sub(i32_to_f128(1), half)
                                             : f128_add(i32_to_f128(1), half);
            s.value = f128_mul(t, factor);
            if (n.sign) s.value = detail::Negate(s.value);
            scale = n.exponent;
        }
        else
        {
            float128_t xf = detail::Ldexp(n.m, n.exponent);
            if (n.sign) xf = detail::Negate(xf);
            s.value = detail::Exp2Minus1Kernel(xf);
        }
    }
    *result = Round(status, s, scale);
    return true;
}

bool YLog2X(Status* status, const Float80& y, const Float80& x, Float80* result)
{
    switch (CheckOperands(status, x, &y, result, {}, false))
    {
        case Precheck::kSuppressed: return false;
        case Precheck::kDone: return true;
        case Precheck::kNaNResult: *result = NaNResult(x, y); return true;
        case Precheck::kProceed: break;
    }
    const Kind ky = Classify(y);
    const Kind kx = Classify(x);
    const bool ny = y.Sign();
    const bool x_zero = kx == Kind::kZero;
    const bool x_one = x == kOne;
    if (x.Sign() && !x_zero)
    {
        return Invalid(status, result);
    }
    if ((x_zero && ky == Kind::kZero) || (kx == Kind::kInfinity && ky == Kind::kZero) ||
        (x_one && ky == Kind::kInfinity))
    {
        return Invalid(status, result);
    }
    if (x_zero && ky != Kind::kInfinity)
    {
        status->raised |= kZeroDivide;
        if (status->Unmasked(kZeroDivide))
        {
            return false;
        }
        *result = Infinity(!ny);
        return true;
    }
    if (!DenormalCheck(status, x, &y))
    {
        return false;
    }
    if (x_zero)
    {
        *result = Infinity(!ny);  // y infinite: log2(0) = -inf
        return true;
    }
    if (kx == Kind::kInfinity)
    {
        *result = Infinity(ny);
        return true;
    }
    if (x_one)
    {
        *result = Zero(ny);
        return true;
    }
    // log2(x) < 0 for x < 1.
    const bool below_one = ExponentOf(x) < 0;
    if (ky == Kind::kZero)
    {
        *result = Zero(ny != below_one);
        return true;
    }
    if (ky == Kind::kInfinity)
    {
        *result = Infinity(ny != below_one);
        return true;
    }
    Series s;
    std::int32_t scale = 0;
    {
        const NearestScope near;
        const Normalized nx_ = Normalize(x);
        const Normalized ny_ = Normalize(y);
        const float128_t l = detail::Log2Kernel(nx_.m, nx_.exponent);
        s.value = f128_mul(ny_.m, l);
        if (ny_.sign) s.value = detail::Negate(s.value);
        scale = ny_.exponent;
    }
    *result = Round(status, s, scale);
    return true;
}

bool YLog2XPlus1(Status* status, const Float80& y, const Float80& x, Float80* result)
{
    switch (CheckOperands(status, x, &y, result, {}, false))
    {
        case Precheck::kSuppressed: return false;
        case Precheck::kDone: return true;
        case Precheck::kNaNResult: *result = NaNResult(x, y); return true;
        case Precheck::kProceed: break;
    }
    const Kind ky = Classify(y);
    const Kind kx = Classify(x);
    const bool ny = y.Sign();
    const bool nx = x.Sign();
    if (kx == Kind::kInfinity && nx)
    {
        return Invalid(status, result);
    }
    if ((kx == Kind::kZero && ky == Kind::kInfinity) ||
        (kx == Kind::kInfinity && ky == Kind::kZero))
    {
        return Invalid(status, result);
    }
    if (!DenormalCheck(status, x, &y))
    {
        return false;
    }
    if (kx == Kind::kZero)
    {
        *result = Zero(ny != nx);
        return true;
    }
    if (kx == Kind::kInfinity)
    {
        *result = Infinity(ny);
        return true;
    }
    // A zero or infinite y keeps its kind, its sign flipped for x < 0
    // (x <= -1 included, measured, #25).
    if (ky == Kind::kZero)
    {
        *result = Zero(ny != nx);
        return true;
    }
    if (ky == Kind::kInfinity)
    {
        *result = Infinity(ny != nx);
        return true;
    }
    if (nx && ExponentOf(x) >= 0)
    {
        // x <= -1: the source unchanged (measured, #25).
        *result = InexactUnchanged(status, x);
        return true;
    }
    Series s;
    std::int32_t scale = 0;
    {
        const NearestScope near;
        const Normalized n = Normalize(x);
        const Normalized ny_ = Normalize(y);
        float128_t l;
        std::int32_t l_scale = 0;
        if (n.exponent < -60)
        {
            // log2(1 + x) = x log2(e) (1 - x/2 + ...) at scale e.
            const float128_t half = detail::Ldexp(n.m, n.exponent - 1);
            const float128_t factor = n.sign ? f128_add(i32_to_f128(1), half)
                                             : f128_sub(i32_to_f128(1), half);
            l = f128_mul(f128_mul(n.m, detail::Log2E()), factor);
            if (n.sign) l = detail::Negate(l);
            l_scale = n.exponent;
        }
        else
        {
            float128_t xf = detail::Ldexp(n.m, n.exponent);
            if (n.sign) xf = detail::Negate(xf);
            if (n.exponent <= -2)
            {
                l = detail::Log2OnePlusKernel(xf);
            }
            else
            {
                const float128_t w = f128_add(i32_to_f128(1), xf);
                // w = m_w * 2^e_w for Log2Kernel.
                union ui128_f128 u;
                u.f = w;
                const std::int32_t e_w =
                    static_cast<std::int32_t>((u.ui.v64 >> 48) & 0x7FFFu) - kExponentBias;
                l = detail::Log2Kernel(detail::Ldexp(w, -e_w), e_w);
            }
        }
        s.value = f128_mul(ny_.m, l);
        if (ny_.sign) s.value = detail::Negate(s.value);
        scale = ny_.exponent + l_scale;
    }
    *result = Round(status, s, scale);
    return true;
}

}  // namespace rex86::fpu
