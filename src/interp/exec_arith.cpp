// Interpreter increment 2 (#13): shifts and rotates, double shifts,
// multiply and divide, the bit-test family, bit scans, SETcc, ENTER,
// LEAVE and XLAT. Where the SDM leaves a flag or value undefined, a
// deterministic choice is made so every host computes the same bits;
// the SST comparison masks those bits, and any mismatch outside the
// masks names the instruction to adjust.

#include "interp/exec.h"
#include "interp/flags.h"

namespace rex86::interp
{

bool ConditionCodeHolds(const unsigned condition_code, const CpuState& state)
{
    const bool cf = GetFlag(state, kEflagsCarry);
    const bool zf = GetFlag(state, kEflagsZero);
    const bool sf = GetFlag(state, kEflagsSign);
    const bool of = GetFlag(state, kEflagsOverflow);
    const bool pf = GetFlag(state, kEflagsParity);
    switch (condition_code & 0xFu)
    {
        case 0x0: return of;
        case 0x1: return !of;
        case 0x2: return cf;
        case 0x3: return !cf;
        case 0x4: return zf;
        case 0x5: return !zf;
        case 0x6: return cf || zf;
        case 0x7: return !cf && !zf;
        case 0x8: return sf;
        case 0x9: return !sf;
        case 0xA: return pf;
        case 0xB: return !pf;
        case 0xC: return sf != of;
        case 0xD: return sf == of;
        case 0xE: return zf || sf != of;
        default: return !zf && sf == of;
    }
}

namespace
{

ExecStatus Done(const Ctx& ctx)
{
    return ctx.faulted ? ExecStatus::kFault : ExecStatus::kContinue;
}


// --- shifts and rotates -------------------------------------------------

ExecStatus ExecShift(Ctx* ctx, const ZydisMnemonic mnemonic)
{
    CpuState& s = ctx->state;
    const ZydisDecodedOperand* const ops = ctx->decoded.operands;
    const unsigned width = ops[0].size;
    const std::uint32_t mask = WidthMask(width);

    std::uint32_t value = 0;
    std::uint32_t count = 0;
    if (!ReadOperand(ctx, ops[0], &value) || !ReadOperand(ctx, ops[1], &count))
    {
        return ExecStatus::kFault;
    }
    value &= mask;
    count &= 0x1Fu;  // the 386+ masks the count to 5 bits
    if (count == 0)
    {
        return ExecStatus::kContinue;  // no flags, no write
    }

    std::uint32_t result = 0;
    bool carry = false;
    bool overflow = GetFlag(s, kEflagsOverflow);
    switch (mnemonic)
    {
        case ZYDIS_MNEMONIC_SHL:
        {
            result = count < 32 ? (value << count) & mask : 0;
            if (count <= width)
            {
                carry = ((value >> (width - count)) & 1u) != 0;
            }
            else
            {
                // Above the operand width CF is undefined by the SDM.
                // Measured on the 386EX (D2.4/D2.5): the operand
                // recirculates every `width` steps, so CF is the low bit
                // when the count is a multiple of the width and zero
                // otherwise.
                carry = count % width == 0 && (value & 1u) != 0;
            }
            overflow = carry != ((result & SignBit(width)) != 0);
            break;
        }
        case ZYDIS_MNEMONIC_SHR:
        {
            result = count < 32 ? (value >> count) & mask : 0;
            // Above the operand width CF is undefined by the SDM; the
            // 386EX's recirculating path yields the sign bit at count
            // multiples of the width and zero otherwise (D2.5/D3.5).
            carry = count <= width
                ? ((value >> (count - 1)) & 1u) != 0
                : count % width == 0 && (value & SignBit(width)) != 0;
            // Defined for count 1 as the original sign; the 386 computes
            // it per step, so above count 1 the shifted sign is gone
            // (measured: D2.5/D3.5).
            overflow = count == 1 && (value & SignBit(width)) != 0;
            break;
        }
        case ZYDIS_MNEMONIC_SAR:
        {
            const std::int64_t wide = static_cast<std::int64_t>(
                static_cast<std::int32_t>(
                    (value & SignBit(width)) != 0 ? value | ~mask : value));
            result = static_cast<std::uint32_t>(wide >> (count < 63 ? count
                                                                    : 63)) &
                mask;
            carry = ((wide >> (count - 1)) & 1) != 0;
            overflow = false;  // defined for count 1
            break;
        }
        case ZYDIS_MNEMONIC_ROL:
        {
            const unsigned effective = count % width;
            result = effective == 0
                ? value
                : ((value << effective) | (value >> (width - effective))) &
                    mask;
            carry = (result & 1u) != 0;
            overflow = carry != ((result & SignBit(width)) != 0);
            break;
        }
        case ZYDIS_MNEMONIC_ROR:
        {
            const unsigned effective = count % width;
            result = effective == 0
                ? value
                : ((value >> effective) | (value << (width - effective))) &
                    mask;
            carry = (result & SignBit(width)) != 0;
            overflow = ((result ^ (result << 1)) & SignBit(width)) != 0;
            break;
        }
        case ZYDIS_MNEMONIC_RCL:
        case ZYDIS_MNEMONIC_RCR:
        {
            // A (width+1)-bit rotation through CF.
            const unsigned bits = width + 1;
            const unsigned effective = count % bits;
            const std::uint64_t combined = value |
                (static_cast<std::uint64_t>(GetFlag(s, kEflagsCarry) ? 1 : 0)
                 << width);
            std::uint64_t rotated = combined;
            if (effective != 0)
            {
                rotated = mnemonic == ZYDIS_MNEMONIC_RCL
                    ? ((combined << effective) |
                       (combined >> (bits - effective)))
                    : ((combined >> effective) |
                       (combined << (bits - effective)));
                rotated &= (1ull << bits) - 1u;
            }
            result = static_cast<std::uint32_t>(rotated) & mask;
            carry = ((rotated >> width) & 1u) != 0;
            overflow = mnemonic == ZYDIS_MNEMONIC_RCL
                ? carry != ((result & SignBit(width)) != 0)
                : ((result ^ (result << 1)) & SignBit(width)) != 0;
            break;
        }
        default:
            return ExecStatus::kUnimplemented;
    }

    SetFlag(s, kEflagsCarry, carry);
    SetFlag(s, kEflagsOverflow, overflow);
    if (mnemonic == ZYDIS_MNEMONIC_SHL || mnemonic == ZYDIS_MNEMONIC_SHR ||
        mnemonic == ZYDIS_MNEMONIC_SAR)
    {
        SetResultFlags(s, width, result);
        SetFlag(s, kEflagsAdjust, false);  // undefined; deterministic
    }
    WriteOperand(ctx, ops[0], result);
    return Done(*ctx);
}

ExecStatus ExecDoubleShift(Ctx* ctx, const bool left)
{
    CpuState& s = ctx->state;
    const ZydisDecodedOperand* const ops = ctx->decoded.operands;
    const unsigned width = ops[0].size;
    const std::uint32_t mask = WidthMask(width);

    std::uint32_t destination = 0;
    std::uint32_t source = 0;
    std::uint32_t count = 0;
    if (!ReadOperand(ctx, ops[0], &destination) ||
        !ReadOperand(ctx, ops[1], &source) ||
        !ReadOperand(ctx, ops[2], &count))
    {
        return ExecStatus::kFault;
    }
    destination &= mask;
    source &= mask;
    count &= 0x1Fu;
    if (count == 0)
    {
        return ExecStatus::kContinue;
    }

    // The concatenation view: a shift across both halves. A 16-bit count
    // above the operand width is undefined by the SDM; the 386 behaves
    // as a 32-bit rotation of the concatenation (measured: 0FA4.MOO
    // #7/#14), which the count > width branches reproduce.
    std::uint64_t combined;
    std::uint64_t shifted;
    std::uint32_t result;
    bool carry;
    if (left)
    {
        combined = (static_cast<std::uint64_t>(destination) << width) |
            source;
        shifted = combined << count;
        // Not `shifted >> (2 * width)`: that is a 64-bit shift by 64 for
        // 32-bit operands, which is undefined.
        carry = ((combined >> (2u * width - count)) & 1u) != 0;
        // A 16-bit count above the width is undefined by the SDM; the
        // 386EX rotates the SOURCE alone by count - width, ignoring the
        // destination (measured: 0FA4.MOO).
        result = count <= width
            ? static_cast<std::uint32_t>(shifted >> width) & mask
            : ((source << (count - width)) |
               (source >> (2u * width - count))) &
                mask;
    }
    else
    {
        combined = (static_cast<std::uint64_t>(source) << width) |
            destination;
        shifted = combined >> (count - 1u);
        carry = (shifted & 1u) != 0;
        result = count <= width
            ? static_cast<std::uint32_t>(shifted >> 1) & mask
            : ((source >> (count - width)) |
               (source << (2u * width - count))) &
                mask;
    }

    SetFlag(s, kEflagsCarry, carry);
    // OF is defined for count 1 (a sign change); undefined above, kept
    // deterministic by using the same formula throughout.
    SetFlag(s, kEflagsOverflow,
            ((destination ^ result) & SignBit(width)) != 0);
    SetResultFlags(s, width, result);
    SetFlag(s, kEflagsAdjust, false);
    WriteOperand(ctx, ops[0], result);
    return Done(*ctx);
}

// --- multiply and divide ------------------------------------------------

std::uint32_t SignExtendTo32(const std::uint32_t value, const unsigned width)
{
    return (value & SignBit(width)) != 0 ? value | ~WidthMask(width)
                                         : value & WidthMask(width);
}

ExecStatus ExecMul(Ctx* ctx)
{
    CpuState& s = ctx->state;
    const ZydisDecodedOperand* const ops = ctx->decoded.operands;
    const unsigned width = ops[0].size;
    std::uint32_t source = 0;
    if (!ReadOperand(ctx, ops[0], &source))
    {
        return ExecStatus::kFault;
    }
    source &= WidthMask(width);

    const std::uint32_t a = width == 8
        ? (s.Get(Gpr::kEax) & 0xFFu)
        : (width == 16 ? (s.Get(Gpr::kEax) & 0xFFFFu) : s.Get(Gpr::kEax));
    const std::uint64_t product = static_cast<std::uint64_t>(a) * source;
    const std::uint32_t low =
        static_cast<std::uint32_t>(product) & WidthMask(width);
    const std::uint32_t high =
        static_cast<std::uint32_t>(product >> width) & WidthMask(width);

    if (width == 8)
    {
        WriteGpr(s, ZYDIS_REGISTER_AX, static_cast<std::uint32_t>(product) &
                                           0xFFFFu);
    }
    else if (width == 16)
    {
        WriteGpr(s, ZYDIS_REGISTER_AX, low);
        WriteGpr(s, ZYDIS_REGISTER_DX, high);
    }
    else
    {
        s.Set(Gpr::kEax, low);
        s.Set(Gpr::kEdx, high);
    }
    const bool upper = high != 0;
    SetFlag(s, kEflagsCarry, upper);
    SetFlag(s, kEflagsOverflow, upper);
    // SF/ZF/AF/PF are undefined; derived deterministically from the low
    // half.
    SetResultFlags(s, width, low);
    SetFlag(s, kEflagsAdjust, false);
    return ExecStatus::kContinue;
}

ExecStatus ExecImul(Ctx* ctx)
{
    CpuState& s = ctx->state;
    const decode::DecodedInstruction& d = ctx->decoded;
    const ZydisDecodedOperand* const ops = d.operands;
    const unsigned visible = d.instruction.operand_count_visible;
    const unsigned width = ops[0].size;

    if (visible == 1)
    {
        std::uint32_t source = 0;
        if (!ReadOperand(ctx, ops[0], &source))
        {
            return ExecStatus::kFault;
        }
        const std::int64_t a = static_cast<std::int32_t>(SignExtendTo32(
            width == 8 ? (s.Get(Gpr::kEax) & 0xFFu)
                       : (width == 16 ? (s.Get(Gpr::kEax) & 0xFFFFu)
                                      : s.Get(Gpr::kEax)),
            width));
        const std::int64_t b =
            static_cast<std::int32_t>(SignExtendTo32(source, width));
        const std::int64_t product = a * b;
        const std::uint32_t low =
            static_cast<std::uint32_t>(product) & WidthMask(width);
        const std::uint32_t high =
            static_cast<std::uint32_t>(static_cast<std::uint64_t>(product) >>
                                       width) &
            WidthMask(width);
        if (width == 8)
        {
            WriteGpr(s, ZYDIS_REGISTER_AX,
                     static_cast<std::uint32_t>(product) & 0xFFFFu);
        }
        else if (width == 16)
        {
            WriteGpr(s, ZYDIS_REGISTER_AX, low);
            WriteGpr(s, ZYDIS_REGISTER_DX, high);
        }
        else
        {
            s.Set(Gpr::kEax, low);
            s.Set(Gpr::kEdx, high);
        }
        const bool fits = product == static_cast<std::int64_t>(
                                         static_cast<std::int32_t>(
                                             SignExtendTo32(low, width)));
        SetFlag(s, kEflagsCarry, !fits);
        SetFlag(s, kEflagsOverflow, !fits);
        SetResultFlags(s, width, low);
        SetFlag(s, kEflagsAdjust, false);
        return ExecStatus::kContinue;
    }

    // Two- and three-operand forms: a truncating signed multiply.
    std::uint32_t lhs = 0;
    std::uint32_t rhs = 0;
    if (visible == 2)
    {
        if (!ReadOperand(ctx, ops[0], &lhs) || !ReadOperand(ctx, ops[1], &rhs))
        {
            return ExecStatus::kFault;
        }
    }
    else
    {
        if (!ReadOperand(ctx, ops[1], &lhs) || !ReadOperand(ctx, ops[2], &rhs))
        {
            return ExecStatus::kFault;
        }
    }
    const std::int64_t product =
        static_cast<std::int64_t>(
            static_cast<std::int32_t>(SignExtendTo32(lhs, width))) *
        static_cast<std::int32_t>(SignExtendTo32(rhs, width));
    const std::uint32_t low =
        static_cast<std::uint32_t>(product) & WidthMask(width);
    const bool fits = product == static_cast<std::int64_t>(
                                     static_cast<std::int32_t>(
                                         SignExtendTo32(low, width)));
    SetFlag(s, kEflagsCarry, !fits);
    SetFlag(s, kEflagsOverflow, !fits);
    SetResultFlags(s, width, low);
    SetFlag(s, kEflagsAdjust, false);
    WriteOperand(ctx, ops[0], low);
    return Done(*ctx);
}

ExecStatus ExecDiv(Ctx* ctx, const bool is_signed)
{
    CpuState& s = ctx->state;
    const ZydisDecodedOperand* const ops = ctx->decoded.operands;
    const unsigned width = ops[0].size;
    std::uint32_t divisor = 0;
    if (!ReadOperand(ctx, ops[0], &divisor))
    {
        return ExecStatus::kFault;
    }
    divisor &= WidthMask(width);

    const std::uint32_t low = width == 8
        ? (s.Get(Gpr::kEax) & 0xFFu)
        : (width == 16 ? (s.Get(Gpr::kEax) & 0xFFFFu) : s.Get(Gpr::kEax));
    const std::uint32_t high = width == 8
        ? ((s.Get(Gpr::kEax) >> 8) & 0xFFu)
        : (width == 16 ? (s.Get(Gpr::kEdx) & 0xFFFFu) : s.Get(Gpr::kEdx));

    const auto divide_fault = [&]() {
        ctx->Fault(FaultKind::kDivide,
                   ctx->state.Seg(Segment::kCs).base + ctx->state.eip,
                   false);
        return ExecStatus::kFault;
    };
    if (divisor == 0)
    {
        return divide_fault();
    }

    std::uint32_t quotient = 0;
    std::uint32_t remainder = 0;
    if (!is_signed)
    {
        const std::uint64_t dividend =
            (static_cast<std::uint64_t>(high) << width) | low;
        const std::uint64_t q = dividend / divisor;
        if (q > WidthMask(width))
        {
            return divide_fault();
        }
        quotient = static_cast<std::uint32_t>(q);
        remainder = static_cast<std::uint32_t>(dividend % divisor);
    }
    else
    {
        const std::int64_t dividend = static_cast<std::int64_t>(
            (static_cast<std::uint64_t>(high) << width) | low) <<
                (64 - 2 * width) >>
            (64 - 2 * width);
        const std::int64_t sdivisor =
            static_cast<std::int32_t>(SignExtendTo32(divisor, width));
        const std::int64_t q = dividend / sdivisor;
        const std::int64_t r = dividend % sdivisor;
        const std::int64_t limit = 1ll << (width - 1);
        if (q >= limit || q < -limit)
        {
            return divide_fault();
        }
        quotient = static_cast<std::uint32_t>(q) & WidthMask(width);
        remainder = static_cast<std::uint32_t>(r) & WidthMask(width);
    }

    if (width == 8)
    {
        WriteGpr(s, ZYDIS_REGISTER_AL, quotient);
        WriteGpr(s, ZYDIS_REGISTER_AH, remainder);
    }
    else if (width == 16)
    {
        WriteGpr(s, ZYDIS_REGISTER_AX, quotient);
        WriteGpr(s, ZYDIS_REGISTER_DX, remainder);
    }
    else
    {
        s.Set(Gpr::kEax, quotient);
        s.Set(Gpr::kEdx, remainder);
    }
    // Every flag is undefined after a divide; left unchanged.
    return ExecStatus::kContinue;
}

// --- bit operations -----------------------------------------------------

ExecStatus ExecBitTest(Ctx* ctx, const ZydisMnemonic mnemonic)
{
    CpuState& s = ctx->state;
    const decode::DecodedInstruction& d = ctx->decoded;
    const ZydisDecodedOperand* const ops = d.operands;
    const unsigned width = ops[0].size;

    std::uint32_t offset_value = 0;
    if (!ReadOperand(ctx, ops[1], &offset_value))
    {
        return ExecStatus::kFault;
    }

    bool bit = false;
    const bool write = mnemonic != ZYDIS_MNEMONIC_BT;
    if (ops[0].type == ZYDIS_OPERAND_TYPE_MEMORY &&
        ops[1].type == ZYDIS_OPERAND_TYPE_REGISTER)
    {
        // A register bit offset over memory addresses bits beyond the
        // operand. The access is one operand-size unit at EA + (width/8) *
        // floor(index / width), as the SDM describes and the 386EX shows
        // (a word or dword crossing the segment limit faults).
        const std::int32_t index = static_cast<std::int32_t>(
            SignExtendTo32(offset_value & WidthMask(width), width));
        const std::int32_t unit = index >> (width == 16 ? 4 : 5);
        const std::uint32_t byte_offset =
            static_cast<std::uint32_t>(unit) * (width / 8u);
        const unsigned bit_index = static_cast<unsigned>(index) & (width - 1u);
        const std::uint32_t address =
            (EffectiveAddress(*ctx, ops[0]) + byte_offset) &
            WidthMask(d.instruction.address_width);
        const Segment segment = SegmentOf(d, ops[0]);
        std::uint32_t value = 0;
        if (!ReadVirtual(ctx, segment, address, width, &value))
        {
            return ExecStatus::kFault;
        }
        bit = ((value >> bit_index) & 1u) != 0;
        if (write)
        {
            std::uint32_t updated = value;
            if (mnemonic == ZYDIS_MNEMONIC_BTS)
            {
                updated |= 1u << bit_index;
            }
            else if (mnemonic == ZYDIS_MNEMONIC_BTR)
            {
                updated &= ~(1u << bit_index);
            }
            else
            {
                updated ^= 1u << bit_index;
            }
            if (!WriteVirtual(ctx, segment, address, width, updated))
            {
                return ExecStatus::kFault;
            }
        }
    }
    else
    {
        const unsigned bit_index =
            static_cast<unsigned>(offset_value) & (width - 1u);
        std::uint32_t value = 0;
        if (!ReadOperand(ctx, ops[0], &value))
        {
            return ExecStatus::kFault;
        }
        bit = ((value >> bit_index) & 1u) != 0;
        if (write)
        {
            if (mnemonic == ZYDIS_MNEMONIC_BTS)
            {
                value |= 1u << bit_index;
            }
            else if (mnemonic == ZYDIS_MNEMONIC_BTR)
            {
                value &= ~(1u << bit_index);
            }
            else
            {
                value ^= 1u << bit_index;
            }
            if (!WriteOperand(ctx, ops[0], value & WidthMask(width)))
            {
                return ctx->faulted ? ExecStatus::kFault
                                    : ExecStatus::kUnimplemented;
            }
        }
    }
    SetFlag(s, kEflagsCarry, bit);
    return Done(*ctx);
}

ExecStatus ExecBitScan(Ctx* ctx, const bool forward)
{
    CpuState& s = ctx->state;
    const ZydisDecodedOperand* const ops = ctx->decoded.operands;
    const unsigned width = ops[0].size;
    std::uint32_t source = 0;
    if (!ReadOperand(ctx, ops[1], &source))
    {
        return ExecStatus::kFault;
    }
    source &= WidthMask(width);
    if (source == 0)
    {
        SetFlag(s, kEflagsZero, true);
        return ExecStatus::kContinue;  // destination undefined: preserved
    }
    unsigned index = 0;
    if (forward)
    {
        while (((source >> index) & 1u) == 0)
        {
            ++index;
        }
    }
    else
    {
        index = width - 1u;
        while (((source >> index) & 1u) == 0)
        {
            --index;
        }
    }
    SetFlag(s, kEflagsZero, false);
    WriteOperand(ctx, ops[0], index);
    return Done(*ctx);
}

// --- frames and lookups -------------------------------------------------

ExecStatus ExecEnter(Ctx* ctx)
{
    CpuState& s = ctx->state;
    const decode::DecodedInstruction& d = ctx->decoded;
    const unsigned width = d.instruction.operand_width;
    const std::uint32_t alloc =
        static_cast<std::uint32_t>(d.operands[0].imm.value.u);
    const unsigned nesting =
        static_cast<unsigned>(d.operands[1].imm.value.u) % 32u;
    const SegmentRegister& ss = s.Seg(Segment::kSs);
    const std::uint32_t sp_mask = ss.default_32bit ? 0xFFFFFFFFu : 0xFFFFu;

    if (!Push(ctx, width, s.Get(Gpr::kEbp) & WidthMask(width)))
    {
        return ExecStatus::kFault;
    }
    const std::uint32_t frame_temp = s.Get(Gpr::kEsp) & sp_mask;
    if (nesting > 0)
    {
        std::uint32_t walker = s.Get(Gpr::kEbp);
        for (unsigned level = 1; level < nesting; ++level)
        {
            walker = (walker - width / 8u) & WidthMask(width);
            std::uint32_t display = 0;
            if (!ReadVirtual(ctx, Segment::kSs, walker & sp_mask, width,
                             &display) ||
                !Push(ctx, width, display))
            {
                return ExecStatus::kFault;
            }
        }
        if (!Push(ctx, width, frame_temp & WidthMask(width)))
        {
            return ExecStatus::kFault;
        }
    }
    if (width == 16)
    {
        WriteGpr(s, ZYDIS_REGISTER_BP, frame_temp);
    }
    else
    {
        s.Set(Gpr::kEbp, frame_temp);
    }
    const std::uint32_t sp = s.Get(Gpr::kEsp);
    s.Set(Gpr::kEsp, ((sp - alloc) & sp_mask) | (sp & ~sp_mask));
    return ExecStatus::kContinue;
}

ExecStatus ExecLeave(Ctx* ctx)
{
    CpuState& s = ctx->state;
    const unsigned width = ctx->decoded.instruction.operand_width;
    const SegmentRegister& ss = s.Seg(Segment::kSs);
    const std::uint32_t sp_mask = ss.default_32bit ? 0xFFFFFFFFu : 0xFFFFu;
    const std::uint32_t sp = s.Get(Gpr::kEsp);
    s.Set(Gpr::kEsp,
          (s.Get(Gpr::kEbp) & sp_mask) | (sp & ~sp_mask));
    std::uint32_t frame = 0;
    if (!Pop(ctx, width, &frame))
    {
        return ExecStatus::kFault;
    }
    if (width == 16)
    {
        WriteGpr(s, ZYDIS_REGISTER_BP, frame);
    }
    else
    {
        s.Set(Gpr::kEbp, frame);
    }
    return ExecStatus::kContinue;
}

ExecStatus ExecXlat(Ctx* ctx)
{
    CpuState& s = ctx->state;
    const decode::DecodedInstruction& d = ctx->decoded;
    const std::uint32_t address_mask = WidthMask(d.instruction.address_width);
    const std::uint32_t table = (s.Get(Gpr::kEbx) +
                                 (s.Get(Gpr::kEax) & 0xFFu)) &
        address_mask;
    // XLAT's hidden memory operand carries the segment, overrides
    // included.
    Segment segment = Segment::kDs;
    for (ZyanU8 index = 0; index < d.instruction.operand_count; ++index)
    {
        if (d.operands[index].type == ZYDIS_OPERAND_TYPE_MEMORY)
        {
            segment = SegmentOf(d, d.operands[index]);
            break;
        }
    }
    std::uint32_t value = 0;
    if (!ReadVirtual(ctx, segment, table, 8, &value))
    {
        return ExecStatus::kFault;
    }
    WriteGpr(s, ZYDIS_REGISTER_AL, value);
    return ExecStatus::kContinue;
}

}  // namespace

ExecStatus ExecuteArith2(Ctx* ctx, Event* stop_event)
{
    static_cast<void>(stop_event);
    const ZydisMnemonic mnemonic = ctx->decoded.instruction.mnemonic;
    switch (mnemonic)
    {
        case ZYDIS_MNEMONIC_SHL:
        case ZYDIS_MNEMONIC_SHR:
        case ZYDIS_MNEMONIC_SAR:
        case ZYDIS_MNEMONIC_ROL:
        case ZYDIS_MNEMONIC_ROR:
        case ZYDIS_MNEMONIC_RCL:
        case ZYDIS_MNEMONIC_RCR:
            return ExecShift(ctx, mnemonic);
        case ZYDIS_MNEMONIC_SHLD:
            return ExecDoubleShift(ctx, true);
        case ZYDIS_MNEMONIC_SHRD:
            return ExecDoubleShift(ctx, false);
        case ZYDIS_MNEMONIC_MUL:
            return ExecMul(ctx);
        case ZYDIS_MNEMONIC_IMUL:
            return ExecImul(ctx);
        case ZYDIS_MNEMONIC_DIV:
            return ExecDiv(ctx, false);
        case ZYDIS_MNEMONIC_IDIV:
            return ExecDiv(ctx, true);
        case ZYDIS_MNEMONIC_BT:
        case ZYDIS_MNEMONIC_BTS:
        case ZYDIS_MNEMONIC_BTR:
        case ZYDIS_MNEMONIC_BTC:
            return ExecBitTest(ctx, mnemonic);
        case ZYDIS_MNEMONIC_BSF:
            return ExecBitScan(ctx, true);
        case ZYDIS_MNEMONIC_BSR:
            return ExecBitScan(ctx, false);
        case ZYDIS_MNEMONIC_SETO: case ZYDIS_MNEMONIC_SETNO:
        case ZYDIS_MNEMONIC_SETB: case ZYDIS_MNEMONIC_SETNB:
        case ZYDIS_MNEMONIC_SETZ: case ZYDIS_MNEMONIC_SETNZ:
        case ZYDIS_MNEMONIC_SETBE: case ZYDIS_MNEMONIC_SETNBE:
        case ZYDIS_MNEMONIC_SETS: case ZYDIS_MNEMONIC_SETNS:
        case ZYDIS_MNEMONIC_SETP: case ZYDIS_MNEMONIC_SETNP:
        case ZYDIS_MNEMONIC_SETL: case ZYDIS_MNEMONIC_SETNL:
        case ZYDIS_MNEMONIC_SETLE: case ZYDIS_MNEMONIC_SETNLE:
        {
            const bool hold = ConditionCodeHolds(
                ctx->decoded.instruction.opcode & 0xFu, ctx->state);
            if (!WriteOperand(ctx, ctx->decoded.operands[0], hold ? 1u : 0u))
            {
                return ctx->faulted ? ExecStatus::kFault
                                    : ExecStatus::kUnimplemented;
            }
            return Done(*ctx);
        }
        case ZYDIS_MNEMONIC_ENTER:
            return ExecEnter(ctx);
        case ZYDIS_MNEMONIC_LEAVE:
            return ExecLeave(ctx);
        case ZYDIS_MNEMONIC_XLAT:
            return ExecXlat(ctx);
        default:
            return ExecStatus::kUnimplemented;
    }
}

ExecStatus ExecuteExtended(Ctx* ctx, std::uint32_t* next_eip,
                           Event* stop_event)
{
    ExecStatus status = ExecuteArith2(ctx, stop_event);
    if (status != ExecStatus::kUnimplemented)
    {
        return status;
    }
    status = ExecuteStrings(ctx, stop_event);
    if (status != ExecStatus::kUnimplemented)
    {
        return status;
    }
    status = ExecuteSegments(ctx, next_eip);
    if (status != ExecStatus::kUnimplemented)
    {
        return status;
    }
    return ExecuteBcd(ctx);
}

}  // namespace rex86::interp
