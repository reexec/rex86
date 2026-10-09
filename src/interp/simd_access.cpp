#include "interp/simd_access.h"

#include "interp/flags.h"
#include "interp/x87_stack.h"

namespace rex86::interp::simd
{

namespace
{

// The linear address of a memory operand, for the alignment check.
std::uint32_t LinearAddress(const Ctx& ctx, const ZydisDecodedOperand& operand)
{
    const Segment segment = SegmentOf(ctx.decoded, operand);
    const std::uint32_t mask = WidthMask(ctx.decoded.instruction.address_width);
    return ctx.state.Seg(segment).base + (EffectiveAddress(ctx, operand) & mask);
}

template <typename Access>
bool ForEachPiece(Ctx* ctx, const ZydisDecodedOperand& operand,
                  const unsigned count, const Access& access)
{
    const Segment segment = SegmentOf(ctx->decoded, operand);
    const std::uint32_t base = EffectiveAddress(*ctx, operand);
    const std::uint32_t mask = WidthMask(ctx->decoded.instruction.address_width);
    unsigned done = 0;
    while (done < count)
    {
        const unsigned left = count - done;
        const unsigned width = left >= 4 ? 32u : left >= 2 ? 16u : 8u;
        if (!access(segment, (base + done) & mask, width, done))
        {
            return false;
        }
        done += width / 8u;
    }
    return true;
}

}  // namespace

int MmIndex(const ZydisDecodedOperand& operand)
{
    if (operand.type != ZYDIS_OPERAND_TYPE_REGISTER ||
        operand.reg.value < ZYDIS_REGISTER_MM0 ||
        operand.reg.value > ZYDIS_REGISTER_MM7)
    {
        return -1;
    }
    return operand.reg.value - ZYDIS_REGISTER_MM0;
}

int XmmIndex(const ZydisDecodedOperand& operand)
{
    if (operand.type != ZYDIS_OPERAND_TYPE_REGISTER ||
        operand.reg.value < ZYDIS_REGISTER_XMM0 ||
        operand.reg.value > ZYDIS_REGISTER_XMM7)
    {
        return -1;
    }
    return operand.reg.value - ZYDIS_REGISTER_XMM0;
}

bool IsGpr(const ZydisDecodedOperand& operand)
{
    return operand.type == ZYDIS_OPERAND_TYPE_REGISTER &&
        ZydisRegisterGetClass(operand.reg.value) == ZYDIS_REGCLASS_GPR32;
}

bool IsMemory(const ZydisDecodedOperand& operand)
{
    return operand.type == ZYDIS_OPERAND_TYPE_MEMORY;
}

std::uint64_t ReadMm(const CpuState& state, const unsigned index)
{
    std::uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i)
    {
        value |= static_cast<std::uint64_t>(state.x87.registers[index][i]) << (8 * i);
    }
    return value;
}

void WriteMm(CpuState& state, const unsigned index, const std::uint64_t value)
{
    for (unsigned i = 0; i < 8; ++i)
    {
        state.x87.registers[index][i] = static_cast<std::uint8_t>(value >> (8 * i));
    }
    state.x87.registers[index][8] = 0xFF;
    state.x87.registers[index][9] = 0xFF;
}

void EnterMmx(CpuState& state)
{
    x87::SetTop(&state.x87, 0);
    state.x87.tag_word = 0;
}

bool CheckPendingX87(Ctx* ctx)
{
    if ((ctx->state.x87.status_word & x87::kErrorSummary) == 0)
    {
        return true;
    }
    ctx->Fault(FaultKind::kFloatingPoint,
               ctx->state.Seg(Segment::kCs).base + ctx->state.eip, false);
    return false;
}

bool CheckAligned16(Ctx* ctx, const ZydisDecodedOperand& operand)
{
    const std::uint32_t linear = LinearAddress(*ctx, operand);
    if ((linear & 15u) == 0)
    {
        return true;
    }
    ctx->Fault(FaultKind::kGeneralProtection, linear, false);
    return false;
}

