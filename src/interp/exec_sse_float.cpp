// The SSE floating-point instructions (design #29, increment 2): packed and
// scalar arithmetic, MIN/MAX, SQRT, RCP/RSQRT, CMPPS/CMPSS, COMISS/UCOMISS
// and the conversions. Each lane comes from fpu::sse; the lanes' flags are
// then applied in SDM 11.5.1's order: an unmasked pre-computation
// exception (#I, #D, #Z) in any lane sets those flags and raises #XM with
// nothing written; otherwise an unmasked post-computation exception (#O,
// #U, #P) sets every flag and raises #XM with nothing written; otherwise
// the results are written and the flags merged into MXCSR.

#include "interp/exec.h"
#include "interp/simd_access.h"

#include "fpu/sse_math.h"

namespace rex86::interp
{

namespace
{

namespace sse = fpu::sse;
using simd::Xmm;

struct Flags
{
    std::uint32_t pre = 0;
    std::uint32_t post = 0;

    void Add(const sse::Lane& lane)
    {
        pre |= lane.pre;
        post |= lane.post;
    }
};

// False with #XM when an unmasked exception was raised.
bool Commit(Ctx* ctx, const Flags& flags)
{
    std::uint32_t& mxcsr = ctx->state.sse.mxcsr;
    const std::uint32_t unmasked = ~(mxcsr >> sse::kMaskShift) & sse::kFlagMask;
    std::uint32_t raised = flags.pre;
    if ((flags.pre & unmasked) == 0)
    {
        raised |= flags.post;
    }
    mxcsr |= raised;
    if ((raised & unmasked) != 0)
    {
        ctx->Fault(FaultKind::kSimdFloatingPoint,
                   ctx->state.Seg(Segment::kCs).base + ctx->state.eip, false);
        return false;
    }
    return true;
}

bool ArithOp(const ZydisMnemonic m, sse::Op* op, bool* scalar)
{
    *scalar = false;
    switch (m)
    {
        case ZYDIS_MNEMONIC_ADDSS: *scalar = true; [[fallthrough]];
        case ZYDIS_MNEMONIC_ADDPS: *op = sse::Op::kAdd; return true;
        case ZYDIS_MNEMONIC_SUBSS: *scalar = true; [[fallthrough]];
        case ZYDIS_MNEMONIC_SUBPS: *op = sse::Op::kSub; return true;
        case ZYDIS_MNEMONIC_MULSS: *scalar = true; [[fallthrough]];
        case ZYDIS_MNEMONIC_MULPS: *op = sse::Op::kMul; return true;
        case ZYDIS_MNEMONIC_DIVSS: *scalar = true; [[fallthrough]];
        case ZYDIS_MNEMONIC_DIVPS: *op = sse::Op::kDiv; return true;
        case ZYDIS_MNEMONIC_MINSS: *scalar = true; [[fallthrough]];
        case ZYDIS_MNEMONIC_MINPS: *op = sse::Op::kMin; return true;
        case ZYDIS_MNEMONIC_MAXSS: *scalar = true; [[fallthrough]];
        case ZYDIS_MNEMONIC_MAXPS: *op = sse::Op::kMax; return true;
        case ZYDIS_MNEMONIC_SQRTSS: *scalar = true; [[fallthrough]];
        case ZYDIS_MNEMONIC_SQRTPS: *op = sse::Op::kSqrt; return true;
        case ZYDIS_MNEMONIC_RCPSS: *scalar = true; [[fallthrough]];
        case ZYDIS_MNEMONIC_RCPPS: *op = sse::Op::kRcp; return true;
        case ZYDIS_MNEMONIC_RSQRTSS: *scalar = true; [[fallthrough]];
        case ZYDIS_MNEMONIC_RSQRTPS: *op = sse::Op::kRsqrt; return true;
        default: return false;
    }
}

}  // namespace

ExecStatus ExecuteSseFloat(Ctx* ctx)
{
    const decode::DecodedInstruction& d = ctx->decoded;
    if (d.instruction.meta.isa_set != ZYDIS_ISA_SET_SSE)
    {
        return ExecStatus::kUnimplemented;
    }
    const ZydisMnemonic m = d.instruction.mnemonic;
    const ZydisDecodedOperand* ops = d.operands;
    CpuState& s = ctx->state;
    const std::uint32_t mxcsr = s.sse.mxcsr;
    Flags flags;

    sse::Op op = sse::Op::kAdd;
    bool scalar = false;
    if (ArithOp(m, &op, &scalar) || m == ZYDIS_MNEMONIC_CMPPS || m == ZYDIS_MNEMONIC_CMPSS)
    {
        const bool compare = m == ZYDIS_MNEMONIC_CMPPS || m == ZYDIS_MNEMONIC_CMPSS;
        scalar = scalar || m == ZYDIS_MNEMONIC_CMPSS;
        Xmm source = {};
        if (!simd::ReadXmmSource(ctx, ops[1], !scalar, &source))
        {
            return ExecStatus::kFault;
        }
        Xmm& to = s.sse.xmm[static_cast<std::size_t>(simd::XmmIndex(ops[0]))];
        Xmm result = to;
        const unsigned predicate =
            compare ? static_cast<unsigned>(ops[2].imm.value.u & 7u) : 0u;
        for (unsigned lane = 0; lane < (scalar ? 1u : 4u); ++lane)
        {
            const std::uint32_t a = simd::Lane32(to, lane);
            const std::uint32_t b = simd::Lane32(source, lane);
            const sse::Lane r = compare ? sse::Compare(predicate, a, b)
                                        : sse::Arith(op, a, b, mxcsr);
            flags.Add(r);
            simd::SetLane32(&result, lane, r.value);
        }
        if (!Commit(ctx, flags))
        {
            return ExecStatus::kFault;
        }
        to = result;
        return ExecStatus::kContinue;
    }

    switch (m)
    {
        case ZYDIS_MNEMONIC_COMISS:
        case ZYDIS_MNEMONIC_UCOMISS:
        {
            Xmm source = {};
            if (!simd::ReadXmmSource(ctx, ops[1], false, &source))
            {
                return ExecStatus::kFault;
            }
            const Xmm& a = s.sse.xmm[static_cast<std::size_t>(simd::XmmIndex(ops[0]))];
            const sse::Lane r = sse::OrderedCompare(simd::Lane32(a, 0), simd::Lane32(source, 0),
                                                    m == ZYDIS_MNEMONIC_COMISS);
            flags.Add(r);
            if (!Commit(ctx, flags))
            {
                return ExecStatus::kFault;
            }
            // ZF, PF, CF from the comparison; OF, SF, AF cleared.
            s.eflags = (s.eflags & ~0x8D5u) | r.value;
            return ExecStatus::kContinue;
        }
        case ZYDIS_MNEMONIC_CVTSI2SS:
        {
            std::uint64_t value = 0;
            if (!simd::ReadMmSource(ctx, ops[1], &value))
            {
                return ExecStatus::kFault;
            }
            const sse::Lane r = sse::FromInt32(static_cast<std::uint32_t>(value), mxcsr);
            flags.Add(r);
            if (!Commit(ctx, flags))
            {
                return ExecStatus::kFault;
            }
            simd::SetLane32(&s.sse.xmm[static_cast<std::size_t>(simd::XmmIndex(ops[0]))], 0, r.value);
            return ExecStatus::kContinue;
        }
        case ZYDIS_MNEMONIC_CVTSS2SI:
        case ZYDIS_MNEMONIC_CVTTSS2SI:
        {
            Xmm source = {};
            if (!simd::ReadXmmSource(ctx, ops[1], false, &source))
            {
                return ExecStatus::kFault;
            }
            const sse::Lane r = sse::ToInt32(simd::Lane32(source, 0), mxcsr,
                                             m == ZYDIS_MNEMONIC_CVTTSS2SI);
            flags.Add(r);
            if (!Commit(ctx, flags))
            {
                return ExecStatus::kFault;
            }
            WriteGpr(s, ops[0].reg.value, r.value);
            return ExecStatus::kContinue;
        }
        case ZYDIS_MNEMONIC_CVTPI2PS:
        {
            // An MM source is an MMX instruction: #MF first, the MMX state
            // after (design #29, decision 3).
            const bool mm_source = simd::MmIndex(ops[1]) >= 0;
            if (mm_source && !simd::CheckPendingX87(ctx))
            {
                return ExecStatus::kFault;
            }
            std::uint64_t value = 0;
            if (!simd::ReadMmSource(ctx, ops[1], &value))
            {
                return ExecStatus::kFault;
            }
            const sse::Lane low = sse::FromInt32(static_cast<std::uint32_t>(value), mxcsr);
            const sse::Lane high = sse::FromInt32(static_cast<std::uint32_t>(value >> 32), mxcsr);
            flags.Add(low);
            flags.Add(high);
            if (!Commit(ctx, flags))
            {
                return ExecStatus::kFault;
            }
            if (mm_source)
            {
                simd::EnterMmx(s);
            }
            Xmm& to = s.sse.xmm[static_cast<std::size_t>(simd::XmmIndex(ops[0]))];
            simd::SetLane32(&to, 0, low.value);
            simd::SetLane32(&to, 1, high.value);
            return ExecStatus::kContinue;
        }
        case ZYDIS_MNEMONIC_CVTPS2PI:
        case ZYDIS_MNEMONIC_CVTTPS2PI:
        {
            if (!simd::CheckPendingX87(ctx))
            {
                return ExecStatus::kFault;
            }
            Xmm source = {};
            if (!simd::ReadXmmSource(ctx, ops[1], false, &source))
            {
                return ExecStatus::kFault;
            }
            const bool truncate = m == ZYDIS_MNEMONIC_CVTTPS2PI;
            const sse::Lane low = sse::ToInt32(simd::Lane32(source, 0), mxcsr, truncate);
            const sse::Lane high = sse::ToInt32(simd::Lane32(source, 1), mxcsr, truncate);
            flags.Add(low);
            flags.Add(high);
            if (!Commit(ctx, flags))
            {
                return ExecStatus::kFault;
            }
            simd::EnterMmx(s);
            simd::WriteMm(s, static_cast<unsigned>(simd::MmIndex(ops[0])),
                          static_cast<std::uint64_t>(low.value) |
                              (static_cast<std::uint64_t>(high.value) << 32));
            return ExecStatus::kContinue;
        }
        default:
            return ExecStatus::kUnimplemented;
    }
}

}  // namespace rex86::interp
