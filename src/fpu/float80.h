// The x87's 80-bit extended-real value and its classification. This layer
// knows the number format alone -- no stack, no instruction encoding -- so
// the interpreter and, later, the translation backends' helpers share it.
// See docs/design/20261008-i019-x87-increment-1.md.

#ifndef REX86_FPU_FLOAT80_H_
#define REX86_FPU_FLOAT80_H_

#include <cstdint>

namespace rex86::fpu
{

// The 80-bit memory format: a 64-bit significand with an explicit integer
// bit (J, bit 63) and a 16-bit sign-and-exponent word (bias 16383).
struct Float80
{
    std::uint64_t significand = 0;
    std::uint16_t sign_exponent = 0;

    [[nodiscard]] bool Sign() const
    {
        return (sign_exponent & 0x8000u) != 0;
    }

    [[nodiscard]] std::uint16_t Exponent() const
    {
        return static_cast<std::uint16_t>(sign_exponent & 0x7FFFu);
    }

    [[nodiscard]] bool IntegerBit() const
    {
        return (significand >> 63) != 0;
    }

    friend bool operator==(const Float80&, const Float80&) = default;
};

inline constexpr std::uint16_t kExponentBias = 0x3FFF;
inline constexpr std::uint16_t kExponentMax = 0x7FFF;

// The x87's QNaN 'real indefinite': the default response to a masked
// invalid operation.
inline constexpr Float80 kIndefinite{0xC000000000000000ull, 0xFFFF};

// What an x87 operand is. The 387 and later reject the encodings the 8087
// and 287 accepted -- unnormals (exponent nonzero, J = 0), pseudo-NaNs and
// pseudo-infinities (maximum exponent, J = 0) -- as invalid operands
// (kUnsupported). A pseudo-denormal (exponent zero, J = 1) is accepted and
// classed with the denormals.
enum class Kind : std::uint8_t
{
    kZero,
    kDenormal,
    kNormal,
    kInfinity,
    kQuietNaN,
    kSignalingNaN,
    kUnsupported,
};

Kind Classify(const Float80& value);

[[nodiscard]] inline bool IsNaN(const Kind kind)
{
    return kind == Kind::kQuietNaN || kind == Kind::kSignalingNaN;
}

// The 10-byte little-endian memory image.
Float80 FromBytes(const std::uint8_t bytes[10]);
void ToBytes(const Float80& value, std::uint8_t bytes[10]);

}  // namespace rex86::fpu

#endif  // REX86_FPU_FLOAT80_H_
