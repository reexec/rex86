#include "decode/decoder.h"

namespace rex86::decode
{

namespace
{

bool IsImmediateOperand(const ZydisDecodedOperand& operand)
{
    return operand.type == ZYDIS_OPERAND_TYPE_IMMEDIATE;
}

}  // namespace

const char* DecodedInstruction::MnemonicName() const
{
    const char* const name = ZydisMnemonicGetString(instruction.mnemonic);
    return name != nullptr ? name : "?";
}

const char* DecodedInstruction::IsaSetName() const
{
    const char* const name = ZydisISASetGetString(instruction.meta.isa_set);
    return name != nullptr ? name : "?";
}

bool DecodedInstruction::IsX87() const
{
    return instruction.meta.isa_set == ZYDIS_ISA_SET_X87;
}

std::string DecodedInstruction::OperandSignature() const
{
    std::string signature;
    for (ZyanU8 index = 0; index < instruction.operand_count_visible; ++index)
    {
        const ZydisDecodedOperand& operand = operands[index];
        if (operand.visibility != ZYDIS_OPERAND_VISIBILITY_EXPLICIT)
        {
            continue;
        }
        if (!signature.empty())
        {
            signature += ',';
        }
        switch (operand.type)
        {
            case ZYDIS_OPERAND_TYPE_REGISTER:
                signature += 'r';
                break;
            case ZYDIS_OPERAND_TYPE_MEMORY:
                signature += 'm';
                break;
            case ZYDIS_OPERAND_TYPE_POINTER:
                signature += 'p';
                break;
            case ZYDIS_OPERAND_TYPE_IMMEDIATE:
                signature += 'i';
                break;
            default:
                signature += '?';
                break;
        }
        signature += std::to_string(static_cast<unsigned>(operand.size));
    }
    if (signature.empty())
    {
        signature = "-";
    }
    return signature;
}

bool DecodedInstruction::HasFloat80MemoryOperand() const
{
    for (ZyanU8 index = 0; index < instruction.operand_count_visible; ++index)
    {
        const ZydisDecodedOperand& operand = operands[index];
        if (operand.type == ZYDIS_OPERAND_TYPE_MEMORY &&
            (operand.size == 80U ||
             operand.element_type == ZYDIS_ELEMENT_TYPE_FLOAT80))
        {
            return true;
        }
    }
    return false;
}

ControlFlow DecodedInstruction::Flow() const
{
    if (instruction.mnemonic == ZYDIS_MNEMONIC_HLT)
    {
        return ControlFlow::kHalt;
    }
    switch (instruction.meta.category)
    {
        case ZYDIS_CATEGORY_COND_BR:
            // Jcc, JCXZ/JECXZ and LOOPcc are all relative immediates, so a
            // conditional branch always has a static target.
            return ControlFlow::kConditionalBranch;
        case ZYDIS_CATEGORY_UNCOND_BR:
            return instruction.operand_count_visible > 0 &&
                    IsImmediateOperand(operands[0])
                ? ControlFlow::kDirectJump
                : ControlFlow::kIndirectJump;
        case ZYDIS_CATEGORY_CALL:
            return instruction.operand_count_visible > 0 &&
                    IsImmediateOperand(operands[0])
                ? ControlFlow::kDirectCall
                : ControlFlow::kIndirectCall;
        case ZYDIS_CATEGORY_RET:
            return ControlFlow::kReturn;
        case ZYDIS_CATEGORY_INTERRUPT:
            return ControlFlow::kSoftwareInterrupt;
        default:
            return ControlFlow::kNone;
    }
}

bool DecodedInstruction::DirectTarget(std::uint32_t* target) const
{
    const ControlFlow flow = Flow();
    if (flow != ControlFlow::kDirectJump && flow != ControlFlow::kDirectCall &&
        flow != ControlFlow::kConditionalBranch)
    {
        return false;
    }
    for (ZyanU8 index = 0; index < instruction.operand_count_visible; ++index)
    {
        const ZydisDecodedOperand& operand = operands[index];
        if (operand.type != ZYDIS_OPERAND_TYPE_IMMEDIATE)
        {
            continue;
        }
        ZyanU64 absolute = 0;
        if (!ZYAN_SUCCESS(ZydisCalcAbsoluteAddress(&instruction, &operand,
                                                   guest_address, &absolute)))
        {
            return false;
        }
        *target = static_cast<std::uint32_t>(absolute);
        return true;
    }
    return false;
}

Decoder::Decoder(const Mode mode)
{
    // Initialization cannot fail for a valid mode/width pair, but the
    // result is still checked so a future constant slip fails loudly.
    const bool ok = mode == Mode::kLegacy16
        ? ZYAN_SUCCESS(ZydisDecoderInit(&decoder_,
                                        ZYDIS_MACHINE_MODE_LEGACY_16,
                                        ZYDIS_STACK_WIDTH_16))
        : ZYAN_SUCCESS(ZydisDecoderInit(&decoder_,
                                        ZYDIS_MACHINE_MODE_LEGACY_32,
                                        ZYDIS_STACK_WIDTH_32));
    if (!ok)
    {
        decoder_.machine_mode = ZYDIS_MACHINE_MODE_MAX_VALUE;
    }
}

bool Decoder::Decode(const std::uint8_t* bytes, const std::size_t length,
                     const std::uint32_t guest_address,
                     DecodedInstruction* out) const
{
    bool truncated = false;
    return Decode(bytes, length, guest_address, out, &truncated);
}

bool Decoder::Decode(const std::uint8_t* bytes, const std::size_t length,
                     const std::uint32_t guest_address,
                     DecodedInstruction* out, bool* truncated) const
{
    *truncated = false;
    if (bytes == nullptr || length == 0 || out == nullptr ||
        decoder_.machine_mode == ZYDIS_MACHINE_MODE_MAX_VALUE)
    {
        return false;
    }
    out->guest_address = guest_address;
    const ZyanStatus status = ZydisDecoderDecodeFull(
        &decoder_, bytes, length, &out->instruction, out->operands);
    if (!ZYAN_SUCCESS(status))
    {
        *truncated = status == ZYDIS_STATUS_NO_MORE_DATA;
        return false;
    }
    return out->instruction.length != 0;
}

}  // namespace rex86::decode
