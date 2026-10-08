#include "fpu/transcendental_kernels.h"

namespace rex86::fpu::detail
{

namespace
{

// Pi/2 = P * 2^-65 with P the 66-bit odd integer 0x3_243F6A88_85A308D3:
// the x87's internal Pi, 0.C90FDAA2 2168C234 C * 2^2 (SDM Vol. 1, 8.3.8),
// halved.
constexpr std::uint64_t kHalfPiHigh = 0x3;
constexpr std::uint64_t kHalfPiLow = 0x243F6A8885A308D3ull;

// Terms each series sums, enough that the first one left out stays
// below 2^-118 of the result over the kernel's domain.
constexpr int kSinCosTerms = 16;
constexpr int kAtanSmallTerms = 12;
constexpr int kAtanTerms = 24;
constexpr int kExpTerms = 36;
constexpr int kAtanhTerms = 30;

float128_t One()
{
    return i32_to_f128(1);
}

float128_t Div(const float128_t a, const std::int64_t b)
{
    return f128_div(a, FromInt(b));
}

int Sign(const float128_t value)
{
    if (IsZero(value)) return 0;
    return IsNegative(value) ? -1 : 1;
}

// lead + rest as a value and the sign of what it leaves out.
Series Combine(const float128_t lead, const float128_t rest)
{
    float128_t sum;
    float128_t error;
    FastTwoSum(lead, rest, &sum, &error);
    Series s;
    s.value = sum;
    s.tail = !IsZero(error) ? Sign(error) : Sign(rest);
    if (IsZero(rest)) s.tail = 0;
    return s;
}

// sin(theta) - theta.
float128_t SinRest(const float128_t theta)
{
    const float128_t t2 = f128_mul(theta, theta);
    float128_t h = One();
    for (int n = kSinCosTerms; n >= 2; --n)
    {
        h = f128_sub(One(), Div(f128_mul(t2, h), (2 * n) * (2 * n + 1)));
    }
    return Negate(Div(f128_mul(f128_mul(theta, t2), h), 6));
}

// cos(theta) - 1.
float128_t CosRest(const float128_t theta)
{
    const float128_t t2 = f128_mul(theta, theta);
    float128_t h = One();
    for (int n = kSinCosTerms; n >= 2; --n)
    {
        h = f128_sub(One(), Div(f128_mul(t2, h), (2 * n - 1) * (2 * n)));
    }
    return Negate(Div(f128_mul(t2, h), 2));
}

// atanh(u) / u - 1 = u^2/3 + u^4/5 + ...
float128_t AtanhRatioRest(const float128_t u2)
{
    float128_t acc = i32_to_f128(0);
    for (int n = kAtanhTerms; n >= 1; --n)
    {
        acc = f128_add(Div(One(), 2 * n + 1), f128_mul(u2, acc));
    }
    return f128_mul(u2, acc);
}

// log2(1 + x) through 2 * atanh(x / (2 + x)) * log2(e).
float128_t Log2ViaAtanh(const float128_t numerator, const float128_t denominator)
{
    const float128_t u = f128_div(numerator, denominator);
    const float128_t u2 = f128_mul(u, u);
    const float128_t atanh = f128_add(u, f128_mul(u, AtanhRatioRest(u2)));
    return f128_mul(Ldexp(atanh, 1), Log2E());
}

bool AtLeast(const std::uint64_t high, const std::uint64_t low, const std::uint64_t p_high,
             const std::uint64_t p_low)
{
    return high > p_high || (high == p_high && low >= p_low);
}

}  // namespace

Series NegateSeries(const Series& s)
{
    return Series{Negate(s.value), -s.tail};
}

Reduced ReduceByPi66(const Normalized& x)
{
    Reduced r;
    if (x.exponent < -1)
    {
        // |x| < 1/2 < Pi/4: nothing to reduce.
        r.theta = Ldexp(x.m, x.exponent);
        return r;
    }
    // |x| * 2^65 = M * 2^(E + 2), an integer of up to 128 bits.
    const std::uint64_t m = TopSignificand(x.m);
    const unsigned shift = static_cast<unsigned>(x.exponent + 2);  // 1..64
    const std::uint64_t x_high = shift == 64 ? m : m >> (64 - shift);
    const std::uint64_t x_low = shift == 64 ? 0 : m << shift;

    // Long division by P; the quotient fits in 64 bits (|x| < 2^63).
    std::uint64_t quotient = 0;
    std::uint64_t rem_high = 0;
    std::uint64_t rem_low = 0;
    for (int bit = 127; bit >= 0; --bit)
    {
        const std::uint64_t in =
            bit >= 64 ? (x_high >> (bit - 64)) & 1u : (x_low >> bit) & 1u;
        rem_high = (rem_high << 1) | (rem_low >> 63);
        rem_low = (rem_low << 1) | in;
        if (AtLeast(rem_high, rem_low, kHalfPiHigh, kHalfPiLow))
        {
            const std::uint64_t borrow = rem_low < kHalfPiLow ? 1u : 0u;
            rem_low -= kHalfPiLow;
            rem_high -= kHalfPiHigh + borrow;
            if (bit < 64) quotient |= 1ull << bit;
        }
    }
    // The nearest multiple: round up when 2 * rem > P (never equal: P is
    // odd and wider than any |x| significand).
    const std::uint64_t twice_high = (rem_high << 1) | (rem_low >> 63);
    const std::uint64_t twice_low = rem_low << 1;
    bool negative = false;
    std::uint64_t theta_high = rem_high;
    std::uint64_t theta_low = rem_low;
    if (!AtLeast(kHalfPiHigh, kHalfPiLow, twice_high, twice_low))
    {
        ++quotient;
        negative = true;
        const std::uint64_t borrow = kHalfPiLow < rem_low ? 1u : 0u;
        theta_low = kHalfPiLow - rem_low;
        theta_high = kHalfPiHigh - rem_high - borrow;
    }
    r.quadrant = static_cast<unsigned>(quotient & 3u);
    r.theta = FromU128(theta_high, theta_low, -65, negative);
    return r;
}

Series SinKernel(const float128_t theta)
{
    return Combine(theta, SinRest(theta));
}

Series CosKernel(const float128_t theta)
{
    return Combine(One(), CosRest(theta));
}

Series TanKernel(const float128_t theta)
{
    // tan = theta + (sin_rest - theta * cos_rest) / cos.
    const float128_t rs = SinRest(theta);
    const float128_t rc = CosRest(theta);
    const float128_t c = f128_add(One(), rc);
    const float128_t rest = f128_div(f128_sub(rs, f128_mul(theta, rc)), c);
    return Combine(theta, rest);
}

Series CotKernel(const float128_t theta)
{
    // cot = 1/theta + (theta * cos_rest - sin_rest) / (theta * sin), and
    // 1/theta = q + residual/theta exactly.
    const float128_t q = f128_div(One(), theta);
    const float128_t residual = f128_mulAdd(Negate(q), theta, One());
    const float128_t rs = SinRest(theta);
    const float128_t rc = CosRest(theta);
    const float128_t s = f128_add(theta, rs);
    const float128_t rest_cot =
        f128_div(f128_sub(f128_mul(theta, rc), rs), f128_mul(theta, s));
    const float128_t rest = f128_add(f128_div(residual, theta), rest_cot);
    return Combine(q, rest);
}

Series AtanKernel(const float128_t t, const float128_t residual)
{
    const float128_t t2 = f128_mul(t, t);
    // The residual's contribution: d atan = d t / (1 + t^2).
    const float128_t correction = f128_div(residual, f128_add(One(), t2));
    if (f128_lt(t, Ldexp(One(), -10)))
    {
        float128_t acc = i32_to_f128(0);
        for (int n = kAtanSmallTerms; n >= 1; --n)
        {
            const float128_t coefficient = Div(One(), 2 * n + 1);
            acc = f128_add((n & 1) != 0 ? Negate(coefficient) : coefficient,
                           f128_mul(t2, acc));
        }
        const float128_t rest = f128_add(f128_mul(f128_mul(t, t2), acc), correction);
        return Combine(t, rest);
    }
    // Three halvings, atan(u) = 2 atan(u / (1 + sqrt(1 + u^2))), bring t
    // under 0.1 for the series.
    float128_t u = t;
    for (int i = 0; i < 3; ++i)
    {
        const float128_t root = f128_sqrt(f128_add(One(), f128_mul(u, u)));
        u = f128_div(u, f128_add(One(), root));
    }
    const float128_t u2 = f128_mul(u, u);
    float128_t acc = i32_to_f128(0);
    for (int n = kAtanTerms; n >= 1; --n)
    {
        const float128_t coefficient = Div(One(), 2 * n + 1);
        acc = f128_add((n & 1) != 0 ? Negate(coefficient) : coefficient, f128_mul(u2, acc));
    }
    const float128_t atan_u = f128_add(u, f128_mul(f128_mul(u, u2), acc));
    Series s;
    s.value = f128_add(Ldexp(atan_u, 3), correction);
    return s;
}

float128_t Exp2Minus1Kernel(const float128_t x)
{
    const float128_t t = f128_mul(x, Ln2());
    float128_t h = One();
    for (int n = kExpTerms; n >= 2; --n)
    {
        h = f128_add(One(), Div(f128_mul(t, h), n));
    }
    return f128_mul(t, h);
}

float128_t Log2Kernel(float128_t m, std::int32_t e)
{
    if (f128_eq(m, One()))
    {
        return FromInt(e);
    }
    if (f128_lt(Sqrt2(), m))
    {
        m = Ldexp(m, -1);
        ++e;
    }
    const float128_t log2_m = Log2ViaAtanh(f128_sub(m, One()), f128_add(m, One()));
    return f128_add(FromInt(e), log2_m);
}

float128_t Log2OnePlusKernel(const float128_t x)
{
    return Log2ViaAtanh(x, f128_add(i32_to_f128(2), x));
}

}  // namespace rex86::fpu::detail
