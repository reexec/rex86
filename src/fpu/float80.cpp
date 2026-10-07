#include "fpu/float80.h"

namespace rex86::fpu
{

Kind Classify(const Float80& value)
{
    const std::uint16_t exponent = value.Exponent();
    const bool integer_bit = value.IntegerBit();
    const std::uint64_t fraction = value.significand & 0x7FFFFFFFFFFFFFFFull;
    if (exponent == 0)
    {
        if (value.significand == 0)
        {
            return Kind::kZero;
        }
        return Kind::kDenormal;  // pseudo-denormals (J = 1) included
    }
    if (!integer_bit)
    {
        return Kind::kUnsupported;  // unnormal, pseudo-NaN, pseudo-infinity
    }
    if (exponent == kExponentMax)
    {
        if (fraction == 0)
        {
            return Kind::kInfinity;
        }
        return (fraction & 0x4000000000000000ull) != 0 ? Kind::kQuietNaN
                                                       : Kind::kSignalingNaN;
    }
    return Kind::kNormal;
}

Float80 FromBytes(const std::uint8_t bytes[10])
{
    Float80 value;
    for (int index = 7; index >= 0; --index)
    {
        value.significand = (value.significand << 8) | bytes[index];
    }
    value.sign_exponent =
        static_cast<std::uint16_t>(bytes[8] | (bytes[9] << 8));
    return value;
}

void ToBytes(const Float80& value, std::uint8_t bytes[10])
{
    for (int index = 0; index < 8; ++index)
    {
        bytes[index] =
            static_cast<std::uint8_t>(value.significand >> (8 * index));
    }
    bytes[8] = static_cast<std::uint8_t>(value.sign_exponent);
    bytes[9] = static_cast<std::uint8_t>(value.sign_exponent >> 8);
}

}  // namespace rex86::fpu
