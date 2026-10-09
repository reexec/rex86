// Interpreter increments 2-3 (#13, #15): the string instructions, string
// port I/O, and their REP prefixes. Each iteration is one step of the
// budget (design #32), as each is one trap-flag single step on the
// hardware. A REP stops between iterations when its step allowance is used
// up or attention is raised, and a fault mid-string stops it too; both
// leave the architectural partial state -- updated index and count
// registers with EIP still at the instruction -- which is exactly the
// resumable state the hardware leaves at an interrupt or exception.

#include "interp/exec.h"
#include "interp/flags.h"

namespace rex86::interp
{

namespace
{

std::uint32_t ReadIndex(const CpuState& state, const Gpr reg,
                        const std::uint32_t address_mask)
{
    return state.Get(reg) & address_mask;
}

void WriteIndex(CpuState& state, const Gpr reg,
                const std::uint32_t address_mask, const std::uint32_t value)
{
    state.Set(reg, (state.Get(reg) & ~address_mask) | (value & address_mask));
}

void AdvanceIndex(CpuState& state, const Gpr reg,
                  const std::uint32_t address_mask, const unsigned bytes)
{
    const std::uint32_t delta = GetFlag(state, kEflagsDirection)
        ? static_cast<std::uint32_t>(0) - bytes
        : bytes;
    WriteIndex(state, reg, address_mask,
               ReadIndex(state, reg, address_mask) + delta);
}

// Subtraction flags without a destination write: CMPS and SCAS.
void CompareFlags(CpuState& state, const unsigned width,
                  const std::uint32_t lhs, const std::uint32_t rhs)
{
    SetArithmeticFlags(state, width, lhs, rhs, 0, lhs - rhs, true);
}

}  // namespace

ExecStatus ExecuteStrings(Ctx* ctx, Event* stop_event)
{
    CpuState& s = ctx->state;
    const decode::DecodedInstruction& d = ctx->decoded;
    const ZydisMnemonic mnemonic = d.instruction.mnemonic;

    bool is_movs = false;
    bool is_stos = false;
    bool is_lods = false;
    bool is_scas = false;
    bool is_cmps = false;
    bool is_ins = false;
    bool is_outs = false;
    switch (mnemonic)
    {
        case ZYDIS_MNEMONIC_INSB: case ZYDIS_MNEMONIC_INSW:
        case ZYDIS_MNEMONIC_INSD: is_ins = true; break;
        case ZYDIS_MNEMONIC_OUTSB: case ZYDIS_MNEMONIC_OUTSW:
        case ZYDIS_MNEMONIC_OUTSD: is_outs = true; break;
        case ZYDIS_MNEMONIC_MOVSB: case ZYDIS_MNEMONIC_MOVSW:
        case ZYDIS_MNEMONIC_MOVSD: is_movs = true; break;
        case ZYDIS_MNEMONIC_STOSB: case ZYDIS_MNEMONIC_STOSW:
        case ZYDIS_MNEMONIC_STOSD: is_stos = true; break;
        case ZYDIS_MNEMONIC_LODSB: case ZYDIS_MNEMONIC_LODSW:
        case ZYDIS_MNEMONIC_LODSD: is_lods = true; break;
        case ZYDIS_MNEMONIC_SCASB: case ZYDIS_MNEMONIC_SCASW:
        case ZYDIS_MNEMONIC_SCASD: is_scas = true; break;
        case ZYDIS_MNEMONIC_CMPSB: case ZYDIS_MNEMONIC_CMPSW:
        case ZYDIS_MNEMONIC_CMPSD: is_cmps = true; break;
        default:
            return ExecStatus::kUnimplemented;
    }

    const unsigned width = d.instruction.operand_width;
    const unsigned bytes = width / 8u;
    const std::uint32_t address_mask =
        WidthMask(d.instruction.address_width);

    // The source segment honors overrides, ES included ("es movsb" reads
    // from ES:SI); the ES destination cannot be overridden. The source
    // operand is the one addressed through (E)SI.
    Segment source_segment = Segment::kDs;
    for (ZyanU8 index = 0; index < d.instruction.operand_count; ++index)
    {
        const ZydisDecodedOperand& operand = d.operands[index];
        if (operand.type == ZYDIS_OPERAND_TYPE_MEMORY &&
            (operand.mem.base == ZYDIS_REGISTER_SI ||
             operand.mem.base == ZYDIS_REGISTER_ESI))
        {
            source_segment = SegmentOf(d, operand);
            break;
        }
    }

    const bool has_rep =
        (d.instruction.attributes &
         (ZYDIS_ATTRIB_HAS_REP | ZYDIS_ATTRIB_HAS_REPE |
          ZYDIS_ATTRIB_HAS_REPNE)) != 0;
    const bool repe = (d.instruction.attributes &
                       (ZYDIS_ATTRIB_HAS_REP | ZYDIS_ATTRIB_HAS_REPE)) != 0;

    ctx->keep_partial_state = has_rep;
    // Completed iterations, counted as they finish and reported on every way
    // out, so that a fault or a declined port says how many steps became
    // architectural (design #32).
    struct StepReport
    {
        StepBudget* budget;
        std::uint64_t steps = 0;
        ~StepReport()
        {
            if (budget != nullptr)
            {
                budget->string_steps = steps;
            }
        }
    } report{ctx->budget};
    while (true)
    {
        if (has_rep && ReadIndex(s, Gpr::kEcx, address_mask) == 0)
        {
            break;
        }

        if (is_ins || is_outs)
        {
            // A declined port access stops before this iteration without
            // retiring; completed iterations stay architectural, so
            // resuming re-executes with the remaining count (design #15,
            // decision 4).
            const std::uint16_t port =
                static_cast<std::uint16_t>(s.Get(Gpr::kEdx) & 0xFFFFu);
            const std::uint8_t port_width =
                static_cast<std::uint8_t>(bytes);
            if (is_ins)
            {
                std::uint32_t value = 0;
                if (!ctx->environment.PortRead(port, port_width, &value))
                {
                    stop_event->reason = StopReason::kPortIo;
                    stop_event->port = port;
                    stop_event->port_width = port_width;
                    stop_event->port_is_write = false;
                    return ExecStatus::kStopNoRetire;
                }
                if (!WriteVirtual(ctx, Segment::kEs,
                                  ReadIndex(s, Gpr::kEdi, address_mask),
                                  width, value & WidthMask(width)))
                {
                    return ExecStatus::kFault;
                }
                AdvanceIndex(s, Gpr::kEdi, address_mask, bytes);
            }
            else
            {
                std::uint32_t value = 0;
                if (!ReadVirtual(ctx, source_segment,
                                 ReadIndex(s, Gpr::kEsi, address_mask),
                                 width, &value))
                {
                    return ExecStatus::kFault;
                }
                if (!ctx->environment.PortWrite(port, port_width, value))
                {
                    stop_event->reason = StopReason::kPortIo;
                    stop_event->port = port;
                    stop_event->port_width = port_width;
                    stop_event->port_is_write = true;
                    stop_event->port_value = value;
                    return ExecStatus::kStopNoRetire;
                }
                AdvanceIndex(s, Gpr::kEsi, address_mask, bytes);
            }
        }
        else if (is_movs)
        {
            std::uint32_t value = 0;
            if (!ReadVirtual(ctx, source_segment,
                             ReadIndex(s, Gpr::kEsi, address_mask), width,
                             &value) ||
                !WriteVirtual(ctx, Segment::kEs,
                              ReadIndex(s, Gpr::kEdi, address_mask), width,
                              value))
            {
                return ExecStatus::kFault;
            }
            AdvanceIndex(s, Gpr::kEsi, address_mask, bytes);
            AdvanceIndex(s, Gpr::kEdi, address_mask, bytes);
        }
        else if (is_stos)
        {
            const std::uint32_t value =
                s.Get(Gpr::kEax) & WidthMask(width);
            if (!WriteVirtual(ctx, Segment::kEs,
                              ReadIndex(s, Gpr::kEdi, address_mask), width,
                              value))
            {
                return ExecStatus::kFault;
            }
            AdvanceIndex(s, Gpr::kEdi, address_mask, bytes);
        }
        else if (is_lods)
        {
            std::uint32_t value = 0;
            if (!ReadVirtual(ctx, source_segment,
                             ReadIndex(s, Gpr::kEsi, address_mask), width,
                             &value))
            {
                return ExecStatus::kFault;
            }
            if (width == 8)
            {
                WriteGpr(s, ZYDIS_REGISTER_AL, value);
            }
            else if (width == 16)
            {
                WriteGpr(s, ZYDIS_REGISTER_AX, value);
            }
            else
            {
                s.Set(Gpr::kEax, value);
            }
            AdvanceIndex(s, Gpr::kEsi, address_mask, bytes);
        }
        else if (is_scas)
        {
            std::uint32_t value = 0;
            if (!ReadVirtual(ctx, Segment::kEs,
                             ReadIndex(s, Gpr::kEdi, address_mask), width,
                             &value))
            {
                return ExecStatus::kFault;
            }
            CompareFlags(s, width, s.Get(Gpr::kEax) & WidthMask(width),
                         value);
            AdvanceIndex(s, Gpr::kEdi, address_mask, bytes);
        }
        else  // cmps
        {
            std::uint32_t lhs = 0;
            std::uint32_t rhs = 0;
            // ES:(E)DI is read first: when both sides fault, the host CPU
            // reports the destination (measured by the integer host fuzz).
            if (!ReadVirtual(ctx, Segment::kEs,
                             ReadIndex(s, Gpr::kEdi, address_mask), width,
                             &rhs) ||
                !ReadVirtual(ctx, source_segment,
                             ReadIndex(s, Gpr::kEsi, address_mask), width,
                             &lhs))
            {
                return ExecStatus::kFault;
            }
            CompareFlags(s, width, lhs, rhs);
            AdvanceIndex(s, Gpr::kEsi, address_mask, bytes);
            AdvanceIndex(s, Gpr::kEdi, address_mask, bytes);
        }

        ++report.steps;
        if (!has_rep)
        {
            break;
        }
        const std::uint32_t remaining = ReadIndex(s, Gpr::kEcx, address_mask) - 1u;
        WriteIndex(s, Gpr::kEcx, address_mask, remaining);
        if (remaining == 0 ||
            ((is_scas || is_cmps) && GetFlag(s, kEflagsZero) != repe))
        {
            break;
        }
        // The instruction is not finished: stop at this iteration boundary
        // when the budget or the Cpu's loop needs it (design #32).
        const StepBudget* budget = ctx->budget;
        if (budget != nullptr &&
            (report.steps >= budget->allowance ||
             (budget->attention != nullptr &&
              budget->attention->load(std::memory_order_relaxed))))
        {
            return ExecStatus::kPartial;
        }
    }
    // A REP that ran no iteration still retires, as one step.
    if (report.steps == 0)
    {
        report.steps = 1;
    }
    return ExecStatus::kContinue;
}

}  // namespace rex86::interp
