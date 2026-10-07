// Interpreter increment 3 (#15): segment-register loads, far control
// flow at a single privilege level, IRET and the privileged
// instructions a user-mode core refuses. Every segment load goes through
// LoadSegment, so the host's LoadDescriptor decides what a selector means
// (design #15, decisions 1 and 3).

#include "interp/exec.h"
#include "interp/flags.h"

namespace rex86::interp
{

namespace
{

bool IsSegmentRegister(const ZydisDecodedOperand& operand)
{
    return operand.type == ZYDIS_OPERAND_TYPE_REGISTER &&
        operand.reg.value >= ZYDIS_REGISTER_ES &&
        operand.reg.value <= ZYDIS_REGISTER_GS;
}

Segment SegmentRegisterOf(const ZydisDecodedOperand& operand)
{
    static constexpr Segment kOrder[] = {Segment::kEs, Segment::kCs,
                                         Segment::kSs, Segment::kDs,
                                         Segment::kFs, Segment::kGs};
    return kOrder[operand.reg.value - ZYDIS_REGISTER_ES];
}

std::uint32_t StackPointerMask(const CpuState& state)
{
    return state.Seg(Segment::kSs).default_32bit ? 0xFFFFFFFFu : 0xFFFFu;
}

// Reads a far pointer m16:16 or m16:32 -- the offset first, then the
// selector after it.
bool ReadFarPointer(Ctx* ctx, const ZydisDecodedOperand& operand,
                    const unsigned offset_width, std::uint32_t* offset,
                    std::uint16_t* selector)
{
    const decode::DecodedInstruction& d = ctx->decoded;
    const Segment segment = SegmentOf(d, operand);
    const std::uint32_t address = EffectiveAddress(*ctx, operand);
    std::uint32_t raw_selector = 0;
    if (!ReadVirtual(ctx, segment, address, offset_width, offset) ||
        !ReadVirtual(ctx, segment,
                     (address + offset_width / 8u) &
                         WidthMask(d.instruction.address_width),
                     16, &raw_selector))
    {
        return false;
    }
    *selector = static_cast<std::uint16_t>(raw_selector);
    return true;
}

// The target of a far JMP/CALL: an immediate ptr16:16/32 or a memory
// m16:16/32.
bool ReadFarTarget(Ctx* ctx, const unsigned width, std::uint32_t* offset,
                   std::uint16_t* selector)
{
    const ZydisDecodedOperand& operand = ctx->decoded.operands[0];
    if (operand.type == ZYDIS_OPERAND_TYPE_POINTER)
    {
        *offset = operand.ptr.offset & WidthMask(width);
        *selector = operand.ptr.segment;
        return true;
    }
    return ReadFarPointer(ctx, operand, width, offset, selector);
}

ExecStatus Done(const Ctx& ctx)
{
    return ctx.faulted ? ExecStatus::kFault : ExecStatus::kContinue;
}

ExecStatus ExecMoveToSegment(Ctx* ctx)
{
    const ZydisDecodedOperand* const ops = ctx->decoded.operands;
    const Segment target = SegmentRegisterOf(ops[0]);
    if (target == Segment::kCs)
    {
        // MOV CS is #UD on IA-32.
        ctx->Fault(FaultKind::kIllegalInstruction,
                   ctx->state.Seg(Segment::kCs).base + ctx->state.eip,
                   false);
        return ExecStatus::kFault;
    }
    std::uint32_t value = 0;
    if (!ReadOperand(ctx, ops[1], &value))
    {
        return ExecStatus::kFault;
    }
    if (!LoadSegment(ctx, target, static_cast<std::uint16_t>(value)))
    {
        return ExecStatus::kFault;
    }
    if (target == Segment::kSs)
    {
        ctx->inhibit_interrupts = true;
    }
    return ExecStatus::kContinue;
}

ExecStatus ExecPushSegment(Ctx* ctx)
{
    const ZydisDecodedOperand& operand = ctx->decoded.operands[0];
    const unsigned width = ctx->decoded.instruction.operand_width;
    const std::uint32_t selector =
        ctx->state.Seg(SegmentRegisterOf(operand)).selector;
    Push(ctx, width, selector);
    return Done(*ctx);
}

ExecStatus ExecPopSegment(Ctx* ctx)
{
    const ZydisDecodedOperand& operand = ctx->decoded.operands[0];
    const unsigned width = ctx->decoded.instruction.operand_width;
    const Segment target = SegmentRegisterOf(operand);
    // A 32-bit POP sreg reads only the selector word and then moves the
    // stack pointer by four: the 386EX takes no limit fault for the
    // upper word (SingleStepTests, design #17), and the SDM discards it.
    CpuState& s = ctx->state;
    const std::uint32_t sp_mask = StackPointerMask(s);
    const std::uint32_t sp = s.Get(Gpr::kEsp);
    std::uint32_t value = 0;
    if (!ReadVirtual(ctx, Segment::kSs, sp & sp_mask, 16, &value) ||
        !LoadSegment(ctx, target, static_cast<std::uint16_t>(value)))
    {
        return ExecStatus::kFault;
    }
    s.Set(Gpr::kEsp, ((sp + width / 8u) & sp_mask) | (sp & ~sp_mask));
    if (target == Segment::kSs)
    {
        ctx->inhibit_interrupts = true;
    }
    return ExecStatus::kContinue;
}

// LDS/LES/LFS/LGS/LSS: the segment loads first, so a refused selector
// leaves the destination register unchanged.
ExecStatus ExecLoadFarPointer(Ctx* ctx, const Segment target)
{
    const ZydisDecodedOperand* const ops = ctx->decoded.operands;
    const unsigned width = ops[0].size;
    std::uint32_t offset = 0;
    std::uint16_t selector = 0;
    if (!ReadFarPointer(ctx, ops[1], width, &offset, &selector) ||
        !LoadSegment(ctx, target, selector))
    {
        return ExecStatus::kFault;
    }
    WriteOperand(ctx, ops[0], offset & WidthMask(width));
    return Done(*ctx);
}

ExecStatus ExecFarJump(Ctx* ctx, std::uint32_t* next_eip)
{
    const unsigned width = ctx->decoded.instruction.operand_width;
    std::uint32_t offset = 0;
    std::uint16_t selector = 0;
    if (!ReadFarTarget(ctx, width, &offset, &selector) ||
        !LoadSegment(ctx, Segment::kCs, selector))
    {
        return ExecStatus::kFault;
    }
    *next_eip = offset;
    return ExecStatus::kContinue;
}

ExecStatus ExecFarCall(Ctx* ctx, std::uint32_t* next_eip)
{
    CpuState& s = ctx->state;
    const unsigned width = ctx->decoded.instruction.operand_width;
    std::uint32_t offset = 0;
    std::uint16_t selector = 0;
    if (!ReadFarTarget(ctx, width, &offset, &selector))
    {
        return ExecStatus::kFault;
    }
    const std::uint32_t saved_esp = s.Get(Gpr::kEsp);
    const std::uint32_t return_cs = s.Seg(Segment::kCs).selector;
    if (!Push(ctx, width, return_cs) ||
        !Push(ctx, width, *next_eip & WidthMask(width)))
    {
        s.Set(Gpr::kEsp, saved_esp);
        return ExecStatus::kFault;
    }
    if (!LoadSegment(ctx, Segment::kCs, selector))
    {
        s.Set(Gpr::kEsp, saved_esp);
        return ExecStatus::kFault;
    }
    *next_eip = offset;
    return ExecStatus::kContinue;
}

ExecStatus ExecFarReturn(Ctx* ctx, std::uint32_t* next_eip)
{
    CpuState& s = ctx->state;
    const decode::DecodedInstruction& d = ctx->decoded;
    const unsigned width = d.instruction.operand_width;
    const std::uint32_t saved_esp = s.Get(Gpr::kEsp);
    std::uint32_t offset = 0;
    std::uint32_t selector = 0;
    if (!Pop(ctx, width, &offset) || !Pop(ctx, width, &selector))
    {
        s.Set(Gpr::kEsp, saved_esp);
        return ExecStatus::kFault;
    }
    if (!LoadSegment(ctx, Segment::kCs,
                     static_cast<std::uint16_t>(selector)))
    {
        s.Set(Gpr::kEsp, saved_esp);
        return ExecStatus::kFault;
    }
    if (d.instruction.operand_count_visible > 0 &&
        d.operands[0].type == ZYDIS_OPERAND_TYPE_IMMEDIATE)
    {
        const std::uint32_t sp_mask = StackPointerMask(s);
        const std::uint32_t sp = s.Get(Gpr::kEsp);
        s.Set(Gpr::kEsp,
              ((sp + static_cast<std::uint32_t>(d.operands[0].imm.value.u)) &
               sp_mask) |
                  (sp & ~sp_mask));
    }
    *next_eip = offset & WidthMask(width);
    return ExecStatus::kContinue;
}

ExecStatus ExecInterruptReturn(Ctx* ctx, std::uint32_t* next_eip)
{
    CpuState& s = ctx->state;
    const unsigned width = ctx->decoded.instruction.operand_width;
    const std::uint32_t saved_esp = s.Get(Gpr::kEsp);
    std::uint32_t offset = 0;
    std::uint32_t selector = 0;
    std::uint32_t flags = 0;
    if (!Pop(ctx, width, &offset) || !Pop(ctx, width, &selector) ||
        !Pop(ctx, width, &flags))
    {
        s.Set(Gpr::kEsp, saved_esp);
        return ExecStatus::kFault;
    }
    if (!LoadSegment(ctx, Segment::kCs,
                     static_cast<std::uint16_t>(selector)))
    {
        s.Set(Gpr::kEsp, saved_esp);
        return ExecStatus::kFault;
    }
    const std::uint32_t mask = width == 16 ? (kEflagsPopWritable & 0xFFFFu)
                                           : kEflagsPopWritable;
    s.eflags = ((s.eflags & ~mask) | (flags & mask)) | kEflagsReserved1;
    *next_eip = offset & WidthMask(width);
    return ExecStatus::kContinue;
}

}  // namespace

ExecStatus ExecuteSegments(Ctx* ctx, std::uint32_t* next_eip)
{
    const decode::DecodedInstruction& d = ctx->decoded;
    const ZydisDecodedOperand* const ops = d.operands;
    const bool far = d.instruction.meta.branch_type == ZYDIS_BRANCH_TYPE_FAR;

    switch (d.instruction.mnemonic)
    {
        case ZYDIS_MNEMONIC_MOV:
            if (IsSegmentRegister(ops[0]))
            {
                return ExecMoveToSegment(ctx);
            }
            return ExecStatus::kUnimplemented;
        case ZYDIS_MNEMONIC_PUSH:
            return IsSegmentRegister(ops[0]) ? ExecPushSegment(ctx)
                                             : ExecStatus::kUnimplemented;
        case ZYDIS_MNEMONIC_POP:
            return IsSegmentRegister(ops[0]) ? ExecPopSegment(ctx)
                                             : ExecStatus::kUnimplemented;
        case ZYDIS_MNEMONIC_LDS: return ExecLoadFarPointer(ctx, Segment::kDs);
        case ZYDIS_MNEMONIC_LES: return ExecLoadFarPointer(ctx, Segment::kEs);
        case ZYDIS_MNEMONIC_LFS: return ExecLoadFarPointer(ctx, Segment::kFs);
        case ZYDIS_MNEMONIC_LGS: return ExecLoadFarPointer(ctx, Segment::kGs);
        case ZYDIS_MNEMONIC_LSS: return ExecLoadFarPointer(ctx, Segment::kSs);
        case ZYDIS_MNEMONIC_JMP:
            return far ? ExecFarJump(ctx, next_eip)
                       : ExecStatus::kUnimplemented;
        case ZYDIS_MNEMONIC_CALL:
            return far ? ExecFarCall(ctx, next_eip)
                       : ExecStatus::kUnimplemented;
        case ZYDIS_MNEMONIC_RET:
            return far ? ExecFarReturn(ctx, next_eip)
                       : ExecStatus::kUnimplemented;
        case ZYDIS_MNEMONIC_IRET:
        case ZYDIS_MNEMONIC_IRETD:
            return ExecInterruptReturn(ctx, next_eip);
        case ZYDIS_MNEMONIC_CLTS:
        case ZYDIS_MNEMONIC_LGDT:
        case ZYDIS_MNEMONIC_LIDT:
        case ZYDIS_MNEMONIC_LLDT:
        case ZYDIS_MNEMONIC_LTR:
        case ZYDIS_MNEMONIC_LMSW:
        case ZYDIS_MNEMONIC_INVD:
        case ZYDIS_MNEMONIC_WBINVD:
        case ZYDIS_MNEMONIC_INVLPG:
            // System instructions: a user-mode core refuses them.
            ctx->Fault(FaultKind::kPrivilegedInstruction,
                       ctx->state.Seg(Segment::kCs).base + ctx->state.eip,
                       false);
            return ExecStatus::kFault;
        default:
            return ExecStatus::kUnimplemented;
    }
}

}  // namespace rex86::interp
