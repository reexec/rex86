// The integer instructions after the 386 that the target boards' CPUs
// carry (design #21, decision 1; design #22, decision 6): the 486's BSWAP,
// XADD and CMPXCHG, the Pentium's CMPXCHG8B, the P6's CMOVcc, and the
// architected invalid opcodes UD0/UD1/UD2. Semantics follow the Intel SDM
// pseudocode; the integer host-comparison fuzz is their measurement.

#include "interp/exec.h"
#include "interp/flags.h"

namespace rex86::interp
{
namespace
{

ExecStatus Done(const Ctx& ctx)
{
    return ctx.faulted ? ExecStatus::kFault : ExecStatus::kContinue;
}

ZydisRegister Accumulator(const unsigned width_bits)
{
    switch (width_bits)
    {
        case 8: return ZYDIS_REGISTER_AL;
        case 16: return ZYDIS_REGISTER_AX;
        default: return ZYDIS_REGISTER_EAX;
    }
}

// CMOVcc reads its source whatever the condition, so an inaccessible
// memory source faults even when nothing moves (SDM CMOVcc); a false
// condition leaves the destination untouched in 32-bit code.
ExecStatus ExecCmov(Ctx* ctx)
{
    const ZydisDecodedOperand* ops = ctx->decoded.operands;
    std::uint32_t source = 0;
    if (!ReadOperand(ctx, ops[1], &source))
    {
        return ExecStatus::kFault;
    }
    if (ConditionCodeHolds(ctx->decoded.instruction.opcode & 0xFu, ctx->state))
    {
        WriteOperand(ctx, ops[0], source);
    }
    return Done(*ctx);
}

// The SDM leaves BSWAP of a 16-bit register undefined. The core clears the
// low word, which is what P6 and later processors do; the comparison
// masks the result.
ExecStatus ExecBswap(Ctx* ctx)
{
    const ZydisDecodedOperand& operand = ctx->decoded.operands[0];
    const std::uint32_t value = ReadGpr(ctx->state, operand.reg.value);
    if (operand.size == 16)
    {
        WriteGpr(ctx->state, operand.reg.value, 0);
        return ExecStatus::kContinue;
    }
    const std::uint32_t swapped = (value >> 24) | ((value >> 8) & 0x0000FF00u) |
                                  ((value << 8) & 0x00FF0000u) | (value << 24);
    WriteGpr(ctx->state, operand.reg.value, swapped);
    return ExecStatus::kContinue;
}

// TEMP := SRC + DEST; SRC := DEST; DEST := TEMP. With the same register on
// both sides the destination's write lands last, as in the SDM.
ExecStatus ExecXadd(Ctx* ctx)
{
    const ZydisDecodedOperand* ops = ctx->decoded.operands;
    const unsigned width = ops[0].size;
    std::uint32_t destination = 0;
    std::uint32_t source = 0;
    if (!ReadOperand(ctx, ops[0], &destination) || !ReadOperand(ctx, ops[1], &source))
    {
        return ExecStatus::kFault;
    }
    const std::uint32_t sum = (destination + source) & WidthMask(width);
    if (ops[0].type == ZYDIS_OPERAND_TYPE_MEMORY)
    {
        // The store goes first: the source register may be part of the
        // address (XADD [ecx], ch), which the CPU computes once.
        if (!WriteOperand(ctx, ops[0], sum))
        {
            return ExecStatus::kFault;
        }
        WriteGpr(ctx->state, ops[1].reg.value, destination);
    }
    else
    {
        WriteGpr(ctx->state, ops[1].reg.value, destination);
        WriteGpr(ctx->state, ops[0].reg.value, sum);
    }
    SetArithmeticFlags(ctx->state, width, destination, source, 0, sum, false);
    return ExecStatus::kContinue;
}

// The destination always receives a write cycle (SDM CMPXCHG): the source
// when the compare succeeds, its own value when it fails, so a read-only
// destination faults either way.
ExecStatus ExecCmpxchg(Ctx* ctx)
{
    const ZydisDecodedOperand* ops = ctx->decoded.operands;
    const unsigned width = ops[0].size;
    const ZydisRegister accumulator = Accumulator(width);
    std::uint32_t destination = 0;
    std::uint32_t source = 0;
    if (!ReadOperand(ctx, ops[0], &destination) || !ReadOperand(ctx, ops[1], &source))
    {
        return ExecStatus::kFault;
    }
    const std::uint32_t acc = ReadGpr(ctx->state, accumulator) & WidthMask(width);
    const bool equal = acc == destination;
    if (!WriteOperand(ctx, ops[0], equal ? source : destination))
    {
        return ExecStatus::kFault;
    }
    if (!equal)
    {
        WriteGpr(ctx->state, accumulator, destination);
    }
    SetArithmeticFlags(ctx->state, width, acc, destination, 0,
                       (acc - destination) & WidthMask(width), true);
    return ExecStatus::kContinue;
}

// Compares EDX:EAX with m64. Equal: ZF=1 and m64 := ECX:EBX. Otherwise
// ZF=0, EDX:EAX := m64 and m64 is written back unchanged (SDM). Only ZF
// changes.
ExecStatus ExecCmpxchg8b(Ctx* ctx)
{
    const ZydisDecodedOperand& operand = ctx->decoded.operands[0];
    if (operand.type != ZYDIS_OPERAND_TYPE_MEMORY)
    {
        ctx->Fault(FaultKind::kIllegalInstruction,
                   ctx->state.Seg(Segment::kCs).base + ctx->decoded.guest_address,
                   false);
        return ExecStatus::kFault;
    }
    const Segment segment = SegmentOf(ctx->decoded, operand);
    const std::uint32_t offset = EffectiveAddress(*ctx, operand);
    const std::uint32_t offset_high =
        (offset + 4u) & WidthMask(ctx->decoded.instruction.address_width);
    std::uint32_t low = 0;
    std::uint32_t high = 0;
    if (!RequireWritable(ctx, segment, offset) ||
        !ReadVirtual(ctx, segment, offset, 32, &low) ||
        !ReadVirtual(ctx, segment, offset_high, 32, &high))
    {
        return ExecStatus::kFault;
    }
    CpuState& s = ctx->state;
    const bool equal = low == s.Get(Gpr::kEax) && high == s.Get(Gpr::kEdx);
    const std::uint32_t new_low = equal ? s.Get(Gpr::kEbx) : low;
    const std::uint32_t new_high = equal ? s.Get(Gpr::kEcx) : high;
    if (!WriteVirtual(ctx, segment, offset, 32, new_low) ||
        !WriteVirtual(ctx, segment, offset_high, 32, new_high))
    {
        return ExecStatus::kFault;
    }
    if (!equal)
    {
        s.Set(Gpr::kEax, low);
        s.Set(Gpr::kEdx, high);
    }
    SetFlag(s, kEflagsZero, equal);
    return ExecStatus::kContinue;
}

}  // namespace

ExecStatus ExecutePost386(Ctx* ctx)
{
    switch (ctx->decoded.instruction.mnemonic)
    {
        case ZYDIS_MNEMONIC_CMOVO: case ZYDIS_MNEMONIC_CMOVNO:
        case ZYDIS_MNEMONIC_CMOVB: case ZYDIS_MNEMONIC_CMOVNB:
        case ZYDIS_MNEMONIC_CMOVZ: case ZYDIS_MNEMONIC_CMOVNZ:
        case ZYDIS_MNEMONIC_CMOVBE: case ZYDIS_MNEMONIC_CMOVNBE:
        case ZYDIS_MNEMONIC_CMOVS: case ZYDIS_MNEMONIC_CMOVNS:
        case ZYDIS_MNEMONIC_CMOVP: case ZYDIS_MNEMONIC_CMOVNP:
        case ZYDIS_MNEMONIC_CMOVL: case ZYDIS_MNEMONIC_CMOVNL:
        case ZYDIS_MNEMONIC_CMOVLE: case ZYDIS_MNEMONIC_CMOVNLE:
            return ExecCmov(ctx);
        case ZYDIS_MNEMONIC_BSWAP:
            return ExecBswap(ctx);
        case ZYDIS_MNEMONIC_XADD:
            return ExecXadd(ctx);
        case ZYDIS_MNEMONIC_CMPXCHG:
            return ExecCmpxchg(ctx);
        case ZYDIS_MNEMONIC_CMPXCHG8B:
            return ExecCmpxchg8b(ctx);
        case ZYDIS_MNEMONIC_UD0:
        case ZYDIS_MNEMONIC_UD1:
        case ZYDIS_MNEMONIC_UD2:
            ctx->Fault(FaultKind::kIllegalInstruction,
                       ctx->state.Seg(Segment::kCs).base + ctx->decoded.guest_address,
                       false);
            return ExecStatus::kFault;
        default:
            return ExecStatus::kUnimplemented;
    }
}

}  // namespace rex86::interp
