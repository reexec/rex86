#include "fpu/wide_float.h"

namespace rex86::fpu::detail
{

namespace
{

constexpr std::uint64_t kFractionHigh = 0x0000FFFFFFFFFFFFull;
constexpr std::int32_t kBias = 0x3FFF;  // binary128 and extended share it

struct Bits
{
    std::uint64_t high;
    std::uint64_t low;
};

Bits BitsOf(const float128_t value)
{
    union ui128_f128 u;
    u.f = value;
    return Bits{u.ui.v64, u.ui.v0};
}

// A normal binary128 as sign, biased exponent and a 128-bit significand
// with the integer bit at bit 127.
struct Parts
{
    bool sign = false;
    std::int32_t exponent = 0;
    std::uint64_t high = 0;
    std::uint64_t low = 0;
};

Parts PartsOf(const float128_t value)
{
    const Bits b = BitsOf(value);
    Parts p;
    p.sign = (b.high >> 63) != 0;
    p.exponent = static_cast<std::int32_t>((b.high >> 48) & 0x7FFFu);
    p.high = (1ull << 63) | ((b.high & kFractionHigh) << 15) | (b.low >> 49);
    p.low = b.low << 15;
    return p;
}

// The inverse of PartsOf for a significand with bit 127 set; the low 15
// bits are dropped (zero for every exact use).
float128_t Pack(const bool sign, const std::int32_t exponent, const std::uint64_t high,
                const std::uint64_t low)
{
    const std::uint64_t v64 = (static_cast<std::uint64_t>(sign) << 63) |
        (static_cast<std::uint64_t>(exponent) << 48) | ((high >> 15) & kFractionHigh);
    const std::uint64_t v0 = (high << 49) | (low >> 15);
    return F128(v64, v0);
}

unsigned LeadingZeros64(std::uint64_t value)
{
    unsigned n = 0;
    while ((value & (1ull << 63)) == 0)
    {
        value <<= 1;
        ++n;
    }
    return n;
}

Float80 RoundPack(const bool sign, const std::int32_t exponent, const std::uint64_t high,
                  const std::uint64_t low, const unsigned rounding_control,
                  std::uint16_t* raised)
{
    const SoftScope scope(rounding_control, 80);
    const extFloat80_t r = softfloat_roundPackToExtF80(sign, exponent, high, low, 80);
    *raised = scope.Raised();
    return FromSoft(r);
}

}  // namespace

float128_t F128(const std::uint64_t high, const std::uint64_t low)
{
    union ui128_f128 u;
    u.ui.v64 = high;
    u.ui.v0 = low;
    return u.f;
}

float128_t Pi()
{
    return F128(0x4000921FB54442D1ull, 0x8469898CC51701B8ull);
}

float128_t PiOver2()
{
    return F128(0x3FFF921FB54442D1ull, 0x8469898CC51701B8ull);
}

float128_t PiOver4()
{
    return F128(0x3FFE921FB54442D1ull, 0x8469898CC51701B8ull);
}

float128_t Ln2()
{
    return F128(0x3FFE62E42FEFA39Eull, 0xF35793C7673007E6ull);
}

float128_t Log2E()
{
    return F128(0x3FFF71547652B82Full, 0xE1777D0FFDA0D23Aull);
}

float128_t Sqrt2()
{
    return f128_sqrt(i32_to_f128(2));
}

bool IsZero(const float128_t value)
{
    const Bits b = BitsOf(value);
    return ((b.high & ~(1ull << 63)) | b.low) == 0;
}

bool IsNegative(const float128_t value)
{
    return (BitsOf(value).high >> 63) != 0;
}

float128_t Negate(const float128_t value)
{
    const Bits b = BitsOf(value);
    return F128(b.high ^ (1ull << 63), b.low);
}

float128_t Abs(const float128_t value)
{
    const Bits b = BitsOf(value);
    return F128(b.high & ~(1ull << 63), b.low);
}

float128_t Ldexp(const float128_t value, const std::int32_t n)
{
    if (IsZero(value))
    {
        return value;
    }
    const Bits b = BitsOf(value);
    const std::int32_t exponent = static_cast<std::int32_t>((b.high >> 48) & 0x7FFFu) + n;
    if (exponent <= 0)
    {
        return F128(b.high & (1ull << 63), 0);
    }
    return F128((b.high & ~(0x7FFFull << 48)) | (static_cast<std::uint64_t>(exponent) << 48),
                b.low);
}

float128_t FromInt(const std::int64_t value)
{
    return i64_to_f128(value);
}

float128_t FromU128(std::uint64_t high, std::uint64_t low, const std::int32_t scale,
                    const bool negative)
{
    if ((high | low) == 0)
    {
        return F128(negative ? (1ull << 63) : 0, 0);
    }
    const unsigned zeros = high != 0 ? LeadingZeros64(high) : 64 + LeadingZeros64(low);
    unsigned shift = zeros;
    if (shift >= 64)
    {
        high = low;
        low = 0;
        shift -= 64;
    }
    if (shift != 0)
    {
        high = (high << shift) | (low >> (64 - shift));
        low <<= shift;
    }
    // The integer's top bit was bit 127 - zeros: value = 1.f * 2^(127 - zeros).
    const std::int32_t exponent =
        kBias + 127 - static_cast<std::int32_t>(zeros) + scale;
    return Pack(negative, exponent, high, low);
}

NearestScope::NearestScope()
{
    softfloat_roundingMode = softfloat_round_near_even;
    softfloat_exceptionFlags = 0;
}

void FastTwoSum(const float128_t a, const float128_t b, float128_t* sum, float128_t* error)
{
    *sum = f128_add(a, b);
    const float128_t taken = f128_sub(*sum, a);
    *error = f128_sub(b, taken);
}

Normalized Normalize(const Float80& x)
{
    const Unpacked u = Unpack(x);
    Normalized n;
    n.sign = u.sign;
    n.exponent = u.exponent;
    n.m = Pack(false, kBias, u.significand, 0);
    return n;
}

std::uint64_t TopSignificand(const float128_t value)
{
    return PartsOf(value).high;
}

Float80 RoundToExtended(Status* status, const Wide& wide, const bool force_inexact)
{
    Parts p = PartsOf(wide.value);
    std::int32_t exponent = p.exponent + wide.scale;
    // The tail is signed by value; the significand is a magnitude.
    const int tail = p.sign ? -wide.tail : wide.tail;
    if (tail < 0)
    {
        // Just below: the 15 bits under binary128's precision become ones.
        if (p.low == 0)
        {
            --p.high;
        }
        --p.low;
        if ((p.high >> 63) == 0)
        {
            p.high = (p.high << 1) | (p.low >> 63);
            p.low <<= 1;
            --exponent;
        }
    }
    else if (tail > 0)
    {
        p.low |= 1;
    }

    const unsigned rc = status->RoundingControl();
    std::uint16_t raised = 0;
    Float80 value = RoundPack(p.sign, exponent, p.high, p.low, rc, &raised);
    std::int32_t used = 0;
    if ((raised & kOverflow) != 0 && status->Unmasked(kOverflow))
    {
        // SDM 8.4.4 / 4.9.1.4: the true result divided by 2^24576, rounded.
        used = -24576;
        value = RoundPack(p.sign, exponent + used, p.high, p.low, rc, &raised);
        raised = static_cast<std::uint16_t>((raised & kPrecision) | kOverflow);
    }
    else if (status->Unmasked(kUnderflow) && Classify(value) != Kind::kNormal &&
             (Classify(value) != Kind::kZero || (raised & kPrecision) != 0) &&
             Classify(value) != Kind::kInfinity)
    {
        // Unmasked underflow: the true result times 2^24576, rounded, when
        // it is tiny.
        std::uint16_t scaled_raised = 0;
        const Float80 scaled =
            RoundPack(p.sign, exponent + 24576, p.high, p.low, rc, &scaled_raised);
        if (scaled.Exponent() < 24576 + 1)
        {
            used = 24576;
            value = scaled;
            raised = static_cast<std::uint16_t>((scaled_raised & kPrecision) | kUnderflow);
        }
    }
    if ((raised & kPrecision) != 0)
    {
        std::uint16_t ignored = 0;
        const Float80 toward_zero =
            RoundPack(p.sign, exponent + used, p.high, p.low, 3, &ignored);
        status->round_up = MagnitudeGreater(value, toward_zero);
    }
    else if (force_inexact)
    {
        // Reported inexact, so a tiny result underflows too (measured on
        // exact denormal products of FYL2X/FYL2XP1, #25).
        raised |= kPrecision;
        status->round_up = false;
        if (Classify(value) == Kind::kDenormal)
        {
            raised |= kUnderflow;
            if (status->Unmasked(kUnderflow))
            {
                std::uint16_t ignored = 0;
                value = RoundPack(p.sign, exponent + 24576, p.high, p.low, rc, &ignored);
            }
        }
    }
    status->raised |= raised;
    return value;
}

}  // namespace rex86::fpu::detail
