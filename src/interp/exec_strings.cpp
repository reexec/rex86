// Interpreter increment 2 (#13): the string instructions and their REP
// prefixes. Iterations complete inside one Step (budgeted as one
// instruction, keeping #11's loop contract), and a fault mid-string
// leaves the architectural partial state -- updated index and count
// registers with EIP still at the instruction -- which is exactly the
// resumable state the hardware leaves.

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

ExecStatus ExecuteStrings(Ctx* ctx)
{
    CpuState& s = ctx->state;
    const decode::DecodedInstruction& d = ctx->decoded;
    const ZydisMnemonic mnemonic = d.instruction.mnemonic;

    bool is_movs = false;
    bool is_stos = false;
    bool is_lods = false;
    bool is_scas = false;
    bool is_cmps = false;
    switch (mnemonic)
    {
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

    while (true)
    {
        if (has_rep && ReadIndex(s, Gpr::kEcx, address_mask) == 0)
        {
            break;
        }

        if (is_movs)
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
            if (!ReadVirtual(ctx, source_segment,
                             ReadIndex(s, Gpr::kEsi, address_mask), width,
                             &lhs) ||
                !ReadVirtual(ctx, Segment::kEs,
                             ReadIndex(s, Gpr::kEdi, address_mask), width,
                             &rhs))
            {
                return ExecStatus::kFault;
            }
            CompareFlags(s, width, lhs, rhs);
            AdvanceIndex(s, Gpr::kEsi, address_mask, bytes);
            AdvanceIndex(s, Gpr::kEdi, address_mask, bytes);
        }

        if (!has_rep)
        {
            break;
        }
        WriteIndex(s, Gpr::kEcx, address_mask,
                   ReadIndex(s, Gpr::kEcx, address_mask) - 1u);
        if ((is_scas || is_cmps) &&
            GetFlag(s, kEflagsZero) != repe)
        {
            break;
        }
    }
    return ExecStatus::kContinue;
}

}  // namespace rex86::interp
