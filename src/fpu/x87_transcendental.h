// The x87 transcendentals' numbers (#25): FSIN, FCOS, FSINCOS, FPTAN,
// FPATAN, F2XM1, FYL2X and FYL2XP1 without the register stack. Results
// are the SDM model's values correctly rounded to extended precision under
// CW.RC (precision control does not apply), computed in software so every
// host gets the same bits. A false return or kSuppressed means an
// unmasked pre-computation exception: nothing is written. See
// docs/design/20261008-i025-x87-transcendentals.md.

#ifndef REX86_FPU_X87_TRANSCENDENTAL_H_
#define REX86_FPU_X87_TRANSCENDENTAL_H_

#include <cstdint>

#include "fpu/float80.h"
#include "fpu/x87_math.h"

namespace rex86::fpu
{

// What a trigonometric instruction did.
enum class TrigOutcome : std::uint8_t
{
    kWritten,     // results produced (C2 = 0)
    kOutOfRange,  // |x| >= 2^63: nothing written, no exception (C2 = 1)
    kSuppressed,  // unmasked #IA or #D: nothing written
};

// FSIN, FCOS, FPTAN (the tangent; the 1.0 push is the caller's) and
// FSINCOS. The arguments are reduced by the x87's 66-bit Pi (SDM Vol. 1,
// 8.3.8). For FSINCOS, status->round_up reports the cosine.
TrigOutcome Sine(Status* status, const Float80& x, Float80* sine);
TrigOutcome Cosine(Status* status, const Float80& x, Float80* cosine);
TrigOutcome Tangent(Status* status, const Float80& x, Float80* tangent);
TrigOutcome SineCosine(Status* status, const Float80& x, Float80* sine,
                       Float80* cosine);

// FPATAN: atan2(y = ST(1), x = ST(0)).
bool Arctangent(Status* status, const Float80& y, const Float80& x,
                Float80* result);

// F2XM1: 2^x - 1.
bool Exp2Minus1(Status* status, const Float80& x, Float80* result);

// FYL2X: y * log2(x); FYL2XP1: y * log2(x + 1).
bool YLog2X(Status* status, const Float80& y, const Float80& x, Float80* result);
bool YLog2XPlus1(Status* status, const Float80& y, const Float80& x,
                 Float80* result);

}  // namespace rex86::fpu

#endif  // REX86_FPU_X87_TRANSCENDENTAL_H_
