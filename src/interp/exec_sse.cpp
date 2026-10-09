// The SSE instructions that are not floating-point arithmetic (design #29,
// increment 1): moves, the bitwise logic, SHUFPS, the unpacks, MOVMSKPS,
// MOVNTPS, PREFETCHh and SFENCE. None of them touches MXCSR's flags or the
// x87 state. Aligned forms check 16-byte alignment before any access
// (decision 4).

#include "interp/exec.h"
#include "interp/simd_access.h"

namespace rex86::interp
{

namespace
{

using simd::Xmm;
using simd::XmmIndex;

bool WriteXmmDestination(Ctx* ctx, const ZydisDecodedOperand& operand, const Xmm& value,
                         const bool aligned)
{
    const int xmm = XmmIndex(operand);
    if (xmm >= 0)
    {
        ctx->state.sse.xmm[static_cast<std::size_t>(xmm)] = value;
        return true;
    }
    if (aligned && !simd::CheckAligned16(ctx, operand))
    {
        return false;
    }
    return simd::WriteMemory(ctx, operand, operand.size / 8u, value.data());
}

}  // namespace

ExecStatus ExecuteSse(Ctx* ctx)
{
    const decode::DecodedInstruction& d = ctx->decoded;
    const ZydisMnemonic m = d.instruction.mnemonic;
    const ZydisDecodedOperand* ops = d.operands;
    CpuState& s = ctx->state;

    switch (m)
    {
        case ZYDIS_MNEMONIC_SFENCE:
            return ExecStatus::kContinue;
        case ZYDIS_MNEMONIC_PREFETCHNTA:
        case ZYDIS_MNEMONIC_PREFETCHT0:
        case ZYDIS_MNEMONIC_PREFETCHT1:
        case ZYDIS_MNEMONIC_PREFETCHT2:
            // A hint: no access, no exception (SDM PREFETCHh).
            return d.instruction.meta.isa_set == ZYDIS_ISA_SET_SSE_PREFETCH
                ? ExecStatus::kContinue
                : ExecStatus::kUnimplemented;
        default:
            break;
    }
    if (d.instruction.meta.isa_set != ZYDIS_ISA_SET_SSE)
    {
        return ExecStatus::kUnimplemented;
    }

    switch (m)
    {
        case ZYDIS_MNEMONIC_MOVAPS:
        case ZYDIS_MNEMONIC_MOVUPS:
        case ZYDIS_MNEMONIC_MOVNTPS:
        {
            const bool aligned = m != ZYDIS_MNEMONIC_MOVUPS;
            Xmm value = {};
            if (!simd::ReadXmmSource(ctx, ops[1], aligned, &value) ||
                !WriteXmmDestination(ctx, ops[0], value, aligned))
            {
                return ExecStatus::kFault;
            }
            return ExecStatus::kContinue;
        }
        case ZYDIS_MNEMONIC_MOVSS:
        {
            const int dst = XmmIndex(ops[0]);
            const int src = XmmIndex(ops[1]);
            if (dst >= 0 && src >= 0)
            {
                // Register to register: the low dword alone.
                Xmm& to = s.sse.xmm[static_cast<std::size_t>(dst)];
                simd::SetLane32(&to, 0,
                                simd::Lane32(s.sse.xmm[static_cast<std::size_t>(src)], 0));
                return ExecStatus::kContinue;
            }
            if (dst >= 0)
            {
                // From memory: the dword, the rest zeroed.
                Xmm value = {};
                if (!simd::ReadXmmSource(ctx, ops[1], false, &value))
                {
                    return ExecStatus::kFault;
                }
                s.sse.xmm[static_cast<std::size_t>(dst)] = value;
                return ExecStatus::kContinue;
            }
            const Xmm& from = s.sse.xmm[static_cast<std::size_t>(src)];
            return simd::WriteMemory(ctx, ops[0], 4, from.data()) ? ExecStatus::kContinue
                                                                  : ExecStatus::kFault;
        }
        case ZYDIS_MNEMONIC_MOVLPS:
        case ZYDIS_MNEMONIC_MOVHPS:
        {
            const unsigned half = m == ZYDIS_MNEMONIC_MOVHPS ? 1u : 0u;
            const int dst = XmmIndex(ops[0]);
            if (dst >= 0)
            {
                Xmm value = {};
                if (!simd::ReadXmmSource(ctx, ops[1], false, &value))
                {
                    return ExecStatus::kFault;
                }
                simd::SetHalf64(&s.sse.xmm[static_cast<std::size_t>(dst)], half,
                                simd::Half64(value, 0));
                return ExecStatus::kContinue;
            }
            const Xmm& from = s.sse.xmm[static_cast<std::size_t>(XmmIndex(ops[1]))];
            return simd::WriteMemory(ctx, ops[0], 8, from.data() + 8 * half)
                ? ExecStatus::kContinue
                : ExecStatus::kFault;
        }
        case ZYDIS_MNEMONIC_MOVHLPS:
        case ZYDIS_MNEMONIC_MOVLHPS:
        {
            Xmm& to = s.sse.xmm[static_cast<std::size_t>(XmmIndex(ops[0]))];
            const Xmm from = s.sse.xmm[static_cast<std::size_t>(XmmIndex(ops[1]))];
            if (m == ZYDIS_MNEMONIC_MOVHLPS)
            {
                simd::SetHalf64(&to, 0, simd::Half64(from, 1));
            }
            else
            {
                simd::SetHalf64(&to, 1, simd::Half64(from, 0));
            }
            return ExecStatus::kContinue;
        }
        case ZYDIS_MNEMONIC_MOVMSKPS:
        {
            const Xmm& from = s.sse.xmm[static_cast<std::size_t>(XmmIndex(ops[1]))];
            std::uint32_t mask = 0;
            for (unsigned lane = 0; lane < 4; ++lane)
            {
                mask |= (simd::Lane32(from, lane) >> 31) << lane;
            }
            WriteGpr(s, ops[0].reg.value, mask);
            return ExecStatus::kContinue;
        }
        case ZYDIS_MNEMONIC_ANDPS:
        case ZYDIS_MNEMONIC_ANDNPS:
        case ZYDIS_MNEMONIC_ORPS:
        case ZYDIS_MNEMONIC_XORPS:
        case ZYDIS_MNEMONIC_SHUFPS:
        case ZYDIS_MNEMONIC_UNPCKLPS:
        case ZYDIS_MNEMONIC_UNPCKHPS:
        {
            Xmm source = {};
            if (!simd::ReadXmmSource(ctx, ops[1], true, &source))
            {
                return ExecStatus::kFault;
            }
            Xmm& to = s.sse.xmm[static_cast<std::size_t>(XmmIndex(ops[0]))];
            const Xmm a = to;
            Xmm result = {};
            switch (m)
            {
                case ZYDIS_MNEMONIC_ANDPS:
                    for (unsigned i = 0; i < 16; ++i) result[i] = a[i] & source[i];
                    break;
                case ZYDIS_MNEMONIC_ANDNPS:
                    for (unsigned i = 0; i < 16; ++i)
                        result[i] = static_cast<std::uint8_t>(~a[i] & source[i]);
                    break;
                case ZYDIS_MNEMONIC_ORPS:
                    for (unsigned i = 0; i < 16; ++i) result[i] = a[i] | source[i];
                    break;
                case ZYDIS_MNEMONIC_XORPS:
                    for (unsigned i = 0; i < 16; ++i) result[i] = a[i] ^ source[i];
                    break;
                case ZYDIS_MNEMONIC_SHUFPS:
                {
                    const unsigned order = static_cast<unsigned>(ops[2].imm.value.u & 0xFFu);
                    simd::SetLane32(&result, 0, simd::Lane32(a, order & 3u));
                    simd::SetLane32(&result, 1, simd::Lane32(a, (order >> 2) & 3u));
                    simd::SetLane32(&result, 2, simd::Lane32(source, (order >> 4) & 3u));
                    simd::SetLane32(&result, 3, simd::Lane32(source, (order >> 6) & 3u));
                    break;
                }
                case ZYDIS_MNEMONIC_UNPCKLPS:
                case ZYDIS_MNEMONIC_UNPCKHPS:
                {
                    const unsigned base = m == ZYDIS_MNEMONIC_UNPCKHPS ? 2u : 0u;
                    simd::SetLane32(&result, 0, simd::Lane32(a, base));
                    simd::SetLane32(&result, 1, simd::Lane32(source, base));
                    simd::SetLane32(&result, 2, simd::Lane32(a, base + 1));
                    simd::SetLane32(&result, 3, simd::Lane32(source, base + 1));
                    break;
                }
                default:
                    break;
            }
            to = result;
            return ExecStatus::kContinue;
        }
        default:
            return ExecStatus::kUnimplemented;
    }
}

}  // namespace rex86::interp
