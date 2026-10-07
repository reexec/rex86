#include "interp/interpreter.h"

#include "decode/decoder.h"
#include "interp/access.h"
#include "interp/exec.h"
#include "interp/flags.h"

#include <array>
#include <cstddef>

namespace rex86::interp
{

namespace
{

constexpr std::uint32_t kPopfWritable = kEflagsPopWritable;

constexpr unsigned kMaxInstructionBytes = 15;

bool IsSegmentRegisterOperand(const ZydisDecodedOperand& operand)
{
    return operand.type == ZYDIS_OPERAND_TYPE_REGISTER &&
        operand.reg.value >= ZYDIS_REGISTER_ES &&
        operand.reg.value <= ZYDIS_REGISTER_GS;
}

const decode::Decoder& DecoderFor(const bool default_32bit)
{
    static const decode::Decoder decoder32(
        decode::Decoder::Mode::kLegacy32);
    static const decode::Decoder decoder16(
        decode::Decoder::Mode::kLegacy16);
    return default_32bit ? decoder32 : decoder16;
}


bool ConditionHolds(const ZydisMnemonic mnemonic, const CpuState& state)
{
    const bool cf = GetFlag(state, kEflagsCarry);
    const bool zf = GetFlag(state, kEflagsZero);
    const bool sf = GetFlag(state, kEflagsSign);
    const bool of = GetFlag(state, kEflagsOverflow);
    const bool pf = GetFlag(state, kEflagsParity);
    switch (mnemonic)
    {
        case ZYDIS_MNEMONIC_JO: return of;
        case ZYDIS_MNEMONIC_JNO: return !of;
        case ZYDIS_MNEMONIC_JB: return cf;
        case ZYDIS_MNEMONIC_JNB: return !cf;
        case ZYDIS_MNEMONIC_JZ: return zf;
        case ZYDIS_MNEMONIC_JNZ: return !zf;
        case ZYDIS_MNEMONIC_JBE: return cf || zf;
        case ZYDIS_MNEMONIC_JNBE: return !cf && !zf;
        case ZYDIS_MNEMONIC_JS: return sf;
        case ZYDIS_MNEMONIC_JNS: return !sf;
        case ZYDIS_MNEMONIC_JP: return pf;
        case ZYDIS_MNEMONIC_JNP: return !pf;
        case ZYDIS_MNEMONIC_JL: return sf != of;
        case ZYDIS_MNEMONIC_JNL: return sf == of;
        case ZYDIS_MNEMONIC_JLE: return zf || sf != of;
        case ZYDIS_MNEMONIC_JNLE: return !zf && sf == of;
        default: return false;
    }
}

// The ALU family sharing one shape: read both operands, compute, set
// flags, optionally write back.
ExecStatus ExecAluBinary(Ctx* ctx, const ZydisMnemonic mnemonic)
{
    const ZydisDecodedOperand& dst = ctx->decoded.operands[0];
    const ZydisDecodedOperand& src = ctx->decoded.operands[1];
    const unsigned width = dst.size;
    std::uint32_t lhs = 0;
    std::uint32_t rhs = 0;
    if (!ReadOperand(ctx, dst, &lhs) || !ReadOperand(ctx, src, &rhs))
    {
        return ExecStatus::kFault;
    }
    lhs &= WidthMask(width);
    rhs &= WidthMask(width);

    const std::uint32_t carry = GetFlag(ctx->state, kEflagsCarry) ? 1u : 0u;
    std::uint32_t result = 0;
    bool write = true;
    switch (mnemonic)
    {
        case ZYDIS_MNEMONIC_ADD:
            result = lhs + rhs;
            SetArithmeticFlags(ctx->state, width, lhs, rhs, 0, result, false);
            break;
        case ZYDIS_MNEMONIC_ADC:
            result = lhs + rhs + carry;
            SetArithmeticFlags(ctx->state, width, lhs, rhs, carry, result,
                               false);
            break;
        case ZYDIS_MNEMONIC_SUB:
            result = lhs - rhs;
            SetArithmeticFlags(ctx->state, width, lhs, rhs, 0, result, true);
            break;
        case ZYDIS_MNEMONIC_SBB:
            result = lhs - rhs - carry;
            SetArithmeticFlags(ctx->state, width, lhs, rhs, carry, result,
                               true);
            break;
        case ZYDIS_MNEMONIC_CMP:
            result = lhs - rhs;
            SetArithmeticFlags(ctx->state, width, lhs, rhs, 0, result, true);
            write = false;
            break;
        case ZYDIS_MNEMONIC_AND:
            result = lhs & rhs;
            SetLogicFlags(ctx->state, width, result);
            break;
        case ZYDIS_MNEMONIC_OR:
            result = lhs | rhs;
            SetLogicFlags(ctx->state, width, result);
            break;
        case ZYDIS_MNEMONIC_XOR:
            result = lhs ^ rhs;
            SetLogicFlags(ctx->state, width, result);
            break;
        case ZYDIS_MNEMONIC_TEST:
            result = lhs & rhs;
            SetLogicFlags(ctx->state, width, result);
            write = false;
            break;
        default:
            return ExecStatus::kUnimplemented;
    }
    if (write && !WriteOperand(ctx, dst, result & WidthMask(width)))
    {
        return ctx->faulted ? ExecStatus::kFault
                            : ExecStatus::kUnimplemented;
    }
    return ctx->faulted ? ExecStatus::kFault : ExecStatus::kContinue;
}

// One instruction's semantics, after a successful decode. next_eip is the
// fallthrough; branch semantics overwrite it. On kStop, *stop_event is
// complete apart from instructions_retired.
ExecStatus Execute(Ctx* ctx, std::uint32_t* next_eip, Event* stop_event)
{
    CpuState& s = ctx->state;
    const decode::DecodedInstruction& d = ctx->decoded;
    const ZydisMnemonic mnemonic = d.instruction.mnemonic;
    const ZydisDecodedOperand* const ops = d.operands;
    const unsigned op_width = d.instruction.operand_width;
    // A near branch target is truncated by the operand size, not by CS.D
    // (SDM JMP/Jcc/CALL/RET: 'IF OperandSize = 16 THEN EIP <- tempEIP AND
    // 0000FFFFH'); an o32 branch in 16-bit code keeps all 32 bits and the
    // CS limit check decides.
    const std::uint32_t ip_mask = op_width == 32 ? 0xFFFFFFFFu : 0xFFFFu;

    switch (mnemonic)
    {
        case ZYDIS_MNEMONIC_NOP:
            return ExecStatus::kContinue;

        case ZYDIS_MNEMONIC_MOV:
        {
            if (IsSegmentRegisterOperand(ops[0]))
            {
                return ExecuteSegments(ctx, next_eip);
            }
            std::uint32_t value = 0;
            if (!ReadOperand(ctx, ops[1], &value))
            {
                return ctx->faulted ? ExecStatus::kFault
                                    : ExecStatus::kUnimplemented;
            }
            if (!WriteOperand(ctx, ops[0], value & WidthMask(ops[0].size)))
            {
                return ctx->faulted ? ExecStatus::kFault
                                    : ExecStatus::kUnimplemented;
            }
            return ExecStatus::kContinue;
        }
        case ZYDIS_MNEMONIC_MOVZX:
        case ZYDIS_MNEMONIC_MOVSX:
        {
            std::uint32_t value = 0;
            if (!ReadOperand(ctx, ops[1], &value))
            {
                return ExecStatus::kFault;
            }
            value &= WidthMask(ops[1].size);
            if (mnemonic == ZYDIS_MNEMONIC_MOVSX &&
                (value & SignBit(ops[1].size)) != 0)
            {
                value |= ~WidthMask(ops[1].size);
            }
            WriteOperand(ctx, ops[0], value & WidthMask(ops[0].size));
            return ctx->faulted ? ExecStatus::kFault : ExecStatus::kContinue;
        }
        case ZYDIS_MNEMONIC_LEA:
            WriteOperand(ctx, ops[0],
                         EffectiveAddress(*ctx, ops[1]) &
                             WidthMask(ops[0].size));
            return ExecStatus::kContinue;
        case ZYDIS_MNEMONIC_XCHG:
        {
            std::uint32_t a = 0;
            std::uint32_t b = 0;
            if (!ReadOperand(ctx, ops[0], &a) || !ReadOperand(ctx, ops[1], &b))
            {
                return ExecStatus::kFault;
            }
            if (!WriteOperand(ctx, ops[0], b) || !WriteOperand(ctx, ops[1], a))
            {
                return ctx->faulted ? ExecStatus::kFault
                                    : ExecStatus::kUnimplemented;
            }
            return ExecStatus::kContinue;
        }

        case ZYDIS_MNEMONIC_ADD:
        case ZYDIS_MNEMONIC_ADC:
        case ZYDIS_MNEMONIC_SUB:
        case ZYDIS_MNEMONIC_SBB:
        case ZYDIS_MNEMONIC_CMP:
        case ZYDIS_MNEMONIC_AND:
        case ZYDIS_MNEMONIC_OR:
        case ZYDIS_MNEMONIC_XOR:
        case ZYDIS_MNEMONIC_TEST:
            return ExecAluBinary(ctx, mnemonic);

        case ZYDIS_MNEMONIC_INC:
        case ZYDIS_MNEMONIC_DEC:
        {
            const unsigned width = ops[0].size;
            std::uint32_t value = 0;
            if (!ReadOperand(ctx, ops[0], &value))
            {
                return ExecStatus::kFault;
            }
            value &= WidthMask(width);
            const bool inc = mnemonic == ZYDIS_MNEMONIC_INC;
            const std::uint32_t result = inc ? value + 1u : value - 1u;
            SetArithmeticFlags(ctx->state, width, value, 1u, 0, result, !inc,
                               /*set_carry=*/false);
            WriteOperand(ctx, ops[0], result & WidthMask(width));
            return ctx->faulted ? ExecStatus::kFault : ExecStatus::kContinue;
        }
        case ZYDIS_MNEMONIC_NEG:
        {
            const unsigned width = ops[0].size;
            std::uint32_t value = 0;
            if (!ReadOperand(ctx, ops[0], &value))
            {
                return ExecStatus::kFault;
            }
            value &= WidthMask(width);
            const std::uint32_t result = 0u - value;
            SetArithmeticFlags(ctx->state, width, 0, value, 0, result, true);
            WriteOperand(ctx, ops[0], result & WidthMask(width));
            return ctx->faulted ? ExecStatus::kFault : ExecStatus::kContinue;
        }
        case ZYDIS_MNEMONIC_NOT:
        {
            std::uint32_t value = 0;
            if (!ReadOperand(ctx, ops[0], &value))
            {
                return ExecStatus::kFault;
            }
            WriteOperand(ctx, ops[0], ~value & WidthMask(ops[0].size));
            return ctx->faulted ? ExecStatus::kFault : ExecStatus::kContinue;
        }

        case ZYDIS_MNEMONIC_PUSH:
        {
            if (IsSegmentRegisterOperand(ops[0]))
            {
                return ExecuteSegments(ctx, next_eip);
            }
            std::uint32_t value = 0;
            if (!ReadOperand(ctx, ops[0], &value))
            {
                return ctx->faulted ? ExecStatus::kFault
                                    : ExecStatus::kUnimplemented;
            }
            Push(ctx, op_width, value & WidthMask(op_width));
            return ctx->faulted ? ExecStatus::kFault : ExecStatus::kContinue;
        }
        case ZYDIS_MNEMONIC_POP:
        {
            if (IsSegmentRegisterOperand(ops[0]))
            {
                return ExecuteSegments(ctx, next_eip);
            }
            std::uint32_t value = 0;
            if (!Pop(ctx, op_width, &value))
            {
                return ExecStatus::kFault;
            }
            if (!WriteOperand(ctx, ops[0], value))
            {
                return ctx->faulted ? ExecStatus::kFault
                                    : ExecStatus::kUnimplemented;
            }
            return ExecStatus::kContinue;
        }
        case ZYDIS_MNEMONIC_PUSHFD:
        case ZYDIS_MNEMONIC_PUSHF:
            Push(ctx, op_width, s.eflags & (op_width == 16 ? 0xFFFFu
                                                           : 0x00FCFFFFu));
            return ctx->faulted ? ExecStatus::kFault : ExecStatus::kContinue;
        case ZYDIS_MNEMONIC_POPFD:
        case ZYDIS_MNEMONIC_POPF:
        {
            std::uint32_t value = 0;
            if (!Pop(ctx, op_width, &value))
            {
                return ExecStatus::kFault;
            }
            const std::uint32_t mask =
                op_width == 16 ? (kPopfWritable & 0xFFFFu) : kPopfWritable;
            s.eflags = ((s.eflags & ~mask) | (value & mask)) |
                kEflagsReserved1;
            return ExecStatus::kContinue;
        }
        case ZYDIS_MNEMONIC_PUSHAD:
        case ZYDIS_MNEMONIC_PUSHA:
        {
            // The image is the SDM's (EAX highest, EDI lowest), but the
            // stores go from the lowest slot up, as the 386EX does: a
            // limit fault partway leaves the lower slots written
            // (SingleStepTests, design #17). SP moves only on success.
            const std::uint32_t original_sp = s.Get(Gpr::kEsp);
            const std::uint32_t sp_mask =
                s.Seg(Segment::kSs).default_32bit ? 0xFFFFFFFFu : 0xFFFFu;
            const std::uint32_t bytes = op_width / 8u;
            static constexpr Gpr kLowestFirst[] = {
                Gpr::kEdi, Gpr::kEsi, Gpr::kEbp, Gpr::kEsp,
                Gpr::kEbx, Gpr::kEdx, Gpr::kEcx, Gpr::kEax};
            std::uint32_t slot = original_sp - 8u * bytes;
            for (const Gpr reg : kLowestFirst)
            {
                if (!WriteVirtual(ctx, Segment::kSs, slot & sp_mask, op_width,
                                  s.Get(reg) & WidthMask(op_width)))
                {
                    return ExecStatus::kFault;
                }
                slot += bytes;
            }
            s.Set(Gpr::kEsp, ((original_sp - 8u * bytes) & sp_mask) |
                                 (original_sp & ~sp_mask));
            return ExecStatus::kContinue;
        }
        case ZYDIS_MNEMONIC_POPAD:
        case ZYDIS_MNEMONIC_POPA:
        {
            static constexpr Gpr kOrder[] = {Gpr::kEdi, Gpr::kEsi, Gpr::kEbp,
                                             Gpr::kEsp, Gpr::kEbx, Gpr::kEdx,
                                             Gpr::kEcx, Gpr::kEax};
            for (const Gpr reg : kOrder)
            {
                std::uint32_t value = 0;
                if (!Pop(ctx, op_width, &value))
                {
                    return ExecStatus::kFault;
                }
                if (reg == Gpr::kEsp)
                {
                    // The SDM says the stored stack pointer is discarded,
                    // but the 386EX (SingleStepTests) shows POPAD loading
                    // the bits the running SP does not govern: with a
                    // 16-bit stack, ESP's high half takes the image while
                    // the low half keeps the incremented SP.
                    const std::uint32_t sp_mask =
                        s.Seg(Segment::kSs).default_32bit ? 0xFFFFFFFFu
                                                          : 0xFFFFu;
                    if (op_width == 32)
                    {
                        s.Set(Gpr::kEsp, (value & ~sp_mask) |
                                             (s.Get(Gpr::kEsp) & sp_mask));
                    }
                    continue;
                }
                if (op_width == 16)
                {
                    s.Set(reg, (s.Get(reg) & 0xFFFF0000u) | (value & 0xFFFFu));
                }
                else
                {
                    s.Set(reg, value);
                }
            }
            return ExecStatus::kContinue;
        }

        case ZYDIS_MNEMONIC_JO: case ZYDIS_MNEMONIC_JNO:
        case ZYDIS_MNEMONIC_JB: case ZYDIS_MNEMONIC_JNB:
        case ZYDIS_MNEMONIC_JZ: case ZYDIS_MNEMONIC_JNZ:
        case ZYDIS_MNEMONIC_JBE: case ZYDIS_MNEMONIC_JNBE:
        case ZYDIS_MNEMONIC_JS: case ZYDIS_MNEMONIC_JNS:
        case ZYDIS_MNEMONIC_JP: case ZYDIS_MNEMONIC_JNP:
        case ZYDIS_MNEMONIC_JL: case ZYDIS_MNEMONIC_JNL:
        case ZYDIS_MNEMONIC_JLE: case ZYDIS_MNEMONIC_JNLE:
        {
            std::uint32_t target = 0;
            if (ConditionHolds(mnemonic, s) && d.DirectTarget(&target))
            {
                *next_eip = target & ip_mask;
            }
            return ExecStatus::kContinue;
        }
        case ZYDIS_MNEMONIC_JCXZ:
        case ZYDIS_MNEMONIC_JECXZ:
        {
            const std::uint32_t count = mnemonic == ZYDIS_MNEMONIC_JCXZ
                ? (s.Get(Gpr::kEcx) & 0xFFFFu)
                : s.Get(Gpr::kEcx);
            std::uint32_t target = 0;
            if (count == 0 && d.DirectTarget(&target))
            {
                *next_eip = target & ip_mask;
            }
            return ExecStatus::kContinue;
        }
        case ZYDIS_MNEMONIC_LOOP:
        case ZYDIS_MNEMONIC_LOOPE:
        case ZYDIS_MNEMONIC_LOOPNE:
        {
            const bool count32 = d.instruction.address_width == 32;
            std::uint32_t count =
                count32 ? s.Get(Gpr::kEcx) : (s.Get(Gpr::kEcx) & 0xFFFFu);
            --count;
            count &= count32 ? 0xFFFFFFFFu : 0xFFFFu;
            if (count32)
            {
                s.Set(Gpr::kEcx, count);
            }
            else
            {
                s.Set(Gpr::kEcx,
                      (s.Get(Gpr::kEcx) & 0xFFFF0000u) | count);
            }
            bool take = count != 0;
            if (mnemonic == ZYDIS_MNEMONIC_LOOPE)
            {
                take = take && GetFlag(s, kEflagsZero);
            }
            else if (mnemonic == ZYDIS_MNEMONIC_LOOPNE)
            {
                take = take && !GetFlag(s, kEflagsZero);
            }
            std::uint32_t target = 0;
            if (take && d.DirectTarget(&target))
            {
                *next_eip = target & ip_mask;
            }
            return ExecStatus::kContinue;
        }
        case ZYDIS_MNEMONIC_JMP:
        {
            if (d.instruction.meta.branch_type == ZYDIS_BRANCH_TYPE_FAR)
            {
                return ExecuteSegments(ctx, next_eip);
            }
            std::uint32_t target = 0;
            if (d.DirectTarget(&target))
            {
                *next_eip = target & ip_mask;
                return ExecStatus::kContinue;
            }
            if (ops[0].type == ZYDIS_OPERAND_TYPE_POINTER)
            {
                return ExecStatus::kUnimplemented;  // far jmp
            }
            if (!ReadOperand(ctx, ops[0], &target))
            {
                return ctx->faulted ? ExecStatus::kFault
                                    : ExecStatus::kUnimplemented;
            }
            *next_eip = target & WidthMask(op_width) & ip_mask;
            return ExecStatus::kContinue;
        }
        case ZYDIS_MNEMONIC_CALL:
        {
            if (d.instruction.meta.branch_type == ZYDIS_BRANCH_TYPE_FAR)
            {
                return ExecuteSegments(ctx, next_eip);
            }
            std::uint32_t target = 0;
            if (d.DirectTarget(&target))
            {
                if (!CheckBranchTarget(ctx, target & ip_mask) ||
                    !Push(ctx, op_width, *next_eip & WidthMask(op_width)))
                {
                    return ExecStatus::kFault;
                }
                *next_eip = target & ip_mask;
                return ExecStatus::kContinue;
            }
            if (ops[0].type == ZYDIS_OPERAND_TYPE_POINTER)
            {
                return ExecStatus::kUnimplemented;  // far call
            }
            if (!ReadOperand(ctx, ops[0], &target))
            {
                return ctx->faulted ? ExecStatus::kFault
                                    : ExecStatus::kUnimplemented;
            }
            if (!CheckBranchTarget(ctx,
                                   target & WidthMask(op_width) & ip_mask) ||
                !Push(ctx, op_width, *next_eip & WidthMask(op_width)))
            {
                return ExecStatus::kFault;
            }
            *next_eip = target & WidthMask(op_width) & ip_mask;
            return ExecStatus::kContinue;
        }
        case ZYDIS_MNEMONIC_RET:
        {
            if (d.instruction.meta.branch_type == ZYDIS_BRANCH_TYPE_FAR)
            {
                return ExecuteSegments(ctx, next_eip);
            }
            std::uint32_t target = 0;
            if (!Pop(ctx, op_width, &target))
            {
                return ExecStatus::kFault;
            }
            if (d.instruction.operand_count_visible > 0 &&
                ops[0].type == ZYDIS_OPERAND_TYPE_IMMEDIATE)
            {
                const SegmentRegister& ss = s.Seg(Segment::kSs);
                const std::uint32_t sp_mask =
                    ss.default_32bit ? 0xFFFFFFFFu : 0xFFFFu;
                const std::uint32_t sp = s.Get(Gpr::kEsp);
                const std::uint32_t adjusted =
                    (sp + static_cast<std::uint32_t>(ops[0].imm.value.u)) &
                    sp_mask;
                s.Set(Gpr::kEsp, adjusted | (sp & ~sp_mask));
            }
            *next_eip = target & WidthMask(op_width) & ip_mask;
            return ExecStatus::kContinue;
        }

        case ZYDIS_MNEMONIC_CLC: SetFlag(s, kEflagsCarry, false); return ExecStatus::kContinue;
        case ZYDIS_MNEMONIC_STC: SetFlag(s, kEflagsCarry, true); return ExecStatus::kContinue;
        case ZYDIS_MNEMONIC_CMC:
            SetFlag(s, kEflagsCarry, !GetFlag(s, kEflagsCarry));
            return ExecStatus::kContinue;
        case ZYDIS_MNEMONIC_CLD: SetFlag(s, kEflagsDirection, false); return ExecStatus::kContinue;
        case ZYDIS_MNEMONIC_STD: SetFlag(s, kEflagsDirection, true); return ExecStatus::kContinue;
        case ZYDIS_MNEMONIC_CLI: SetFlag(s, kEflagsInterrupt, false); return ExecStatus::kContinue;
        case ZYDIS_MNEMONIC_STI:
            // An STI that enables interrupts delays their delivery by one
            // instruction (the `sti; hlt` idiom).
            if (!GetFlag(s, kEflagsInterrupt))
            {
                ctx->inhibit_interrupts = true;
            }
            SetFlag(s, kEflagsInterrupt, true);
            return ExecStatus::kContinue;
        case ZYDIS_MNEMONIC_LAHF:
            WriteGpr(s, ZYDIS_REGISTER_AH,
                     (s.eflags & 0xD5u) | kEflagsReserved1);
            return ExecStatus::kContinue;
        case ZYDIS_MNEMONIC_SAHF:
        {
            const std::uint32_t ah = ReadGpr(s, ZYDIS_REGISTER_AH);
            s.eflags = (s.eflags & ~0xD5u) | (ah & 0xD5u) | kEflagsReserved1;
            return ExecStatus::kContinue;
        }

        case ZYDIS_MNEMONIC_CBW:
            WriteGpr(s, ZYDIS_REGISTER_AX,
                     static_cast<std::uint32_t>(static_cast<std::int8_t>(
                         ReadGpr(s, ZYDIS_REGISTER_AL))) &
                         0xFFFFu);
            return ExecStatus::kContinue;
        case ZYDIS_MNEMONIC_CWDE:
            s.Set(Gpr::kEax,
                  static_cast<std::uint32_t>(static_cast<std::int16_t>(
                      ReadGpr(s, ZYDIS_REGISTER_AX))));
            return ExecStatus::kContinue;
        case ZYDIS_MNEMONIC_CWD:
            WriteGpr(s, ZYDIS_REGISTER_DX,
                     (ReadGpr(s, ZYDIS_REGISTER_AX) & 0x8000u) != 0 ? 0xFFFFu
                                                                    : 0u);
            return ExecStatus::kContinue;
        case ZYDIS_MNEMONIC_CDQ:
            s.Set(Gpr::kEdx,
                  (s.Get(Gpr::kEax) & 0x80000000u) != 0 ? 0xFFFFFFFFu : 0u);
            return ExecStatus::kContinue;

        case ZYDIS_MNEMONIC_HLT:
            stop_event->reason = StopReason::kHalted;
            return ExecStatus::kStop;
        case ZYDIS_MNEMONIC_INT:
        case ZYDIS_MNEMONIC_INT3:
        case ZYDIS_MNEMONIC_INT1:
        case ZYDIS_MNEMONIC_INTO:
        {
            std::uint8_t vector = 0;
            if (mnemonic == ZYDIS_MNEMONIC_INT)
            {
                vector = static_cast<std::uint8_t>(ops[0].imm.value.u);
            }
            else if (mnemonic == ZYDIS_MNEMONIC_INT3)
            {
                vector = 3;
            }
            else if (mnemonic == ZYDIS_MNEMONIC_INT1)
            {
                vector = 1;
            }
            else
            {
                if (!GetFlag(s, kEflagsOverflow))
                {
                    return ExecStatus::kContinue;  // INTO without OF
                }
                vector = 4;
            }
            stop_event->reason = StopReason::kSoftwareInterrupt;
            stop_event->vector = vector;
            return ExecStatus::kStop;
        }

        case ZYDIS_MNEMONIC_IN:
        {
            const unsigned width = ops[0].size;
            const std::uint16_t port = static_cast<std::uint16_t>(
                ops[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE
                    ? ops[1].imm.value.u
                    : ReadGpr(s, ZYDIS_REGISTER_DX));
            std::uint32_t value = 0;
            if (ctx->environment.PortRead(
                    port, static_cast<std::uint8_t>(width / 8u), &value))
            {
                WriteOperand(ctx, ops[0], value & WidthMask(width));
                return ExecStatus::kContinue;
            }
            stop_event->reason = StopReason::kPortIo;
            stop_event->port = port;
            stop_event->port_width = static_cast<std::uint8_t>(width / 8u);
            stop_event->port_is_write = false;
            return ExecStatus::kStop;
        }
        case ZYDIS_MNEMONIC_OUT:
        {
            const unsigned width = ops[1].size;
            const std::uint16_t port = static_cast<std::uint16_t>(
                ops[0].type == ZYDIS_OPERAND_TYPE_IMMEDIATE
                    ? ops[0].imm.value.u
                    : ReadGpr(s, ZYDIS_REGISTER_DX));
            std::uint32_t value = 0;
            ReadOperand(ctx, ops[1], &value);
            value &= WidthMask(width);
            if (ctx->environment.PortWrite(
                    port, static_cast<std::uint8_t>(width / 8u), value))
            {
                return ExecStatus::kContinue;
            }
            stop_event->reason = StopReason::kPortIo;
            stop_event->port = port;
            stop_event->port_width = static_cast<std::uint8_t>(width / 8u);
            stop_event->port_is_write = true;
            stop_event->port_value = value;
            return ExecStatus::kStop;
        }

        case ZYDIS_MNEMONIC_RDTSC:
        {
            const std::uint64_t tsc = ctx->environment.ReadTimeStampCounter();
            s.Set(Gpr::kEax, static_cast<std::uint32_t>(tsc));
            s.Set(Gpr::kEdx, static_cast<std::uint32_t>(tsc >> 32));
            return ExecStatus::kContinue;
        }
        case ZYDIS_MNEMONIC_CPUID:
        {
            std::uint32_t registers[4] = {};
            ctx->environment.Cpuid(s.Get(Gpr::kEax), s.Get(Gpr::kEcx),
                                   registers);
            s.Set(Gpr::kEax, registers[0]);
            s.Set(Gpr::kEbx, registers[1]);
            s.Set(Gpr::kEcx, registers[2]);
            s.Set(Gpr::kEdx, registers[3]);
            return ExecStatus::kContinue;
        }

        default:
            return ExecuteExtended(ctx, next_eip, stop_event);
    }
}

}  // namespace

StepResult Step(CpuState& state, GuestMemory& memory,
                Environment& environment, const Features& features)
{
    StepResult result;
    const SegmentRegister& cs = state.Seg(Segment::kCs);
    const std::uint32_t start_offset = state.eip;

    // Fetch up to 15 bytes, stopping early at the segment limit or an
    // unfetchable page. At least one byte must be fetchable. EIP is not
    // wrapped here: sequential execution past a 16-bit segment's 0xFFFF
    // leaves EIP at 0x10000 on real hardware, and the next fetch is what
    // faults.
    std::uint8_t bytes[kMaxInstructionBytes] = {};
    unsigned fetched = 0;
    bool stopped_at_limit = false;
    for (; fetched < kMaxInstructionBytes; ++fetched)
    {
        const std::uint32_t offset = start_offset + fetched;
        if (!cs.IsFlat() && (!cs.present || offset > cs.limit))
        {
            stopped_at_limit = true;
            break;
        }
        const std::uint32_t linear = cs.base + offset;
        if (!memory.pages().AllHave(linear, 1, kPageReadExecute) ||
            !memory.Read8(linear, &bytes[fetched]))
        {
            break;
        }
    }
    // An instruction that does not fit below the CS limit is #GP (SDM); a
    // fetch cut short by page attributes is the host's access violation.
    if (fetched == 0)
    {
        result.status = StepStatus::kFaulted;
        result.event.reason = StopReason::kFault;
        result.event.fault_kind = stopped_at_limit
            ? FaultKind::kGeneralProtection
            : FaultKind::kAccessViolation;
        result.event.fault_address = cs.base + start_offset;
        result.event.fault_on_fetch = true;
        return result;
    }

    decode::DecodedInstruction decoded;
    bool truncated = false;
    if (!DecoderFor(cs.default_32bit)
             .Decode(bytes, fetched, state.eip, &decoded, &truncated))
    {
        result.status = StepStatus::kFaulted;
        result.event.reason = StopReason::kFault;
        result.event.fault_kind = FaultKind::kIllegalInstruction;
        if (stopped_at_limit && truncated)
        {
            result.event.fault_kind = FaultKind::kGeneralProtection;
            result.event.fault_on_fetch = true;
        }
        result.event.fault_address = cs.base + start_offset;
        return result;
    }

    // A disabled feature's instruction is an illegal instruction, per the
    // Features contract. RTM (XBEGIN/XABORT, which reuse the C7/C6 /7
    // encodings) is never part of this core's CPU and raises #UD as on
    // every processor without it.
    const ZydisISASet isa = decoded.instruction.meta.isa_set;
    if (isa == ZYDIS_ISA_SET_RTM ||
        ((isa == ZYDIS_ISA_SET_X87 || isa == ZYDIS_ISA_SET_FCMOV ||
          isa == ZYDIS_ISA_SET_FCOMI) &&
         !features.x87) ||
        (isa == ZYDIS_ISA_SET_PENTIUMMMX && !features.mmx) ||
        (isa == ZYDIS_ISA_SET_SSE && !features.sse) ||
        ((isa == ZYDIS_ISA_SET_SSE2 || isa == ZYDIS_ISA_SET_SSE2MMX) &&
         !features.sse2))
    {
        result.status = StepStatus::kFaulted;
        result.event.reason = StopReason::kFault;
        result.event.fault_kind = FaultKind::kIllegalInstruction;
        result.event.fault_address = cs.base + start_offset;
        return result;
    }

    // Faults are precise: the integer state the instruction started from
    // comes back on a fault (design #17, decision 6), so semantics code
    // need not unwind registers it changed before a later access failed.
    // Memory stores already made stay, as on the hardware.
    const std::array<std::uint32_t, 8> saved_gpr = state.gpr;
    const std::uint32_t saved_eflags = state.eflags;
    const std::array<SegmentRegister, 6> saved_segments = state.segments;

    Ctx ctx{state, memory, environment, decoded, Event{}, false};
    const std::uint32_t fallthrough = start_offset + decoded.Length();
    std::uint32_t next_eip = fallthrough;
    Event stop_event;
    ExecStatus status = Execute(&ctx, &next_eip, &stop_event);

    // A taken branch must land within the (possibly new) CS limit, or the
    // branch itself faults (SDM: JMP, CALL, RET, IRET). Sequential flow
    // past the limit is left to the next fetch.
    if (status == ExecStatus::kContinue)
    {
        const decode::ControlFlow flow = decoded.Flow();
        const bool branch = flow != decode::ControlFlow::kNone &&
            flow != decode::ControlFlow::kHalt &&
            flow != decode::ControlFlow::kSoftwareInterrupt;
        const SegmentRegister& new_cs = state.Seg(Segment::kCs);
        if (branch &&
            (next_eip != fallthrough ||
             new_cs.selector !=
                 saved_segments[static_cast<std::size_t>(Segment::kCs)]
                     .selector) &&
            !CheckBranchTarget(&ctx, next_eip))
        {
            status = ExecStatus::kFault;
        }
    }
    if (status == ExecStatus::kFault && !ctx.keep_partial_state)
    {
        state.gpr = saved_gpr;
        state.eflags = saved_eflags;
        state.segments = saved_segments;
    }

    result.inhibit_interrupts = ctx.inhibit_interrupts;
    switch (status)
    {
        case ExecStatus::kContinue:
            state.eip = next_eip;
            result.status = StepStatus::kRetired;
            return result;
        case ExecStatus::kStop:
            state.eip = next_eip;
            result.status = StepStatus::kRetiredAndStopped;
            result.event = stop_event;
            return result;
        case ExecStatus::kStopNoRetire:
            // A restartable stop (declined string port I/O): EIP still
            // addresses the instruction.
            result.status = StepStatus::kStopped;
            result.event = stop_event;
            return result;
        case ExecStatus::kFault:
            // The instruction did not retire; EIP still addresses it.
            result.status = StepStatus::kFaulted;
            result.event = ctx.fault;
            return result;
        case ExecStatus::kUnimplemented:
        default:
            result.status = StepStatus::kUnimplemented;
            result.event.reason = StopReason::kFault;
            result.event.fault_kind = FaultKind::kIllegalInstruction;
            result.event.fault_address = cs.base + start_offset;
            return result;
    }
}

bool EnterInterrupt(CpuState& state, GuestMemory& memory,
                    Environment& environment, const std::uint16_t target_cs,
                    const std::uint32_t target_eip, Event* fault_event)
{
    // A synthetic decoded instruction only to satisfy Ctx; the frame push
    // uses the stack width SS.D selects, not an instruction's.
    static const decode::DecodedInstruction kNone{};
    Ctx ctx{state, memory, environment, kNone, Event{}, false};
    const unsigned width =
        state.Seg(Segment::kCs).default_32bit ? 32u : 16u;

    if (!Push(&ctx, width, state.eflags & (width == 16 ? 0xFFFFu
                                                       : 0x00FCFFFFu)) ||
        !Push(&ctx, width, state.Seg(Segment::kCs).selector) ||
        !Push(&ctx, width, state.eip & WidthMask(width)))
    {
        *fault_event = ctx.fault;
        return false;
    }

    if (target_cs != state.Seg(Segment::kCs).selector)
    {
        Descriptor descriptor;
        if (!environment.LoadDescriptor(target_cs, &descriptor))
        {
            fault_event->reason = StopReason::kFault;
            fault_event->fault_kind = FaultKind::kGeneralProtection;
            fault_event->fault_address = target_cs;
            return false;
        }
        SegmentRegister& cs = state.Seg(Segment::kCs);
        cs.selector = target_cs;
        cs.base = descriptor.base;
        cs.limit = descriptor.limit;
        cs.present = descriptor.present;
        cs.executable = true;
        cs.writable = descriptor.writable;
        cs.default_32bit = descriptor.default_32bit;
    }
    state.eflags &= ~(kEflagsInterrupt | kEflagsTrap);
    state.eip = target_eip;
    return true;
}

}  // namespace rex86::interp
