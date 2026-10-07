// The x87 control instructions (#19): FNINIT, FNCLEX, FLDCW, FNSTCW,
// FNSTSW, FNSTENV/FLDENV, FNSAVE/FRSTOR and the 8087/287 no-ops. The
// environment uses the protected-mode layouts, 16- or 32-bit by operand
// size (design #19, decision 4); a load reads the whole image before
// changing anything.

#include "interp/exec.h"
#include "interp/x87_access.h"
#include "interp/x87_stack.h"

#include "fpu/x87_math.h"

namespace rex86::interp
{

namespace
{

void Initialize(X87State* x87)
{
    x87->control_word = X87State::kControlWordReset;
    x87->status_word = 0;
    x87->tag_word = X87State::kTagWordAllEmpty;
    x87->last_opcode = 0;
    x87->last_instruction_pointer = 0;
    x87->last_instruction_selector = 0;
    x87->last_operand_pointer = 0;
    x87->last_operand_selector = 0;
}

// The tag word FNSTENV stores: computed from each register's contents,
// empty registers staying empty.
std::uint16_t FullTagWord(const X87State& x87)
{
    std::uint16_t tags = 0;
    for (unsigned physical = 0; physical < 8; ++physical)
    {
        unsigned tag = x87::kTagEmpty;
        if (x87::Tag(x87, physical) != x87::kTagEmpty)
        {
            tag = x87::TagFor(fpu::FromBytes(x87.registers[physical].data()));
        }
        tags = static_cast<std::uint16_t>(tags | (tag << (2 * physical)));
    }
    return tags;
}

void Put16(std::uint8_t* bytes, const std::uint16_t value)
{
    bytes[0] = static_cast<std::uint8_t>(value);
    bytes[1] = static_cast<std::uint8_t>(value >> 8);
}

void Put32(std::uint8_t* bytes, const std::uint32_t value)
{
    for (unsigned index = 0; index < 4; ++index)
    {
        bytes[index] = static_cast<std::uint8_t>(value >> (8 * index));
    }
}

std::uint16_t Get16(const std::uint8_t* bytes)
{
    return static_cast<std::uint16_t>(bytes[0] | (bytes[1] << 8));
}

std::uint32_t Get32(const std::uint8_t* bytes)
{
    return static_cast<std::uint32_t>(bytes[0]) |
        (static_cast<std::uint32_t>(bytes[1]) << 8) |
        (static_cast<std::uint32_t>(bytes[2]) << 16) |
        (static_cast<std::uint32_t>(bytes[3]) << 24);
}

// The environment image: 28 bytes (32-bit) or 14 bytes (16-bit). The
// 32-bit layout's reserved upper halves are written as ones.
unsigned EncodeEnvironment(const X87State& x87, const bool wide,
                           std::uint8_t* bytes)
{
    const std::uint16_t tags = FullTagWord(x87);
    if (wide)
    {
        Put32(bytes + 0, 0xFFFF0000u | x87.control_word);
        Put32(bytes + 4, 0xFFFF0000u | x87.status_word);
        Put32(bytes + 8, 0xFFFF0000u | tags);
        Put32(bytes + 12, x87.last_instruction_pointer);
        Put32(bytes + 16, x87.last_instruction_selector |
                              (static_cast<std::uint32_t>(x87.last_opcode & 0x7FFu)
                               << 16));
        Put32(bytes + 20, x87.last_operand_pointer);
        Put32(bytes + 24, 0xFFFF0000u | x87.last_operand_selector);
        return 28;
    }
    Put16(bytes + 0, x87.control_word);
    Put16(bytes + 2, x87.status_word);
    Put16(bytes + 4, tags);
    Put16(bytes + 6, static_cast<std::uint16_t>(x87.last_instruction_pointer));
    Put16(bytes + 8, x87.last_instruction_selector);
    Put16(bytes + 10, static_cast<std::uint16_t>(x87.last_operand_pointer));
    Put16(bytes + 12, x87.last_operand_selector);
    return 14;
}

// Loads an environment image. Tags keep only "empty or not" and are
// recomputed from the registers (decision 2), which therefore must
// already hold their final contents.
void DecodeEnvironment(X87State* x87, const bool wide, const std::uint8_t* bytes)
{
    std::uint16_t tags = 0;
    if (wide)
    {
        x87->control_word = x87::CanonicalControlWord(Get16(bytes + 0));
        x87->status_word = Get16(bytes + 4);
        tags = Get16(bytes + 8);
        x87->last_instruction_pointer = Get32(bytes + 12);
        x87->last_instruction_selector = Get16(bytes + 16);
        x87->last_opcode = static_cast<std::uint16_t>((Get32(bytes + 16) >> 16) & 0x7FFu);
        x87->last_operand_pointer = Get32(bytes + 20);
        x87->last_operand_selector = Get16(bytes + 24);
    }
    else
    {
        x87->control_word = x87::CanonicalControlWord(Get16(bytes + 0));
        x87->status_word = Get16(bytes + 2);
        tags = Get16(bytes + 4);
        x87->last_instruction_pointer = Get16(bytes + 6);
        x87->last_instruction_selector = Get16(bytes + 8);
        x87->last_operand_pointer = Get16(bytes + 10);
        x87->last_operand_selector = Get16(bytes + 12);
        x87->last_opcode = 0;
    }
    for (unsigned physical = 0; physical < 8; ++physical)
    {
        const bool empty = ((tags >> (2 * physical)) & 3u) == x87::kTagEmpty;
        x87::SetTag(x87, physical,
                    empty ? x87::kTagEmpty
                          : x87::TagFor(fpu::FromBytes(x87->registers[physical].data())));
    }
    x87::UpdateErrorSummary(x87);
}

}  // namespace

ExecStatus ExecuteX87Control(Ctx* ctx)
{
    const decode::DecodedInstruction& d = ctx->decoded;
    X87State& x87 = ctx->state.x87;
    const ZydisDecodedOperand* mem = x87::MemoryOperand(d);
    const bool wide = d.instruction.operand_width == 32;

    switch (d.instruction.mnemonic)
    {
        case ZYDIS_MNEMONIC_FNINIT:
            Initialize(&x87);
            return ExecStatus::kContinue;
        case ZYDIS_MNEMONIC_FNCLEX:
            x87.status_word &= 0x7F00u;
            return ExecStatus::kContinue;
        case ZYDIS_MNEMONIC_FENI8087_NOP:
        case ZYDIS_MNEMONIC_FDISI8087_NOP:
        case ZYDIS_MNEMONIC_FSETPM287_NOP:
            return ExecStatus::kContinue;
        case ZYDIS_MNEMONIC_FLDCW:
        {
            std::uint8_t bytes[2] = {};
            if (!x87::ReadOperandBytes(ctx, *mem, 0, 2, bytes))
            {
                return ExecStatus::kFault;
            }
            x87.control_word = x87::CanonicalControlWord(Get16(bytes));
            x87::UpdateErrorSummary(&x87);
            return ExecStatus::kContinue;
        }
        case ZYDIS_MNEMONIC_FNSTCW:
        {
            std::uint8_t bytes[2] = {};
            Put16(bytes, x87.control_word);
            return x87::WriteOperandBytes(ctx, *mem, 0, 2, bytes)
                ? ExecStatus::kContinue
                : ExecStatus::kFault;
        }
        case ZYDIS_MNEMONIC_FNSTSW:
        {
            if (mem == nullptr)
            {
                WriteGpr(ctx->state, ZYDIS_REGISTER_AX, x87.status_word);
                return ExecStatus::kContinue;
            }
            std::uint8_t bytes[2] = {};
            Put16(bytes, x87.status_word);
            return x87::WriteOperandBytes(ctx, *mem, 0, 2, bytes)
                ? ExecStatus::kContinue
                : ExecStatus::kFault;
        }
        case ZYDIS_MNEMONIC_FNSTENV:
        {
            std::uint8_t bytes[28] = {};
            const unsigned size = EncodeEnvironment(x87, wide, bytes);
            if (!x87::WriteOperandBytes(ctx, *mem, 0, size, bytes))
            {
                return ExecStatus::kFault;
            }
            // FNSTENV masks every exception after storing (SDM).
            x87.control_word |= fpu::kExceptionMask;
            x87::UpdateErrorSummary(&x87);
            return ExecStatus::kContinue;
        }
        case ZYDIS_MNEMONIC_FLDENV:
        {
            std::uint8_t bytes[28] = {};
            const unsigned size = wide ? 28u : 14u;
            if (!x87::ReadOperandBytes(ctx, *mem, 0, size, bytes))
            {
                return ExecStatus::kFault;
            }
            DecodeEnvironment(&x87, wide, bytes);
            return ExecStatus::kContinue;
        }
        case ZYDIS_MNEMONIC_FNSAVE:
        {
            std::uint8_t bytes[108] = {};
            const unsigned env = EncodeEnvironment(x87, wide, bytes);
            for (unsigned st = 0; st < 8; ++st)
            {
                fpu::ToBytes(x87::Read(x87, st), bytes + env + 10 * st);
            }
            if (!x87::WriteOperandBytes(ctx, *mem, 0, env + 80, bytes))
            {
                return ExecStatus::kFault;
            }
            Initialize(&x87);
            return ExecStatus::kContinue;
        }
        case ZYDIS_MNEMONIC_FRSTOR:
        {
            std::uint8_t bytes[108] = {};
            const unsigned env = wide ? 28u : 14u;
            if (!x87::ReadOperandBytes(ctx, *mem, 0, env + 80, bytes))
            {
                return ExecStatus::kFault;
            }
            // The registers are stored in ST order relative to the loaded
            // TOP; place them before the tags are recomputed.
            const std::uint16_t status_word = wide ? Get16(bytes + 4) : Get16(bytes + 2);
            const unsigned top = (status_word >> 11) & 7u;
            for (unsigned st = 0; st < 8; ++st)
            {
                const unsigned physical = (top + st) & 7u;
                for (unsigned index = 0; index < 10; ++index)
                {
                    x87.registers[physical][index] = bytes[env + 10 * st + index];
                }
            }
            DecodeEnvironment(&x87, wide, bytes);
            return ExecStatus::kContinue;
        }
        default:
            return ExecStatus::kUnimplemented;
    }
}

}  // namespace rex86::interp
