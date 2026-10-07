// Eager EFLAGS computation, shared by every width. The interpreter computes
// flags at each instruction (design #1: the correctness reference computes
// flags eagerly; lazy flags belong to the translation backends).

#ifndef REX86_INTERP_FLAGS_H_
#define REX86_INTERP_FLAGS_H_

#include <cstdint>

#include "rex86/cpu_state.h"

namespace rex86::interp
{

constexpr std::uint32_t WidthMask(const unsigned width_bits)
{
    return width_bits >= 32 ? 0xFFFFFFFFu : ((1u << width_bits) - 1u);
}

constexpr std::uint32_t SignBit(const unsigned width_bits)
{
    return 1u << (width_bits - 1u);
}

// Parity of the low byte: PF is set for an even number of one bits.
inline bool ParityEven(const std::uint32_t value)
{
    std::uint8_t byte = static_cast<std::uint8_t>(value);
    byte ^= static_cast<std::uint8_t>(byte >> 4);
    byte ^= static_cast<std::uint8_t>(byte >> 2);
    byte ^= static_cast<std::uint8_t>(byte >> 1);
    return (byte & 1u) == 0;
}

inline void SetFlag(CpuState& state, const std::uint32_t flag, const bool on)
{
    state.eflags = on ? (state.eflags | flag) : (state.eflags & ~flag);
}

inline bool GetFlag(const CpuState& state, const std::uint32_t flag)
{
    return (state.eflags & flag) != 0;
}

// SF, ZF and PF from a result, shared by every flag-writing instruction.
inline void SetResultFlags(CpuState& state, const unsigned width_bits,
                           const std::uint32_t result)
{
    const std::uint32_t masked = result & WidthMask(width_bits);
    SetFlag(state, kEflagsZero, masked == 0);
    SetFlag(state, kEflagsSign, (masked & SignBit(width_bits)) != 0);
    SetFlag(state, kEflagsParity, ParityEven(masked));
}

// Flags of ADD/ADC (is_subtract false) and SUB/SBB/CMP/NEG (true), with
// carry_in for the ADC/SBB forms. set_carry is false for INC and DEC,
// which preserve CF while writing every other arithmetic flag.
inline void SetArithmeticFlags(CpuState& state, const unsigned width_bits,
                               const std::uint32_t lhs,
                               const std::uint32_t rhs,
                               const std::uint32_t carry_in,
                               const std::uint32_t result,
                               const bool is_subtract,
                               const bool set_carry = true)
{
    const std::uint32_t mask = WidthMask(width_bits);
    const std::uint32_t sign = SignBit(width_bits);
    const std::uint32_t a = lhs & mask;
    const std::uint32_t b = rhs & mask;
    const std::uint32_t r = result & mask;

    if (set_carry)
    {
        const std::uint64_t wide = is_subtract
            ? static_cast<std::uint64_t>(b) + carry_in
            : static_cast<std::uint64_t>(a) + b + carry_in;
        const bool carry = is_subtract
            ? static_cast<std::uint64_t>(a) < wide
            : (wide & ~static_cast<std::uint64_t>(mask)) != 0;
        SetFlag(state, kEflagsCarry, carry);
    }
    const std::uint32_t overflow = is_subtract ? ((a ^ b) & (a ^ r))
                                               : ((a ^ r) & (b ^ r));
    SetFlag(state, kEflagsOverflow, (overflow & sign) != 0);
    SetFlag(state, kEflagsAdjust, ((a ^ b ^ r) & 0x10u) != 0);
    SetResultFlags(state, width_bits, r);
}

// AND/OR/XOR/TEST: CF and OF cleared, AF left undefined by the SDM and
// cleared here so every host computes the same bit pattern.
inline void SetLogicFlags(CpuState& state, const unsigned width_bits,
                          const std::uint32_t result)
{
    SetFlag(state, kEflagsCarry, false);
    SetFlag(state, kEflagsOverflow, false);
    SetFlag(state, kEflagsAdjust, false);
    SetResultFlags(state, width_bits, result);
}

}  // namespace rex86::interp

#endif  // REX86_INTERP_FLAGS_H_
