// The MMX instructions and the MMX integer instructions SSE added
// (design #29, decisions 2 and 3). Every instruction here first raises #MF
// for a pending x87 exception, then reads its operands (which may fault),
// and only then enters the MMX state (TOP = 0, tags valid) and writes, so
// a fault leaves the x87 state as it was.

#include "interp/exec.h"
#include "interp/simd_access.h"

#include "interp/flags.h"

namespace rex86::interp
{

namespace
{

using simd::MmIndex;
using simd::ReadMm;
using simd::WriteMm;

// Lane i of width w bits (8, 16, 32, 64) of a 64-bit value.
std::uint64_t Lane(const std::uint64_t value, const unsigned i, const unsigned w)
{
    const std::uint64_t mask = w == 64 ? ~0ull : (1ull << w) - 1;
    return (value >> (i * w)) & mask;
}

std::uint64_t WithLane(const std::uint64_t value, const unsigned i, const unsigned w,
                       const std::uint64_t lane)
{
    const std::uint64_t mask = (w == 64 ? ~0ull : (1ull << w) - 1) << (i * w);
    return (value & ~mask) | ((lane << (i * w)) & mask);
}

std::int64_t Signed(const std::uint64_t lane, const unsigned w)
{
    if (w == 64)
    {
        return static_cast<std::int64_t>(lane);
    }
    const std::uint64_t sign = 1ull << (w - 1);
    return static_cast<std::int64_t>((lane ^ sign)) - static_cast<std::int64_t>(sign);
}

std::uint64_t SaturateSigned(const std::int64_t value, const unsigned w)
{
    const std::int64_t max = (1ll << (w - 1)) - 1;
    const std::int64_t min = -(1ll << (w - 1));
    const std::int64_t clamped = value > max ? max : value < min ? min : value;
    return static_cast<std::uint64_t>(clamped) & ((1ull << w) - 1);
}

std::uint64_t SaturateUnsigned(const std::int64_t value, const unsigned w)
{
    const std::int64_t max = static_cast<std::int64_t>((1ull << w) - 1);
    return static_cast<std::uint64_t>(value > max ? max : value < 0 ? 0 : value);
}

template <typename Op>
std::uint64_t Lanewise(const std::uint64_t a, const std::uint64_t b, const unsigned w,
                       const Op& op)
{
    std::uint64_t result = 0;
    for (unsigned i = 0; i < 64 / w; ++i)
    {
        result = WithLane(result, i, w, op(Lane(a, i, w), Lane(b, i, w)));
    }
    return result;
}

unsigned LaneWidth(const ZydisMnemonic m)
{
    switch (m)
    {
        case ZYDIS_MNEMONIC_PADDB: case ZYDIS_MNEMONIC_PSUBB:
        case ZYDIS_MNEMONIC_PADDSB: case ZYDIS_MNEMONIC_PSUBSB:
        case ZYDIS_MNEMONIC_PADDUSB: case ZYDIS_MNEMONIC_PSUBUSB:
        case ZYDIS_MNEMONIC_PCMPEQB: case ZYDIS_MNEMONIC_PCMPGTB:
        case ZYDIS_MNEMONIC_PAVGB: case ZYDIS_MNEMONIC_PMAXUB:
        case ZYDIS_MNEMONIC_PMINUB:
            return 8;
        case ZYDIS_MNEMONIC_PADDD: case ZYDIS_MNEMONIC_PSUBD:
        case ZYDIS_MNEMONIC_PCMPEQD: case ZYDIS_MNEMONIC_PCMPGTD:
        case ZYDIS_MNEMONIC_PSLLD: case ZYDIS_MNEMONIC_PSRLD: case ZYDIS_MNEMONIC_PSRAD:
            return 32;
        case ZYDIS_MNEMONIC_PSLLQ: case ZYDIS_MNEMONIC_PSRLQ:
            return 64;
        default:
            return 16;
    }
}

// The two-operand lane arithmetic: dst = dst op src.
bool BinaryLanes(const ZydisMnemonic m, const std::uint64_t a, const std::uint64_t b,
                 std::uint64_t* result)
{
    const unsigned w = LaneWidth(m);
    switch (m)
    {
        case ZYDIS_MNEMONIC_PADDB: case ZYDIS_MNEMONIC_PADDW: case ZYDIS_MNEMONIC_PADDD:
            *result = Lanewise(a, b, w, [](std::uint64_t x, std::uint64_t y) { return x + y; });
            return true;
        case ZYDIS_MNEMONIC_PSUBB: case ZYDIS_MNEMONIC_PSUBW: case ZYDIS_MNEMONIC_PSUBD:
            *result = Lanewise(a, b, w, [](std::uint64_t x, std::uint64_t y) { return x - y; });
            return true;
        case ZYDIS_MNEMONIC_PADDSB: case ZYDIS_MNEMONIC_PADDSW:
            *result = Lanewise(a, b, w, [w](std::uint64_t x, std::uint64_t y) {
                return SaturateSigned(Signed(x, w) + Signed(y, w), w);
            });
            return true;
        case ZYDIS_MNEMONIC_PSUBSB: case ZYDIS_MNEMONIC_PSUBSW:
            *result = Lanewise(a, b, w, [w](std::uint64_t x, std::uint64_t y) {
                return SaturateSigned(Signed(x, w) - Signed(y, w), w);
            });
            return true;
        case ZYDIS_MNEMONIC_PADDUSB: case ZYDIS_MNEMONIC_PADDUSW:
            *result = Lanewise(a, b, w, [w](std::uint64_t x, std::uint64_t y) {
                return SaturateUnsigned(static_cast<std::int64_t>(x + y), w);
            });
            return true;
        case ZYDIS_MNEMONIC_PSUBUSB: case ZYDIS_MNEMONIC_PSUBUSW:
            *result = Lanewise(a, b, w, [w](std::uint64_t x, std::uint64_t y) {
                return SaturateUnsigned(static_cast<std::int64_t>(x) - static_cast<std::int64_t>(y), w);
            });
            return true;
        case ZYDIS_MNEMONIC_PCMPEQB: case ZYDIS_MNEMONIC_PCMPEQW: case ZYDIS_MNEMONIC_PCMPEQD:
            *result = Lanewise(a, b, w, [](std::uint64_t x, std::uint64_t y) {
                return x == y ? ~0ull : 0ull;
            });
            return true;
        case ZYDIS_MNEMONIC_PCMPGTB: case ZYDIS_MNEMONIC_PCMPGTW: case ZYDIS_MNEMONIC_PCMPGTD:
            *result = Lanewise(a, b, w, [w](std::uint64_t x, std::uint64_t y) {
                return Signed(x, w) > Signed(y, w) ? ~0ull : 0ull;
            });
            return true;
        case ZYDIS_MNEMONIC_PMULLW:
            *result = Lanewise(a, b, 16, [](std::uint64_t x, std::uint64_t y) {
                return static_cast<std::uint64_t>(Signed(x, 16) * Signed(y, 16));
            });
            return true;
        case ZYDIS_MNEMONIC_PMULHW:
            *result = Lanewise(a, b, 16, [](std::uint64_t x, std::uint64_t y) {
                return static_cast<std::uint64_t>((Signed(x, 16) * Signed(y, 16)) >> 16);
            });
            return true;
        case ZYDIS_MNEMONIC_PMULHUW:
            *result = Lanewise(a, b, 16, [](std::uint64_t x, std::uint64_t y) {
                return (x * y) >> 16;
            });
            return true;
        case ZYDIS_MNEMONIC_PMADDWD:
        {
            std::uint64_t r = 0;
            for (unsigned i = 0; i < 2; ++i)
            {
                const std::int64_t sum =
                    Signed(Lane(a, 2 * i, 16), 16) * Signed(Lane(b, 2 * i, 16), 16) +
                    Signed(Lane(a, 2 * i + 1, 16), 16) * Signed(Lane(b, 2 * i + 1, 16), 16);
                r = WithLane(r, i, 32, static_cast<std::uint64_t>(sum));
            }
            *result = r;
            return true;
        }
        case ZYDIS_MNEMONIC_PAND:
            *result = a & b;
            return true;
        case ZYDIS_MNEMONIC_PANDN:
            *result = ~a & b;
            return true;
        case ZYDIS_MNEMONIC_POR:
            *result = a | b;
            return true;
        case ZYDIS_MNEMONIC_PXOR:
            *result = a ^ b;
            return true;
        case ZYDIS_MNEMONIC_PAVGB: case ZYDIS_MNEMONIC_PAVGW:
            *result = Lanewise(a, b, w, [](std::uint64_t x, std::uint64_t y) {
                return (x + y + 1) >> 1;
            });
            return true;
        case ZYDIS_MNEMONIC_PMAXUB:
            *result = Lanewise(a, b, 8, [](std::uint64_t x, std::uint64_t y) { return x > y ? x : y; });
            return true;
        case ZYDIS_MNEMONIC_PMINUB:
            *result = Lanewise(a, b, 8, [](std::uint64_t x, std::uint64_t y) { return x < y ? x : y; });
            return true;
        case ZYDIS_MNEMONIC_PMAXSW:
            *result = Lanewise(a, b, 16, [](std::uint64_t x, std::uint64_t y) {
                return Signed(x, 16) > Signed(y, 16) ? x : y;
            });
            return true;
        case ZYDIS_MNEMONIC_PMINSW:
            *result = Lanewise(a, b, 16, [](std::uint64_t x, std::uint64_t y) {
                return Signed(x, 16) < Signed(y, 16) ? x : y;
            });
            return true;
        case ZYDIS_MNEMONIC_PSADBW:
        {
            std::uint64_t sum = 0;
            for (unsigned i = 0; i < 8; ++i)
            {
                const std::uint64_t x = Lane(a, i, 8);
                const std::uint64_t y = Lane(b, i, 8);
                sum += x > y ? x - y : y - x;
            }
            *result = sum;
            return true;
        }
        case ZYDIS_MNEMONIC_PACKSSWB: case ZYDIS_MNEMONIC_PACKSSDW: case ZYDIS_MNEMONIC_PACKUSWB:
        {
            const bool dwords = m == ZYDIS_MNEMONIC_PACKSSDW;
            const unsigned in = dwords ? 32 : 16;
            const unsigned out = in / 2;
            const unsigned per = 64 / in;
            std::uint64_t r = 0;
            for (unsigned i = 0; i < 2 * per; ++i)
            {
                const std::uint64_t source = i < per ? a : b;
                const std::int64_t value = Signed(Lane(source, i % per, in), in);
                const std::uint64_t packed = m == ZYDIS_MNEMONIC_PACKUSWB
                    ? SaturateUnsigned(value, out)
                    : SaturateSigned(value, out);
                r = WithLane(r, i, out, packed);
            }
            *result = r;
            return true;
        }
        case ZYDIS_MNEMONIC_PUNPCKLBW: case ZYDIS_MNEMONIC_PUNPCKLWD: case ZYDIS_MNEMONIC_PUNPCKLDQ:
        case ZYDIS_MNEMONIC_PUNPCKHBW: case ZYDIS_MNEMONIC_PUNPCKHWD: case ZYDIS_MNEMONIC_PUNPCKHDQ:
        {
            const unsigned lw = (m == ZYDIS_MNEMONIC_PUNPCKLBW || m == ZYDIS_MNEMONIC_PUNPCKHBW) ? 8
                : (m == ZYDIS_MNEMONIC_PUNPCKLWD || m == ZYDIS_MNEMONIC_PUNPCKHWD) ? 16 : 32;
            const bool high = m == ZYDIS_MNEMONIC_PUNPCKHBW || m == ZYDIS_MNEMONIC_PUNPCKHWD ||
                m == ZYDIS_MNEMONIC_PUNPCKHDQ;
            const unsigned half = 32 / lw;
            std::uint64_t r = 0;
            for (unsigned i = 0; i < half; ++i)
            {
                const unsigned from = high ? half + i : i;
                r = WithLane(r, 2 * i, lw, Lane(a, from, lw));
                r = WithLane(r, 2 * i + 1, lw, Lane(b, from, lw));
            }
            *result = r;
            return true;
        }
        default:
            return false;
    }
}

bool IsShift(const ZydisMnemonic m)
{
    switch (m)
    {
        case ZYDIS_MNEMONIC_PSLLW: case ZYDIS_MNEMONIC_PSLLD: case ZYDIS_MNEMONIC_PSLLQ:
        case ZYDIS_MNEMONIC_PSRLW: case ZYDIS_MNEMONIC_PSRLD: case ZYDIS_MNEMONIC_PSRLQ:
        case ZYDIS_MNEMONIC_PSRAW: case ZYDIS_MNEMONIC_PSRAD:
            return true;
        default:
            return false;
    }
}

// The shift count is the whole 64-bit operand; a count past the lane
// width clears the lanes (logical) or fills them with the sign
// (arithmetic).
std::uint64_t Shift(const ZydisMnemonic m, const std::uint64_t value, const std::uint64_t count)
{
    const unsigned w = LaneWidth(m);
    const bool left = m == ZYDIS_MNEMONIC_PSLLW || m == ZYDIS_MNEMONIC_PSLLD ||
        m == ZYDIS_MNEMONIC_PSLLQ;
    const bool arithmetic = m == ZYDIS_MNEMONIC_PSRAW || m == ZYDIS_MNEMONIC_PSRAD;
    std::uint64_t result = 0;
    for (unsigned i = 0; i < 64 / w; ++i)
    {
        const std::uint64_t lane = Lane(value, i, w);
        std::uint64_t shifted = 0;
        if (arithmetic)
        {
            const std::uint64_t c = count > w - 1 ? w - 1 : count;
            shifted = static_cast<std::uint64_t>(Signed(lane, w) >> c);
        }
        else if (count < w)
        {
            shifted = left ? lane << count : lane >> count;
        }
        result = WithLane(result, i, w, shifted);
    }
    return result;
}

bool UsesMmx(const decode::DecodedInstruction& d)
{
    for (ZyanU8 i = 0; i < d.instruction.operand_count; ++i)
    {
        if (MmIndex(d.operands[i]) >= 0)
        {
            return true;
        }
    }
    return false;
}

}  // namespace

ExecStatus ExecuteMmx(Ctx* ctx)
{
    const decode::DecodedInstruction& d = ctx->decoded;
    const ZydisMnemonic m = d.instruction.mnemonic;
    if (m != ZYDIS_MNEMONIC_EMMS && !UsesMmx(d))
    {
        return ExecStatus::kUnimplemented;
    }
    if (!simd::CheckPendingX87(ctx))
    {
        return ExecStatus::kFault;
    }
    CpuState& s = ctx->state;
    const ZydisDecodedOperand* ops = d.operands;
    if (m == ZYDIS_MNEMONIC_EMMS)
    {
        // Every tag empty and TOP = 0 (SDM Vol. 1 table 9-2; measured on
        // Zen 3).
        simd::EnterMmx(s);
        s.x87.tag_word = X87State::kTagWordAllEmpty;
        return ExecStatus::kContinue;
    }
    const int dst = MmIndex(ops[0]);

    switch (m)
    {
        case ZYDIS_MNEMONIC_MOVD:
        case ZYDIS_MNEMONIC_MOVQ:
        case ZYDIS_MNEMONIC_MOVNTQ:
        {
            std::uint64_t value = 0;
            if (!simd::ReadMmSource(ctx, ops[1], &value))
            {
                return ctx->faulted ? ExecStatus::kFault : ExecStatus::kUnimplemented;
            }
            if (dst >= 0)
            {
                if (m == ZYDIS_MNEMONIC_MOVD)
                {
                    value &= 0xFFFFFFFFull;
                }
                simd::EnterMmx(s);
                WriteMm(s, static_cast<unsigned>(dst), value);
                return ExecStatus::kContinue;
            }
            if (simd::IsGpr(ops[0]))
            {
                simd::EnterMmx(s);
                WriteGpr(s, ops[0].reg.value, static_cast<std::uint32_t>(value));
                return ExecStatus::kContinue;
            }
            std::uint8_t bytes[8];
            for (unsigned i = 0; i < 8; ++i)
            {
                bytes[i] = static_cast<std::uint8_t>(value >> (8 * i));
            }
            if (!simd::WriteMemory(ctx, ops[0], ops[0].size / 8u, bytes))
            {
                return ExecStatus::kFault;
            }
            simd::EnterMmx(s);
            return ExecStatus::kContinue;
        }
        case ZYDIS_MNEMONIC_PEXTRW:
        {
            const std::uint64_t value = ReadMm(s, static_cast<unsigned>(MmIndex(ops[1])));
            const unsigned lane = static_cast<unsigned>(ops[2].imm.value.u & 3u);
            simd::EnterMmx(s);
            WriteGpr(s, ops[0].reg.value, static_cast<std::uint32_t>(Lane(value, lane, 16)));
            return ExecStatus::kContinue;
        }
        case ZYDIS_MNEMONIC_PINSRW:
        {
            std::uint64_t word = 0;
            if (simd::IsGpr(ops[1]))
            {
                word = ReadGpr(s, ops[1].reg.value);
            }
            else if (!simd::ReadMmSource(ctx, ops[1], &word))
            {
                return ExecStatus::kFault;
            }
            const unsigned lane = static_cast<unsigned>(ops[2].imm.value.u & 3u);
            const std::uint64_t value = ReadMm(s, static_cast<unsigned>(dst));
            simd::EnterMmx(s);
            WriteMm(s, static_cast<unsigned>(dst), WithLane(value, lane, 16, word & 0xFFFFu));
            return ExecStatus::kContinue;
        }
        case ZYDIS_MNEMONIC_PMOVMSKB:
        {
            const std::uint64_t value = ReadMm(s, static_cast<unsigned>(MmIndex(ops[1])));
            std::uint32_t mask = 0;
            for (unsigned i = 0; i < 8; ++i)
            {
                mask |= static_cast<std::uint32_t>((value >> (8 * i + 7)) & 1u) << i;
            }
            simd::EnterMmx(s);
            WriteGpr(s, ops[0].reg.value, mask);
            return ExecStatus::kContinue;
        }
        case ZYDIS_MNEMONIC_PSHUFW:
        {
            std::uint64_t source = 0;
            if (!simd::ReadMmSource(ctx, ops[1], &source))
            {
                return ExecStatus::kFault;
            }
            const unsigned order = static_cast<unsigned>(ops[2].imm.value.u & 0xFFu);
            std::uint64_t result = 0;
            for (unsigned i = 0; i < 4; ++i)
            {
                result = WithLane(result, i, 16, Lane(source, (order >> (2 * i)) & 3u, 16));
            }
            simd::EnterMmx(s);
            WriteMm(s, static_cast<unsigned>(dst), result);
            return ExecStatus::kContinue;
        }
        case ZYDIS_MNEMONIC_MASKMOVQ:
        {
            // DS:(E)DI, a segment override applying; only the selected bytes
            // are stored (design #29, decision 4).
            const std::uint64_t data = ReadMm(s, static_cast<unsigned>(dst));
            const std::uint64_t mask = ReadMm(s, static_cast<unsigned>(MmIndex(ops[1])));
            const ZydisDecodedOperand* target = nullptr;
            for (ZyanU8 i = 0; i < d.instruction.operand_count; ++i)
            {
                if (simd::IsMemory(ops[i]))
                {
                    target = &ops[i];
                }
            }
            if (target == nullptr)
            {
                return ExecStatus::kUnimplemented;
            }
            const Segment segment = SegmentOf(d, *target);
            const std::uint32_t amask = WidthMask(d.instruction.address_width);
            const std::uint32_t base = s.Get(Gpr::kEdi) & amask;
            for (unsigned i = 0; i < 8; ++i)
            {
                if (((mask >> (8 * i + 7)) & 1u) != 0 &&
                    !ProbeWrite(ctx, segment, (base + i) & amask, 8))
                {
                    return ExecStatus::kFault;
                }
            }
            for (unsigned i = 0; i < 8; ++i)
            {
                if (((mask >> (8 * i + 7)) & 1u) != 0 &&
                    !WriteVirtual(ctx, segment, (base + i) & amask, 8,
                                  static_cast<std::uint32_t>(Lane(data, i, 8))))
                {
                    return ExecStatus::kFault;
                }
            }
            simd::EnterMmx(s);
            return ExecStatus::kContinue;
        }
        default:
            break;
    }

    if (dst < 0)
    {
        return ExecStatus::kUnimplemented;
    }
    if (IsShift(m))
    {
        std::uint64_t count = 0;
        if (ops[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE)
        {
            count = ops[1].imm.value.u & 0xFFu;
        }
        else if (!simd::ReadMmSource(ctx, ops[1], &count))
        {
            return ExecStatus::kFault;
        }
        const std::uint64_t value = ReadMm(s, static_cast<unsigned>(dst));
        simd::EnterMmx(s);
        WriteMm(s, static_cast<unsigned>(dst), Shift(m, value, count));
        return ExecStatus::kContinue;
    }
    std::uint64_t source = 0;
    if (!simd::ReadMmSource(ctx, ops[1], &source))
    {
        return ctx->faulted ? ExecStatus::kFault : ExecStatus::kUnimplemented;
    }
    std::uint64_t result = 0;
    if (!BinaryLanes(m, ReadMm(s, static_cast<unsigned>(dst)), source, &result))
    {
        return ExecStatus::kUnimplemented;
    }
    simd::EnterMmx(s);
    WriteMm(s, static_cast<unsigned>(dst), result);
    return ExecStatus::kContinue;
}

}  // namespace rex86::interp
