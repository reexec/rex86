// The core-internal decoder: Zydis in 32-bit legacy mode, wrapped in the
// questions the core keeps asking (length, operand signature, control flow,
// x87). This header lives under src/ and may expose Zydis types; the public
// contract under include/rex86/ must never include it, so the decoder stays
// replaceable without a contract change.

#ifndef REX86_DECODE_DECODER_H_
#define REX86_DECODE_DECODER_H_

#include <Zydis.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace rex86::decode
{

// How an instruction leaves the block. Indirect jumps and indirect calls are
// separate kinds because a walk continues after a call (it returns) and cannot
// continue after a jump (nothing promises the next byte is code).
enum class ControlFlow : std::uint8_t
{
    kNone,
    kDirectJump,
    kDirectCall,
    kConditionalBranch,
    kReturn,
    kIndirectJump,
    kIndirectCall,
    kSoftwareInterrupt,
    kHalt,
};

struct DecodedInstruction
{
    ZydisDecodedInstruction instruction{};
    ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT] = {};
    std::uint32_t guest_address = 0;

    std::uint32_t Length() const
    {
        return instruction.length;
    }

    const char* MnemonicName() const;
    const char* IsaSetName() const;
    bool IsX87() const;

    // The explicit operands as the census signature: one letter per operand
    // (r register, m memory, i immediate, p pointer) followed by its width in
    // bits, comma-separated; "-" when there is none. The format matches
    // rePIU's census so the two reports compare directly.
    std::string OperandSignature() const;

    // True for a memory operand in the 80-bit extended-real format, which is
    // the observable x87 format design #1 commits to.
    bool HasFloat80MemoryOperand() const;

    ControlFlow Flow() const;

    // The absolute guest target of a direct jump, direct call or conditional
    // branch. Returns false for every other flow kind.
    bool DirectTarget(std::uint32_t* target) const;
};

class Decoder
{
public:
    Decoder();

    // Decodes one instruction at guest_address from at most `length` bytes.
    // Returns false when the bytes do not form a valid 32-bit instruction.
    bool Decode(const std::uint8_t* bytes, std::size_t length,
                std::uint32_t guest_address, DecodedInstruction* out) const;

private:
    ZydisDecoder decoder_{};
};

}  // namespace rex86::decode

#endif  // REX86_DECODE_DECODER_H_
