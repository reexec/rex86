// FXSAVE, FXRSTOR, LDMXCSR and STMXCSR (design #29, decision 5). The image
// is the SDM's 32-bit FXSAVE layout. Without Features::sse the MXCSR and
// XMM fields are neither written nor read, as on a P6 without SSE.
// FXRSTOR reads the whole image and checks MXCSR before changing anything.

#include "interp/exec.h"
#include "interp/simd_access.h"
#include "interp/x87_stack.h"

#include "fpu/float80.h"

namespace rex86::interp
{

namespace
{

constexpr unsigned kImageBytes = 512;
// Bytes 0-287 hold the state; FXSAVE leaves the rest untouched.
constexpr unsigned kStateBytes = 288;
constexpr unsigned kX87Bytes = 160;
// The Pentium III's MXCSR: every bit below 16 but DAZ (bit 6).
constexpr std::uint32_t kMxcsrValid = 0x0000FFBFu;

void Put16(std::uint8_t* bytes, const std::uint32_t value)
{
    bytes[0] = static_cast<std::uint8_t>(value);
    bytes[1] = static_cast<std::uint8_t>(value >> 8);
}

void Put32(std::uint8_t* bytes, const std::uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
    {
        bytes[i] = static_cast<std::uint8_t>(value >> (8 * i));
    }
}

std::uint16_t Get16(const std::uint8_t* bytes)
{
    return static_cast<std::uint16_t>(bytes[0] | (bytes[1] << 8));
}

std::uint32_t Get32(const std::uint8_t* bytes)
{
    return static_cast<std::uint32_t>(Get16(bytes)) |
        (static_cast<std::uint32_t>(Get16(bytes + 2)) << 16);
}

const ZydisDecodedOperand* Memory(const decode::DecodedInstruction& d)
{
    for (ZyanU8 i = 0; i < d.instruction.operand_count; ++i)
    {
        if (simd::IsMemory(d.operands[i]))
        {
            return &d.operands[i];
        }
    }
    return nullptr;
}

void Encode(const CpuState& s, const bool sse, std::uint8_t* image)
{
    const X87State& x87 = s.x87;
    Put16(image + 0, x87.control_word);
    Put16(image + 2, x87.status_word);
    std::uint8_t abridged = 0;
    for (unsigned physical = 0; physical < 8; ++physical)
    {
        if (x87::Tag(x87, physical) != x87::kTagEmpty)
        {
            abridged = static_cast<std::uint8_t>(abridged | (1u << physical));
        }
    }
    image[4] = abridged;
    Put16(image + 6, x87.last_opcode & 0x7FFu);
    Put32(image + 8, x87.last_instruction_pointer);
    Put16(image + 12, x87.last_instruction_selector);
    Put32(image + 16, x87.last_operand_pointer);
    Put16(image + 20, x87.last_operand_selector);
    if (sse)
    {
        Put32(image + 24, s.sse.mxcsr);
        Put32(image + 28, kMxcsrValid);
    }
    for (unsigned st = 0; st < 8; ++st)
    {
        const auto& reg = x87.registers[x87::Physical(x87, st)];
        for (unsigned i = 0; i < 10; ++i)
        {
            image[32 + 16 * st + i] = reg[i];
        }
    }
    if (sse)
    {
        for (unsigned n = 0; n < 8; ++n)
        {
            for (unsigned i = 0; i < 16; ++i)
            {
                image[160 + 16 * n + i] = s.sse.xmm[n][i];
            }
        }
    }
}

void Decode(CpuState* s, const bool sse, const std::uint8_t* image)
{
    X87State& x87 = s->x87;
    x87.control_word = x87::CanonicalControlWord(Get16(image + 0));
    x87.status_word = Get16(image + 2);
    x87.last_opcode = static_cast<std::uint16_t>(Get16(image + 6) & 0x7FFu);
    x87.last_instruction_pointer = Get32(image + 8);
    x87.last_instruction_selector = Get16(image + 12);
    x87.last_operand_pointer = Get32(image + 16);
    x87.last_operand_selector = Get16(image + 20);
    const unsigned top = (x87.status_word >> 11) & 7u;
    for (unsigned st = 0; st < 8; ++st)
    {
        auto& reg = x87.registers[(top + st) & 7u];
        for (unsigned i = 0; i < 10; ++i)
        {
            reg[i] = image[32 + 16 * st + i];
        }
    }
    // The abridged tag keeps only "empty or not"; the full tag comes back
    // from the restored contents.
    std::uint16_t tags = 0;
    for (unsigned physical = 0; physical < 8; ++physical)
    {
        unsigned tag = x87::kTagEmpty;
        if ((image[4] >> physical) & 1u)
        {
            tag = x87::TagFor(fpu::FromBytes(x87.registers[physical].data()));
        }
        tags = static_cast<std::uint16_t>(tags | (tag << (2 * physical)));
    }
    x87.tag_word = tags;
    if (sse)
    {
        s->sse.mxcsr = Get32(image + 24);
        for (unsigned n = 0; n < 8; ++n)
        {
            for (unsigned i = 0; i < 16; ++i)
            {
                s->sse.xmm[n][i] = image[160 + 16 * n + i];
            }
        }
    }
}

bool CheckMxcsr(Ctx* ctx, const std::uint32_t value)
{
    if ((value & ~kMxcsrValid) == 0)
    {
        return true;
    }
    ctx->Fault(FaultKind::kGeneralProtection,
               ctx->state.Seg(Segment::kCs).base + ctx->state.eip, false);
    return false;
}

}  // namespace

ExecStatus ExecuteFxsave(Ctx* ctx)
{
    const Features features = ctx->features != nullptr ? *ctx->features : Features{};
    const decode::DecodedInstruction& d = ctx->decoded;
    const ZydisMnemonic m = d.instruction.mnemonic;
    if (m != ZYDIS_MNEMONIC_FXSAVE && m != ZYDIS_MNEMONIC_FXRSTOR &&
        m != ZYDIS_MNEMONIC_LDMXCSR && m != ZYDIS_MNEMONIC_STMXCSR)
    {
        return ExecStatus::kUnimplemented;
    }
    const ZydisDecodedOperand* mem = Memory(d);
    if (mem == nullptr)
    {
        return ExecStatus::kUnimplemented;
    }
    CpuState& s = ctx->state;
    switch (m)
    {
        case ZYDIS_MNEMONIC_STMXCSR:
        {
            std::uint8_t bytes[4];
            Put32(bytes, s.sse.mxcsr);
            return simd::WriteMemory(ctx, *mem, 4, bytes) ? ExecStatus::kContinue
                                                          : ExecStatus::kFault;
        }
        case ZYDIS_MNEMONIC_LDMXCSR:
        {
            std::uint8_t bytes[4];
            if (!simd::ReadMemory(ctx, *mem, 4, bytes) || !CheckMxcsr(ctx, Get32(bytes)))
            {
                return ExecStatus::kFault;
            }
            s.sse.mxcsr = Get32(bytes);
            return ExecStatus::kContinue;
        }
        default:
            break;
    }

    if (!simd::CheckAligned16(ctx, *mem))
    {
        return ExecStatus::kFault;
    }
    const unsigned used = features.sse ? kStateBytes : kX87Bytes;
    if (m == ZYDIS_MNEMONIC_FXSAVE)
    {
        // The image is read back first so the fields FXSAVE leaves alone
        // (reserved bytes, MXCSR and XMM without SSE) keep their contents,
        // and the whole 512-byte operand is checked for reachability.
        std::uint8_t image[kImageBytes];
        if (!simd::ReadMemory(ctx, *mem, kImageBytes, image))
        {
            return ExecStatus::kFault;
        }
        for (unsigned i = 0; i < used; ++i)
        {
            const bool keep = !features.sse && i >= 24 && i < 32;
            if (!keep)
            {
                image[i] = 0;
            }
        }
        Encode(s, features.sse, image);
        return simd::WriteMemory(ctx, *mem, kImageBytes, image) ? ExecStatus::kContinue
                                                                : ExecStatus::kFault;
    }

    std::uint8_t image[kImageBytes];
    if (!simd::ReadMemory(ctx, *mem, kImageBytes, image))
    {
        return ExecStatus::kFault;
    }
    if (features.sse && !CheckMxcsr(ctx, Get32(image + 24)))
    {
        return ExecStatus::kFault;
    }
    Decode(&s, features.sse, image);
    return ExecStatus::kContinue;
}

}  // namespace rex86::interp
