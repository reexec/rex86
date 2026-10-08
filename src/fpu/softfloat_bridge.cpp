#include "fpu/softfloat_bridge.h"

namespace rex86::fpu::detail
{

extFloat80_t ToSoft(const Float80& value)
{
    extFloat80_t soft;
    soft.signif = value.significand;
    soft.signExp = value.sign_exponent;
    if (value.Exponent() == 0 && value.IntegerBit())
    {
        soft.signExp = static_cast<std::uint16_t>(soft.signExp | 1u);
    }
    return soft;
}

Float80 FromSoft(const extFloat80_t& soft)
{
    return Float80{soft.signif, soft.signExp};
}

std::uint_fast8_t SoftRounding(const unsigned rounding_control)
{
    switch (rounding_control)
    {
        case 1: return softfloat_round_min;
        case 2: return softfloat_round_max;
        case 3: return softfloat_round_minMag;
        case 0:
        default: return softfloat_round_near_even;
    }
}

std::uint_fast8_t SoftPrecision(const std::uint16_t control_word)
{
    switch ((control_word >> 8) & 3u)
    {
        case 0: return 32;
        case 2: return 64;
        default: return 80;
    }
}

std::uint16_t FromSoftFlags(const std::uint_fast8_t flags)
{
    std::uint16_t raised = 0;
    if ((flags & softfloat_flag_invalid) != 0) raised |= kInvalid;
    if ((flags & softfloat_flag_infinite) != 0) raised |= kZeroDivide;
    if ((flags & softfloat_flag_overflow) != 0) raised |= kOverflow;
    if ((flags & softfloat_flag_underflow) != 0) raised |= kUnderflow;
    if ((flags & softfloat_flag_inexact) != 0) raised |= kPrecision;
    return raised;
}

SoftScope::SoftScope(const unsigned rounding_control, const std::uint_fast8_t precision)
{
    softfloat_roundingMode = SoftRounding(rounding_control);
    extF80_roundingPrecision = precision;
    softfloat_detectTininess = softfloat_tininess_afterRounding;
    softfloat_exceptionFlags = 0;
}

std::uint16_t SoftScope::Raised() const
{
    return FromSoftFlags(softfloat_exceptionFlags);
}

bool MagnitudeGreater(const Float80& a, const Float80& b)
{
    if (a.Exponent() != b.Exponent())
    {
        return a.Exponent() > b.Exponent();
    }
    return a.significand > b.significand;
}

Unpacked Unpack(const Float80& value)
{
    Unpacked out;
    out.sign = value.Sign();
    std::int32_t exponent = value.Exponent();
    std::uint64_t significand = value.significand;
    if (exponent == 0)
    {
        exponent = 1;  // denormals and pseudo-denormals share emin
    }
    while ((significand >> 63) == 0)
    {
        significand <<= 1;
        --exponent;
    }
    out.exponent = exponent - kExponentBias;
    out.significand = significand;
    return out;
}

Precheck CheckOperands(Status* status, const Float80& a, const Float80* b,
                       Float80* result, const OperandHints hints,
                       const bool denormals)
{
    const Kind ka = Classify(a);
    const Kind kb = b != nullptr ? Classify(*b) : Kind::kNormal;
    if (ka == Kind::kUnsupported || kb == Kind::kUnsupported)
    {
        status->raised |= kInvalid;
        if (status->Unmasked(kInvalid))
        {
            return Precheck::kSuppressed;
        }
        *result = kIndefinite;
        return Precheck::kDone;
    }
    if (IsNaN(ka) || IsNaN(kb))
    {
        if (ka == Kind::kSignalingNaN || kb == Kind::kSignalingNaN)
        {
            status->raised |= kInvalid;
            if (status->Unmasked(kInvalid))
            {
                return Precheck::kSuppressed;
            }
        }
        return Precheck::kNaNResult;
    }
    if (denormals &&
        (ka == Kind::kDenormal || kb == Kind::kDenormal || hints.a_denormal ||
         hints.b_denormal))
    {
        status->raised |= kDenormalOperand;
        if (status->Unmasked(kDenormalOperand))
        {
            return Precheck::kSuppressed;
        }
    }
    return Precheck::kProceed;
}

}  // namespace rex86::fpu::detail
