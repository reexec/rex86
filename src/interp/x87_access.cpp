#include "interp/x87_access.h"

#include "interp/flags.h"

namespace rex86::interp::x87
{

namespace
{

template <typename Access>
bool ForEachPiece(Ctx* ctx, const ZydisDecodedOperand& operand,
                  const std::uint32_t offset, const unsigned count,
                  const Access& access)
{
    const decode::DecodedInstruction& d = ctx->decoded;
    const Segment segment = SegmentOf(d, operand);
    const std::uint32_t base = EffectiveAddress(*ctx, operand);
    const std::uint32_t mask = WidthMask(d.instruction.address_width);
    unsigned done = 0;
    while (done < count)
    {
        const unsigned left = count - done;
        const unsigned width = left >= 4 ? 32u : left >= 2 ? 16u : 8u;
        if (!access(segment, (base + offset + done) & mask, width, done))
        {
            return false;
        }
        done += width / 8u;
    }
    return true;
}

}  // namespace

bool ReadOperandBytes(Ctx* ctx, const ZydisDecodedOperand& operand,
                      const std::uint32_t offset, const unsigned count,
                      std::uint8_t* bytes)
{
    return ForEachPiece(
        ctx, operand, offset, count,
        [&](const Segment segment, const std::uint32_t address,
            const unsigned width, const unsigned at) {
            std::uint32_t value = 0;
            if (!ReadVirtual(ctx, segment, address, width, &value))
            {
                return false;
            }
            for (unsigned index = 0; index < width / 8u; ++index)
            {
                bytes[at + index] = static_cast<std::uint8_t>(value >> (8 * index));
            }
            return true;
        });
}

bool WriteOperandBytes(Ctx* ctx, const ZydisDecodedOperand& operand,
                       const std::uint32_t offset, const unsigned count,
                       const std::uint8_t* bytes)
{
    return ForEachPiece(
        ctx, operand, offset, count,
        [&](const Segment segment, const std::uint32_t address,
            const unsigned width, const unsigned at) {
            std::uint32_t value = 0;
            for (unsigned index = 0; index < width / 8u; ++index)
            {
                value |= static_cast<std::uint32_t>(bytes[at + index]) << (8 * index);
            }
            return WriteVirtual(ctx, segment, address, width, value);
        });
}

const ZydisDecodedOperand* MemoryOperand(const decode::DecodedInstruction& d)
{
    for (ZyanU8 index = 0; index < d.instruction.operand_count; ++index)
    {
        if (d.operands[index].type == ZYDIS_OPERAND_TYPE_MEMORY)
        {
            return &d.operands[index];
        }
    }
    return nullptr;
}

}  // namespace rex86::interp::x87
