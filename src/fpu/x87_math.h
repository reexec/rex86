// The x87 numeric model: every operation the x87 performs on values, with
// the x87's exception semantics on top of Berkeley SoftFloat 3. Each
// function takes the control word through Status and reports the exception
// flags it raised and whether a rounded result went up (C1). A false
// return means an unmasked pre-computation exception (#IA, #D, #Z) or an
// unmasked numeric exception on a memory destination: the destination is
// not written. See docs/design/20261008-i019-x87-increment-1.md.

#ifndef REX86_FPU_X87_MATH_H_
#define REX86_FPU_X87_MATH_H_

#include <cstdint>

#include "fpu/float80.h"

namespace rex86::fpu
{

// The status-word exception flags (bits 0-6).
inline constexpr std::uint16_t kInvalid = 0x0001;
inline constexpr std::uint16_t kDenormalOperand = 0x0002;
inline constexpr std::uint16_t kZeroDivide = 0x0004;
inline constexpr std::uint16_t kOverflow = 0x0008;
inline constexpr std::uint16_t kUnderflow = 0x0010;
inline constexpr std::uint16_t kPrecision = 0x0020;
inline constexpr std::uint16_t kStackFault = 0x0040;
inline constexpr std::uint16_t kExceptionMask = 0x003F;

// One operation's inputs from CW and outputs toward SW.
struct Status
{
    explicit Status(const std::uint16_t control) : control_word(control)
    {
    }

    std::uint16_t control_word;
    // Exception flags this operation raised (to be OR-ed into SW).
    std::uint16_t raised = 0;
    // An inexact result was rounded away from zero: the C1 report.
    bool round_up = false;

    // True when any of `flags` is unmasked in CW.
    [[nodiscard]] bool Unmasked(const std::uint16_t flags) const
    {
        return (flags & ~control_word & kExceptionMask) != 0;
    }

    // CW.RC: 0 nearest, 1 down, 2 up, 3 toward zero.
    [[nodiscard]] unsigned RoundingControl() const
    {
        return (control_word >> 10) & 3u;
    }
};

enum class Arithmetic : std::uint8_t
{
    kAdd,
    kSubtract,
    kMultiply,
    kDivide,
};

// A memory operand converted from single or double precision is exact and
// normal in extended precision, but the x87 still reports #D for it: the
// hint carries that, so the exception takes its place in the priority
// order (SDM 4.9.2) instead of being decided at conversion.
struct OperandHints
{
    bool a_denormal = false;
    bool b_denormal = false;
};

// a op b under CW's rounding and precision control.
bool Arith(Status* status, Arithmetic op, const Float80& a, const Float80& b,
           Float80* result, OperandHints hints = {});

// FSQRT.
bool SquareRoot(Status* status, const Float80& a, Float80* result);

// FRNDINT: round to an integer value by CW.RC.
bool RoundToInteger(Status* status, const Float80& a, Float80* result);

// FSCALE: st0 * 2^trunc(st1).
bool Scale(Status* status, const Float80& st0, const Float80& st1,
           Float80* result);

// FXTRACT: x = significand * 2^exponent, the significand in [1, 2).
bool Extract(Status* status, const Float80& x, Float80* exponent,
             Float80* significand);

// FPREM (truncating quotient) and FPREM1 (IEEE, nearest quotient). On a
// complete reduction `complete` is true; `quotient_low` holds the
// quotient's three low bits either way.
bool Remainder(Status* status, bool ieee, const Float80& dividend,
               const Float80& divisor, Float80* result, bool* complete,
               unsigned* quotient_low);

// Comparison. `quiet` (FUCOM family) raises #IA for signaling NaNs only;
// otherwise any NaN raises it. The relation is always reported -- an
// invalid operand compares unordered and an unmasked #D still compares
// the values (measured, #19) -- and a false return means an unmasked
// exception withholds the instruction's pops.
enum class Relation : std::uint8_t
{
    kLess,
    kEqual,
    kGreater,
    kUnordered,
};
bool Compare(Status* status, const Float80& a, const Float80& b, bool quiet,
             Relation* relation, OperandHints hints = {});

// A single- or double-precision memory operand as an arithmetic or compare
// source: exact, raising nothing, a signaling NaN kept signaling (the
// operation raises #IA and picks the NaN by the x87's rules); `denormal`
// reports a denormal source for OperandHints.
Float80 SourceFromFloat32(std::uint32_t bits, bool* denormal);
Float80 SourceFromFloat64(std::uint64_t bits, bool* denormal);

// Loads (FLD m32/m64; FILD; FBLD) and stores (FST m32/m64; FIST; FBSTP).
bool FromFloat32(Status* status, std::uint32_t bits, Float80* result);
bool FromFloat64(Status* status, std::uint64_t bits, Float80* result);
Float80 FromInteger(std::int64_t value);
bool ToFloat32(Status* status, const Float80& a, std::uint32_t* bits);
bool ToFloat64(Status* status, const Float80& a, std::uint64_t* bits);
// width_bits is 16, 32 or 64; out-of-range values give the integer
// indefinite (the most negative value) when #IA is masked.
bool ToInteger(Status* status, const Float80& a, unsigned width_bits,
               std::int64_t* value);
Float80 FromPackedBcd(const std::uint8_t bcd[10]);
bool ToPackedBcd(Status* status, const Float80& a, std::uint8_t bcd[10]);

// FLDPI and the other constants, rounded by CW.RC.
enum class Constant : std::uint8_t
{
    kOne,
    kLog2Ten,
    kLog2E,
    kPi,
    kLog10Two,
    kLnTwo,
    kZero,
};
Float80 LoadConstant(Status* status, Constant constant);

}  // namespace rex86::fpu

#endif  // REX86_FPU_X87_MATH_H_
