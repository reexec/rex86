// Values carried past extended precision on their way to an x87 result:
// SoftFloat binary128 helpers, a separate power of two that keeps the
// exponent out of f128's range limits, and the one final rounding to the
// x87's extended format with its overflow/underflow responses and C1.
// Internal to src/fpu/ (it includes SoftFloat). See
// docs/design/20261008-i025-x87-transcendentals.md, decision 2.

#ifndef REX86_FPU_WIDE_FLOAT_H_
#define REX86_FPU_WIDE_FLOAT_H_

#include <cstdint>

#include "fpu/softfloat_bridge.h"

namespace rex86::fpu::detail
{

// A binary128 value from its two 64-bit halves (sign, 15-bit exponent,
// 112-bit fraction).
float128_t F128(std::uint64_t high, std::uint64_t low);

// The constants the transcendentals need, correctly rounded to binary128
// (Sqrt2 computed, under the caller's NearestScope).
float128_t Pi();
float128_t PiOver2();
float128_t PiOver4();
float128_t Ln2();
float128_t Log2E();
float128_t Sqrt2();

bool IsZero(float128_t value);
bool IsNegative(float128_t value);
float128_t Negate(float128_t value);
float128_t Abs(float128_t value);

// value * 2^n by exponent arithmetic. A result below the normal range
// flushes to a zero of the same sign: used for corrections whose
// contribution is then negligible. The caller keeps results that matter
// in range.
float128_t Ldexp(float128_t value, std::int32_t n);

float128_t FromInt(std::int64_t value);

// The 128-bit integer (high:low) times 2^scale, exact when it has at most
// 113 significant bits.
float128_t FromU128(std::uint64_t high, std::uint64_t low, std::int32_t scale,
                    bool negative = false);

// Every f128 kernel computes under round-to-nearest-even and ignores
// SoftFloat's flags; this sets that up for its lifetime.
class NearestScope
{
public:
    NearestScope();
};

// Error-free addition under round-to-nearest: a + b = *sum + *error
// exactly, for |a| >= |b| or a == 0.
void FastTwoSum(float128_t a, float128_t b, float128_t* sum, float128_t* error);

// x = m * 2^exponent, m in [1, 2), for a finite nonzero x (denormals and
// pseudo-denormals normalized). m is positive; the sign is separate.
struct Normalized
{
    bool sign = false;
    std::int32_t exponent = 0;
    float128_t m;
};

Normalized Normalize(const Float80& x);

// The top 64 bits of a normal binary128's significand, integer bit at
// bit 63: a Normalized m's exact extended-precision significand.
std::uint64_t TopSignificand(float128_t value);

// value * 2^scale; `tail` is the sign of what the f128 value leaves out
// (true - value * 2^scale) when the computation knows it, else 0.
struct Wide
{
    float128_t value;  // finite, normal, nonzero
    std::int32_t scale = 0;
    int tail = 0;
};

// The one rounding to extended precision under CW.RC; precision control
// never applies to the transcendentals (SDM Vol. 1, 8.1.5.2). Masked or
// bias-adjusted overflow and underflow as in #19, C1 for an inexact
// result. `force_inexact` reports #P with C1 = 0 when the rounding found
// the value exact: the computed paths of the transcendentals raise #P
// even then (measured, #25).
Float80 RoundToExtended(Status* status, const Wide& wide, bool force_inexact);

}  // namespace rex86::fpu::detail

#endif  // REX86_FPU_WIDE_FLOAT_H_
