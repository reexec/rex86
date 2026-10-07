// Interpreter increment 3 (#15): the BCD adjustments, BOUND and SALC.
// The BCD instructions follow the SDM pseudocode exactly (DAS has no ELSE
// on its second condition); flags the SDM leaves undefined are computed
// deterministically and, where SingleStepTests shows a different 386
// value outside the f_umask, adjusted to it (see the analysis topic).

#include "interp/exec.h"
#include "interp/flags.h"

namespace rex86::interp
{

namespace
{

std::uint32_t Al(const CpuState& s)
{
    return s.Get(Gpr::kEax) & 0xFFu;
}

std::uint32_t Ah(const CpuState& s)
{
    return (s.Get(Gpr::kEax) >> 8) & 0xFFu;
}

void SetAx(CpuState& s, const std::uint32_t ax)
{
    s.Set(Gpr::kEax, (s.Get(Gpr::kEax) & 0xFFFF0000u) | (ax & 0xFFFFu));
}

void SetAl(CpuState& s, const std::uint32_t al)
{
    s.Set(Gpr::kEax, (s.Get(Gpr::kEax) & 0xFFFFFF00u) | (al & 0xFFu));
}

ExecStatus ExecAaa(CpuState& s, const bool subtract)
{
    if ((Al(s) & 0x0Fu) > 9u || GetFlag(s, kEflagsAdjust))
    {
        const std::uint32_t ax = s.Get(Gpr::kEax) & 0xFFFFu;
        // AAA: AX + 106h. AAS: AX - 6 as a 16-bit subtraction (a borrow
        // out of AL reaches AH), then AH - 1 -- the SDM pseudocode.
        const std::uint32_t adjusted = subtract
            ? (ax - 6u) - 0x100u
            : ax + 0x106u;
        SetAx(s, adjusted);
        SetFlag(s, kEflagsAdjust, true);
        SetFlag(s, kEflagsCarry, true);
    }
    else
    {
        SetFlag(s, kEflagsAdjust, false);
        SetFlag(s, kEflagsCarry, false);
    }
    SetAl(s, Al(s) & 0x0Fu);
    return ExecStatus::kContinue;
}

ExecStatus ExecDaa(CpuState& s, const bool subtract)
{
    const std::uint32_t old_al = Al(s);
    const bool old_cf = GetFlag(s, kEflagsCarry);
    std::uint32_t al = old_al;
    bool cf = false;
    if ((al & 0x0Fu) > 9u || GetFlag(s, kEflagsAdjust))
    {
        const std::uint32_t adjusted = subtract ? al - 6u : al + 6u;
        cf = old_cf || (adjusted & 0x100u) != 0;
        al = adjusted & 0xFFu;
        SetFlag(s, kEflagsAdjust, true);
    }
    else
    {
        SetFlag(s, kEflagsAdjust, false);
    }
    if (old_al > 0x99u || old_cf)
    {
        al = (subtract ? al - 0x60u : al + 0x60u) & 0xFFu;
        cf = true;
    }
    else if (!subtract)
    {
        cf = false;  // DAA's ELSE; DAS keeps the first condition's CF.
    }
    SetAl(s, al);
    SetFlag(s, kEflagsCarry, cf);
    SetResultFlags(s, 8, al);
    return ExecStatus::kContinue;
}

ExecStatus ExecAam(Ctx* ctx)
{
    CpuState& s = ctx->state;
    const std::uint32_t base =
        static_cast<std::uint32_t>(ctx->decoded.operands[0].imm.value.u) &
        0xFFu;
    if (base == 0)
    {
        ctx->Fault(FaultKind::kDivide,
                   s.Seg(Segment::kCs).base + s.eip, false);
        return ExecStatus::kFault;
    }
    const std::uint32_t al = Al(s);
    SetAx(s, ((al / base) << 8) | (al % base));
    SetResultFlags(s, 8, Al(s));
    return ExecStatus::kContinue;
}

ExecStatus ExecAad(Ctx* ctx)
{
    CpuState& s = ctx->state;
    const std::uint32_t base =
        static_cast<std::uint32_t>(ctx->decoded.operands[0].imm.value.u) &
        0xFFu;
    const std::uint32_t al = (Al(s) + Ah(s) * base) & 0xFFu;
    SetAx(s, al);
    SetResultFlags(s, 8, al);
    return ExecStatus::kContinue;
}

ExecStatus ExecBound(Ctx* ctx)
{
    const decode::DecodedInstruction& d = ctx->decoded;
    const ZydisDecodedOperand* const ops = d.operands;
    const unsigned width = ops[0].size;
    std::uint32_t index = 0;
    if (!ReadOperand(ctx, ops[0], &index))
    {
        return ExecStatus::kFault;
    }
    const Segment segment = SegmentOf(d, ops[1]);
    const std::uint32_t address = EffectiveAddress(*ctx, ops[1]);
    std::uint32_t lower = 0;
    std::uint32_t upper = 0;
    if (!ReadVirtual(ctx, segment, address, width, &lower) ||
        !ReadVirtual(ctx, segment,
                     (address + width / 8u) &
                         WidthMask(d.instruction.address_width),
                     width, &upper))
    {
        return ExecStatus::kFault;
    }
    const auto as_signed = [width](const std::uint32_t value) {
        return static_cast<std::int32_t>(
            (value & SignBit(width)) != 0 ? value | ~WidthMask(width)
                                          : value & WidthMask(width));
    };
    const std::int32_t i = as_signed(index);
    if (i < as_signed(lower) || i > as_signed(upper))
    {
        ctx->Fault(FaultKind::kBound,
                   ctx->state.Seg(Segment::kCs).base + ctx->state.eip,
                   false);
        return ExecStatus::kFault;
    }
    return ExecStatus::kContinue;
}

}  // namespace

ExecStatus ExecuteBcd(Ctx* ctx)
{
    CpuState& s = ctx->state;
    switch (ctx->decoded.instruction.mnemonic)
    {
        case ZYDIS_MNEMONIC_AAA: return ExecAaa(s, false);
        case ZYDIS_MNEMONIC_AAS: return ExecAaa(s, true);
        case ZYDIS_MNEMONIC_DAA: return ExecDaa(s, false);
        case ZYDIS_MNEMONIC_DAS: return ExecDaa(s, true);
        case ZYDIS_MNEMONIC_AAM: return ExecAam(ctx);
        case ZYDIS_MNEMONIC_AAD: return ExecAad(ctx);
        case ZYDIS_MNEMONIC_BOUND: return ExecBound(ctx);
        case ZYDIS_MNEMONIC_SALC:
            // Undocumented on the 386 but present: AL from CF.
            SetAl(s, GetFlag(s, kEflagsCarry) ? 0xFFu : 0x00u);
            return ExecStatus::kContinue;
        default:
            return ExecStatus::kUnimplemented;
    }
}

}  // namespace rex86::interp
