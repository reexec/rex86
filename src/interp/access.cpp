#include "interp/access.h"

#include "interp/flags.h"

namespace rex86::interp
{

namespace
{

struct GprSlot
{
    Gpr reg;
    unsigned width_bits;
    bool high_byte;
};

// Maps a Zydis general-purpose register id onto the register file. Returns
// false for anything that is not a GPR (segment registers, EIP, ...).
bool SlotOf(const ZydisRegister reg, GprSlot* slot)
{
    switch (reg)
    {
        case ZYDIS_REGISTER_AL: *slot = {Gpr::kEax, 8, false}; return true;
        case ZYDIS_REGISTER_CL: *slot = {Gpr::kEcx, 8, false}; return true;
        case ZYDIS_REGISTER_DL: *slot = {Gpr::kEdx, 8, false}; return true;
        case ZYDIS_REGISTER_BL: *slot = {Gpr::kEbx, 8, false}; return true;
        case ZYDIS_REGISTER_AH: *slot = {Gpr::kEax, 8, true}; return true;
        case ZYDIS_REGISTER_CH: *slot = {Gpr::kEcx, 8, true}; return true;
        case ZYDIS_REGISTER_DH: *slot = {Gpr::kEdx, 8, true}; return true;
        case ZYDIS_REGISTER_BH: *slot = {Gpr::kEbx, 8, true}; return true;
        case ZYDIS_REGISTER_AX: *slot = {Gpr::kEax, 16, false}; return true;
        case ZYDIS_REGISTER_CX: *slot = {Gpr::kEcx, 16, false}; return true;
        case ZYDIS_REGISTER_DX: *slot = {Gpr::kEdx, 16, false}; return true;
        case ZYDIS_REGISTER_BX: *slot = {Gpr::kEbx, 16, false}; return true;
        case ZYDIS_REGISTER_SP: *slot = {Gpr::kEsp, 16, false}; return true;
        case ZYDIS_REGISTER_BP: *slot = {Gpr::kEbp, 16, false}; return true;
        case ZYDIS_REGISTER_SI: *slot = {Gpr::kEsi, 16, false}; return true;
        case ZYDIS_REGISTER_DI: *slot = {Gpr::kEdi, 16, false}; return true;
        case ZYDIS_REGISTER_EAX: *slot = {Gpr::kEax, 32, false}; return true;
        case ZYDIS_REGISTER_ECX: *slot = {Gpr::kEcx, 32, false}; return true;
        case ZYDIS_REGISTER_EDX: *slot = {Gpr::kEdx, 32, false}; return true;
        case ZYDIS_REGISTER_EBX: *slot = {Gpr::kEbx, 32, false}; return true;
        case ZYDIS_REGISTER_ESP: *slot = {Gpr::kEsp, 32, false}; return true;
        case ZYDIS_REGISTER_EBP: *slot = {Gpr::kEbp, 32, false}; return true;
        case ZYDIS_REGISTER_ESI: *slot = {Gpr::kEsi, 32, false}; return true;
        case ZYDIS_REGISTER_EDI: *slot = {Gpr::kEdi, 32, false}; return true;
        default: return false;
    }
}

}  // namespace

void Ctx::Fault(const FaultKind kind, const std::uint32_t address,
                const bool on_write)
{
    if (faulted)
    {
        return;
    }
    faulted = true;
    fault = Event{};
    fault.reason = StopReason::kFault;
    fault.fault_kind = kind;
    fault.fault_address = address;
    fault.fault_on_write = on_write;
}

std::uint32_t ReadGpr(const CpuState& state, const ZydisRegister reg)
{
    GprSlot slot{};
    if (!SlotOf(reg, &slot))
    {
        return 0;
    }
    const std::uint32_t value = state.Get(slot.reg);
    if (slot.width_bits == 32)
    {
        return value;
    }
    if (slot.high_byte)
    {
        return (value >> 8) & 0xFFu;
    }
    return value & WidthMask(slot.width_bits);
}

void WriteGpr(CpuState& state, const ZydisRegister reg,
              const std::uint32_t value)
{
    GprSlot slot{};
    if (!SlotOf(reg, &slot))
    {
        return;
    }
    const std::uint32_t old = state.Get(slot.reg);
    if (slot.width_bits == 32)
    {
        state.Set(slot.reg, value);
    }
    else if (slot.high_byte)
    {
        state.Set(slot.reg, (old & 0xFFFF00FFu) | ((value & 0xFFu) << 8));
    }
    else if (slot.width_bits == 16)
    {
        state.Set(slot.reg, (old & 0xFFFF0000u) | (value & 0xFFFFu));
    }
    else
    {
        state.Set(slot.reg, (old & 0xFFFFFF00u) | (value & 0xFFu));
    }
}

Segment SegmentOf(const decode::DecodedInstruction& decoded,
                  const ZydisDecodedOperand& operand)
{
    // The last segment prefix wins on IA-32. Zydis resolves overrides into
    // operand.mem.segment for most instructions, but reads 0x3E before an
    // indirect CALL/JMP as the CET notrack hint; the guests this core runs
    // predate CET, where 0x3E is always a DS override.
    bool overridden = false;
    Segment override_segment = Segment::kDs;
    const auto& raw = decoded.instruction.raw;
    for (ZyanU8 index = 0; index < raw.prefix_count; ++index)
    {
        switch (raw.prefixes[index].value)
        {
            case 0x26: override_segment = Segment::kEs; overridden = true; break;
            case 0x2E: override_segment = Segment::kCs; overridden = true; break;
            case 0x36: override_segment = Segment::kSs; overridden = true; break;
            case 0x3E: override_segment = Segment::kDs; overridden = true; break;
            case 0x64: override_segment = Segment::kFs; overridden = true; break;
            case 0x65: override_segment = Segment::kGs; overridden = true; break;
            default: break;
        }
    }
    if (overridden)
    {
        return override_segment;
    }
    switch (operand.mem.segment)
    {
        case ZYDIS_REGISTER_ES: return Segment::kEs;
        case ZYDIS_REGISTER_CS: return Segment::kCs;
        case ZYDIS_REGISTER_SS: return Segment::kSs;
        case ZYDIS_REGISTER_FS: return Segment::kFs;
        case ZYDIS_REGISTER_GS: return Segment::kGs;
        case ZYDIS_REGISTER_DS:
        default: return Segment::kDs;
    }
}

std::uint32_t EffectiveAddress(const Ctx& ctx,
                               const ZydisDecodedOperand& operand)
{
    std::uint32_t address =
        static_cast<std::uint32_t>(operand.mem.disp.value);
    if (operand.mem.base != ZYDIS_REGISTER_NONE)
    {
        address += ReadGpr(ctx.state, operand.mem.base);
    }
    if (operand.mem.index != ZYDIS_REGISTER_NONE)
    {
        address += ReadGpr(ctx.state, operand.mem.index) *
            static_cast<std::uint32_t>(operand.mem.scale);
    }
    // The effective address wraps at the instruction's address width: a
    // 16-bit addressed access never leaves the low 64 KiB of the segment.
    return address & WidthMask(ctx.decoded.instruction.address_width);
}

namespace
{

// Shared by ReadVirtual/WriteVirtual: the limit check and the linear
// address. The flat 4 GiB segment is the fast path and can never fail
// the limit.
bool Linearize(Ctx* ctx, const Segment segment, const std::uint32_t offset,
               const unsigned width_bits, const bool on_write,
               std::uint32_t* linear)
{
    const SegmentRegister& seg = ctx->state.Seg(segment);
    const std::uint32_t bytes = width_bits / 8u;
    if (!seg.IsFlat())
    {
        if (!seg.present || offset > seg.limit ||
            seg.limit - offset < bytes - 1u)
        {
            ctx->Fault(FaultKind::kGeneralProtection, offset, on_write);
            return false;
        }
    }
    *linear = seg.base + offset;
    return true;
}

}  // namespace

bool ReadVirtual(Ctx* ctx, const Segment segment, const std::uint32_t offset,
                 const unsigned width_bits, std::uint32_t* value)
{
    if (ctx->faulted)
    {
        return false;
    }
    std::uint32_t linear = 0;
    if (!Linearize(ctx, segment, offset, width_bits, false, &linear))
    {
        return false;
    }
    bool ok = false;
    if (width_bits == 8)
    {
        std::uint8_t byte = 0;
        ok = ctx->memory.Read8(linear, &byte);
        *value = byte;
    }
    else if (width_bits == 16)
    {
        std::uint16_t word = 0;
        ok = ctx->memory.Read16(linear, &word);
        *value = word;
    }
    else
    {
        ok = ctx->memory.Read32(linear, value);
    }
    if (!ok)
    {
        ctx->Fault(FaultKind::kAccessViolation, linear, false);
    }
    return ok;
}

bool WriteVirtual(Ctx* ctx, const Segment segment, const std::uint32_t offset,
                  const unsigned width_bits, const std::uint32_t value)
{
    if (ctx->faulted)
    {
        return false;
    }
    std::uint32_t linear = 0;
    if (!Linearize(ctx, segment, offset, width_bits, true, &linear))
    {
        return false;
    }
    // Self-modifying code: a store into translated code invalidates the
    // translation before the store lands (goal 2's SMC bar). The
    // interpreter itself has no stale blocks, but keeping the page table
    // truthful here is what the translation backends will rely on.
    const std::uint32_t bytes = width_bits / 8u;
    for (std::uint32_t page = linear & ~(kGuestPageSize - 1u);
         page <= ((linear + bytes - 1u) & ~(kGuestPageSize - 1u));
         page += kGuestPageSize)
    {
        if (Has(ctx->memory.pages().Get(page), PageFlag::kTranslated))
        {
            ctx->memory.pages().Remove(page, 1, PageFlag::kTranslated);
            ctx->environment.OnCodePageWritten(page);
        }
        if (page > 0xFFFFFFFFu - kGuestPageSize)
        {
            break;
        }
    }
    bool ok = false;
    if (width_bits == 8)
    {
        ok = ctx->memory.Write8(linear, static_cast<std::uint8_t>(value));
    }
    else if (width_bits == 16)
    {
        ok = ctx->memory.Write16(linear, static_cast<std::uint16_t>(value));
    }
    else
    {
        ok = ctx->memory.Write32(linear, value);
    }
    if (!ok)
    {
        ctx->Fault(FaultKind::kAccessViolation, linear, true);
    }
    return ok;
}

bool ReadOperand(Ctx* ctx, const ZydisDecodedOperand& operand,
                 std::uint32_t* value)
{
    switch (operand.type)
    {
        case ZYDIS_OPERAND_TYPE_REGISTER:
        {
            GprSlot slot{};
            if (SlotOf(operand.reg.value, &slot))
            {
                *value = ReadGpr(ctx->state, operand.reg.value);
                return true;
            }
            // Reading a segment register (mov r, sreg; push sreg) yields
            // the selector.
            switch (operand.reg.value)
            {
                case ZYDIS_REGISTER_ES:
                case ZYDIS_REGISTER_CS:
                case ZYDIS_REGISTER_SS:
                case ZYDIS_REGISTER_DS:
                case ZYDIS_REGISTER_FS:
                case ZYDIS_REGISTER_GS:
                {
                    static constexpr Segment kOrder[] = {
                        Segment::kEs, Segment::kCs, Segment::kSs,
                        Segment::kDs, Segment::kFs, Segment::kGs};
                    *value = ctx->state
                                 .Seg(kOrder[operand.reg.value -
                                              ZYDIS_REGISTER_ES])
                                 .selector;
                    return true;
                }
                default:
                    return false;
            }
        }
        case ZYDIS_OPERAND_TYPE_MEMORY:
            return ReadVirtual(ctx, SegmentOf(ctx->decoded, operand),
                               EffectiveAddress(*ctx, operand), operand.size,
                               value);
        case ZYDIS_OPERAND_TYPE_IMMEDIATE:
            *value = static_cast<std::uint32_t>(operand.imm.value.u) &
                WidthMask(operand.size);
            if (operand.imm.is_signed)
            {
                // Sign-extend to the operand's width mask; callers mask to
                // their destination width.
                *value = static_cast<std::uint32_t>(
                             static_cast<std::int64_t>(operand.imm.value.s)) &
                    0xFFFFFFFFu;
            }
            return true;
        default:
            return false;
    }
}

bool WriteOperand(Ctx* ctx, const ZydisDecodedOperand& operand,
                  const std::uint32_t value)
{
    switch (operand.type)
    {
        case ZYDIS_OPERAND_TYPE_REGISTER:
        {
            GprSlot slot{};
            if (!SlotOf(operand.reg.value, &slot))
            {
                return false;
            }
            WriteGpr(ctx->state, operand.reg.value, value);
            return true;
        }
        case ZYDIS_OPERAND_TYPE_MEMORY:
            return WriteVirtual(ctx, SegmentOf(ctx->decoded, operand),
                                EffectiveAddress(*ctx, operand),
                                operand.size, value);
        default:
            return false;
    }
}

bool LoadSegment(Ctx* ctx, const Segment segment, const std::uint16_t selector)
{
    if (ctx->faulted)
    {
        return false;
    }
    Descriptor descriptor;
    if (!ctx->environment.LoadDescriptor(selector, &descriptor) ||
        (segment == Segment::kCs &&
         !(descriptor.present && descriptor.executable)) ||
        (segment == Segment::kSs &&
         !(descriptor.present && descriptor.writable)))
    {
        ctx->Fault(FaultKind::kGeneralProtection, selector, false);
        return false;
    }
    SegmentRegister& seg = ctx->state.Seg(segment);
    seg.selector = selector;
    seg.base = descriptor.base;
    seg.limit = descriptor.limit;
    seg.present = descriptor.present;
    seg.executable = descriptor.executable;
    seg.writable = descriptor.writable;
    seg.default_32bit = descriptor.default_32bit;
    return true;
}

bool Push(Ctx* ctx, const unsigned width_bits, const std::uint32_t value)
{
    const SegmentRegister& ss = ctx->state.Seg(Segment::kSs);
    const std::uint32_t sp_mask = ss.default_32bit ? 0xFFFFFFFFu : 0xFFFFu;
    const std::uint32_t old_sp = ctx->state.Get(Gpr::kEsp);
    const std::uint32_t new_sp =
        ((old_sp - width_bits / 8u) & sp_mask) | (old_sp & ~sp_mask);
    if (!WriteVirtual(ctx, Segment::kSs, new_sp & sp_mask, width_bits,
                      value))
    {
        return false;
    }
    ctx->state.Set(Gpr::kEsp, new_sp);
    return true;
}

bool Pop(Ctx* ctx, const unsigned width_bits, std::uint32_t* value)
{
    const SegmentRegister& ss = ctx->state.Seg(Segment::kSs);
    const std::uint32_t sp_mask = ss.default_32bit ? 0xFFFFFFFFu : 0xFFFFu;
    const std::uint32_t old_sp = ctx->state.Get(Gpr::kEsp);
    if (!ReadVirtual(ctx, Segment::kSs, old_sp & sp_mask, width_bits, value))
    {
        return false;
    }
    const std::uint32_t new_sp =
        ((old_sp + width_bits / 8u) & sp_mask) | (old_sp & ~sp_mask);
    ctx->state.Set(Gpr::kEsp, new_sp);
    return true;
}

}  // namespace rex86::interp