bool ReadMemory(Ctx* ctx, const ZydisDecodedOperand& operand, const unsigned count,
                std::uint8_t* bytes)
{
    return ForEachPiece(
        ctx, operand, count,
        [&](const Segment segment, const std::uint32_t address, const unsigned width,
            const unsigned at) {
            std::uint32_t value = 0;
            if (!ReadVirtual(ctx, segment, address, width, &value))
            {
                return false;
            }
            for (unsigned i = 0; i < width / 8u; ++i)
            {
                bytes[at + i] = static_cast<std::uint8_t>(value >> (8 * i));
            }
            return true;
        });
}

bool WriteMemory(Ctx* ctx, const ZydisDecodedOperand& operand, const unsigned count,
                 const std::uint8_t* bytes)
{
    const bool reachable = ForEachPiece(
        ctx, operand, count,
        [&](const Segment segment, const std::uint32_t address, const unsigned width,
            unsigned) { return ProbeWrite(ctx, segment, address, width); });
    if (!reachable)
    {
        return false;
    }
    return ForEachPiece(
        ctx, operand, count,
        [&](const Segment segment, const std::uint32_t address, const unsigned width,
            const unsigned at) {
            std::uint32_t value = 0;
            for (unsigned i = 0; i < width / 8u; ++i)
            {
                value |= static_cast<std::uint32_t>(bytes[at + i]) << (8 * i);
            }
            return WriteVirtual(ctx, segment, address, width, value);
        });
}

bool ReadMmSource(Ctx* ctx, const ZydisDecodedOperand& operand, std::uint64_t* value)
{
    const int mm = MmIndex(operand);
    if (mm >= 0)
    {
        *value = ReadMm(ctx->state, static_cast<unsigned>(mm));
        return true;
    }
    if (IsGpr(operand))
    {
        *value = ReadGpr(ctx->state, operand.reg.value);
        return true;
    }
    if (!IsMemory(operand))
    {
        return false;
    }
    std::uint8_t bytes[8] = {};
    const unsigned count = operand.size / 8u;
    if (!ReadMemory(ctx, operand, count, bytes))
    {
        return false;
    }
    std::uint64_t result = 0;
    for (unsigned i = 0; i < count; ++i)
    {
        result |= static_cast<std::uint64_t>(bytes[i]) << (8 * i);
    }
    *value = result;
    return true;
}

bool ReadXmmSource(Ctx* ctx, const ZydisDecodedOperand& operand, const bool aligned,
                   Xmm* value)
{
    const int xmm = XmmIndex(operand);
    if (xmm >= 0)
    {
        *value = ctx->state.sse.xmm[static_cast<std::size_t>(xmm)];
        return true;
    }
    if (!IsMemory(operand))
    {
        return false;
    }
    if (aligned && !CheckAligned16(ctx, operand))
    {
        return false;
    }
    Xmm bytes = {};
    if (!ReadMemory(ctx, operand, operand.size / 8u, bytes.data()))
    {
        return false;
    }
    *value = bytes;
    return true;
}

std::uint32_t Lane32(const Xmm& value, const unsigned lane)
{
    std::uint32_t result = 0;
    for (unsigned i = 0; i < 4; ++i)
    {
        result |= static_cast<std::uint32_t>(value[4 * lane + i]) << (8 * i);
    }
    return result;
}

void SetLane32(Xmm* value, const unsigned lane, const std::uint32_t lane_value)
{
    for (unsigned i = 0; i < 4; ++i)
    {
        (*value)[4 * lane + i] = static_cast<std::uint8_t>(lane_value >> (8 * i));
    }
}

std::uint64_t Half64(const Xmm& value, const unsigned half)
{
    std::uint64_t result = 0;
    for (unsigned i = 0; i < 8; ++i)
    {
        result |= static_cast<std::uint64_t>(value[8 * half + i]) << (8 * i);
    }
    return result;
}

void SetHalf64(Xmm* value, const unsigned half, const std::uint64_t half_value)
{
    for (unsigned i = 0; i < 8; ++i)
    {
        (*value)[8 * half + i] = static_cast<std::uint8_t>(half_value >> (8 * i));
    }
}

}  // namespace rex86::interp::simd
