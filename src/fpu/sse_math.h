// The SSE binary32 numeric model (design #29, decision 6): one lane of an
// SSE floating-point instruction, as bits in and bits out with the MXCSR
// exception flags it raises, split into the pre-computation flags (#I, #D,
// #Z) and the post-computation flags (#O, #U, #P) so the interpreter can
// apply SDM 11.5.1's order across lanes. Built on SoftFloat 3e with SSE's
// own NaN, denormal-operand, tininess and flush-to-zero rules, measured on
// a Zen 3 where the SDM leaves them open. Knows no instruction encoding.

#ifndef REX86_FPU_SSE_MATH_H_
#define REX86_FPU_SSE_MATH_H_

#include <cstdint>

namespace rex86::fpu::sse
{

// MXCSR bits.
inline constexpr std::uint32_t kInvalid = 0x01;
inline constexpr std::uint32_t kDenormal = 0x02;
inline constexpr std::uint32_t kZeroDivide = 0x04;
inline constexpr std::uint32_t kOverflow = 0x08;
inline constexpr std::uint32_t kUnderflow = 0x10;
inline constexpr std::uint32_t kPrecision = 0x20;
inline constexpr std::uint32_t kFlagMask = 0x3F;
inline constexpr unsigned kMaskShift = 7;
inline constexpr std::uint32_t kFlushToZero = 0x8000;

inline constexpr std::uint32_t kDefaultNaN = 0xFFC00000u;
inline constexpr std::uint32_t kIntegerIndefinite = 0x80000000u;

struct Lane
{
    std::uint32_t value = 0;
    std::uint32_t pre = 0;   // #I, #D, #Z
    std::uint32_t post = 0;  // #O, #U, #P
};

enum class Op : std::uint8_t
{
    kAdd,
    kSub,
    kMul,
    kDiv,
    kMin,
    kMax,
    kSqrt,  // of b
    kRcp,   // of b; no flags
    kRsqrt, // of b; no flags
};

// a is the destination lane, b the source lane.
Lane Arith(Op op, std::uint32_t a, std::uint32_t b, std::uint32_t mxcsr);

// CMPPS/CMPSS predicate 0-7 (EQ, LT, LE, UNORD, NEQ, NLT, NLE, ORD): the
// all-ones or zero mask.
Lane Compare(unsigned predicate, std::uint32_t a, std::uint32_t b);

// COMISS (signal_quiet) and UCOMISS: ZF, PF, CF in `value` bits 6, 2, 0.
Lane OrderedCompare(std::uint32_t a, std::uint32_t b, bool signal_quiet);

// CVT(T)SS2SI, CVT(T)PS2PI: one lane to int32.
Lane ToInt32(std::uint32_t a, std::uint32_t mxcsr, bool truncate);
// CVTSI2SS, CVTPI2PS.
Lane FromInt32(std::uint32_t value, std::uint32_t mxcsr);

}  // namespace rex86::fpu::sse

#endif  // REX86_FPU_SSE_MATH_H_
