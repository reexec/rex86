// The binary128 kernels behind the x87 transcendentals: the reduction by
// the x87's 66-bit Pi and the power series on reduced arguments. Each
// kernel runs under round-to-nearest (the caller's NearestScope) and
// returns a value with the sign of its known error, when the result is a
// short leading term plus a small rest. Internal to src/fpu/. See
// docs/design/20261008-i025-x87-transcendentals.md, decisions 1 and 2.

#ifndef REX86_FPU_TRANSCENDENTAL_KERNELS_H_
#define REX86_FPU_TRANSCENDENTAL_KERNELS_H_

#include <cstdint>

#include "fpu/wide_float.h"

namespace rex86::fpu::detail
{

// A kernel result: `value`, and the sign of (true - value) when known.
struct Series
{
    float128_t value;
    int tail = 0;
};

Series NegateSeries(const Series& s);

// |x| = k * (Pi/2) + theta with k the nearest integer, Pi the x87's 66-bit
// pi (SDM Vol. 1, 8.3.8: 0.C90FDAA2 2168C234 C * 2^2), theta exact. For a
// finite nonzero |x| < 2^63.
struct Reduced
{
    unsigned quadrant = 0;  // k mod 4
    float128_t theta;       // signed, |theta| <= Pi/4
};

Reduced ReduceByPi66(const Normalized& x);

// sin, cos, tan and cot of an exact |theta| <= ~0.8 (theta != 0 for cot).
Series SinKernel(float128_t theta);
Series CosKernel(float128_t theta);
Series TanKernel(float128_t theta);
Series CotKernel(float128_t theta);

// atan(t + residual) for 0 < t <= 1, the residual far below t.
Series AtanKernel(float128_t t, float128_t residual);

// 2^x - 1 for 2^-60 <= |x| <= 1 (x exact).
float128_t Exp2Minus1Kernel(float128_t x);

// log2(m * 2^e) for m in [1, 2); exact (an integer) when m == 1.
float128_t Log2Kernel(float128_t m, std::int32_t e);

// log2(1 + u), u = x/(2 + x) form, for |x| <= 1/2 (x exact).
float128_t Log2OnePlusKernel(float128_t x);

}  // namespace rex86::fpu::detail

#endif  // REX86_FPU_TRANSCENDENTAL_KERNELS_H_
