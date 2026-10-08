// The glue between the x87 numeric model and Berkeley SoftFloat 3: value
// conversion, the per-call rounding globals, flag translation and the
// operand checks every x87 operation shares. Internal to src/fpu/: it
// includes SoftFloat's headers, which nothing outside src/fpu/ sees.
// Split out of x87_math.cpp for the transcendentals (design #25).

#ifndef REX86_FPU_SOFTFLOAT_BRIDGE_H_
#define REX86_FPU_SOFTFLOAT_BRIDGE_H_

#include <cstdint>

#include "fpu/float80.h"
#include "fpu/x87_math.h"

// SoftFloat's headers expect platform.h first, as its own sources do.
extern "C" {
#include "platform.h"
#include "softfloat.h"
#include "internals.h"
}

namespace rex86::fpu::detail
{

// SoftFloat's operand. A pseudo-denormal (exponent 0, J = 1) has the
// value of the same significand at exponent 1, and SoftFloat mishandles
// it as given (an addition loses the carry out of the significand), so it
// is passed in that canonical form.
extFloat80_t ToSoft(const Float80& value);
Float80 FromSoft(const extFloat80_t& soft);

std::uint_fast8_t SoftRounding(unsigned rounding_control);

// CW.PC: 00 single (24 bits), 10 double (53), 11 extended (64). The
// reserved 01 behaves as extended.
std::uint_fast8_t SoftPrecision(std::uint16_t control_word);

std::uint16_t FromSoftFlags(std::uint_fast8_t flags);

// Sets SoftFloat's globals for one call and collects its flags.
class SoftScope
{
public:
    SoftScope(unsigned rounding_control, std::uint_fast8_t precision);

    [[nodiscard]] std::uint16_t Raised() const;
};

// |a| > |b| for canonical values (no unnormals): the exponent then the
// significand order the magnitudes.
bool MagnitudeGreater(const Float80& a, const Float80& b);

// A finite nonzero value as an exact (unbiased exponent, significand with
// J set) pair; denormals and pseudo-denormals normalized.
struct Unpacked
{
    bool sign = false;
    std::int32_t exponent = 0;  // value = significand * 2^(exponent - 63)
    std::uint64_t significand = 0;
};

Unpacked Unpack(const Float80& value);

// The pre-computation checks every arithmetic operation shares: invalid
// operands (unsupported encodings, signaling NaNs) and, unless
// `denormals` is false, denormals.
enum class Precheck : std::uint8_t
{
    kProceed,     // compute normally
    kNaNResult,   // a NaN operand decides the result; SoftFloat propagates
    kDone,        // *result already set (masked invalid response)
    kSuppressed,  // unmasked exception: nothing is written
};

Precheck CheckOperands(Status* status, const Float80& a, const Float80* b,
                       Float80* result, OperandHints hints = {},
                       bool denormals = true);

}  // namespace rex86::fpu::detail

#endif  // REX86_FPU_SOFTFLOAT_BRIDGE_H_
