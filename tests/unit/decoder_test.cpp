#include "decode/decoder.h"

#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

#include "test_support.h"

namespace
{

using rex86::decode::ControlFlow;
using rex86::decode::DecodedInstruction;
using rex86::decode::Decoder;

bool DecodeBytes(const Decoder& decoder,
                 const std::initializer_list<std::uint8_t> bytes,
                 const std::uint32_t guest_address, DecodedInstruction* out)
{
    const std::vector<std::uint8_t> buffer(bytes);
    return decoder.Decode(buffer.data(), buffer.size(), guest_address, out);
}

}  // namespace

void RunDecoderTests(rex86::test::Context& context)
{
    const Decoder decoder;
    DecodedInstruction decoded;

    // mov eax, 0x12345678
    REX86_CHECK(context, DecodeBytes(decoder, {0xB8, 0x78, 0x56, 0x34, 0x12},
                                     0x1000, &decoded));
    REX86_CHECK_EQ(context, decoded.Length(), 5U);
    REX86_CHECK_EQ(context, std::string(decoded.MnemonicName()), "mov");
    REX86_CHECK_EQ(context, decoded.OperandSignature(), "r32,i32");
    REX86_CHECK(context, decoded.Flow() == ControlFlow::kNone);
    std::uint32_t target = 0;
    REX86_CHECK(context, !decoded.DirectTarget(&target));

    // add [eax], bl
    REX86_CHECK(context, DecodeBytes(decoder, {0x00, 0x18}, 0x1000, &decoded));
    REX86_CHECK_EQ(context, std::string(decoded.MnemonicName()), "add");
    REX86_CHECK_EQ(context, decoded.OperandSignature(), "m8,r8");

    // jmp rel8 to itself
    REX86_CHECK(context, DecodeBytes(decoder, {0xEB, 0xFE}, 0x2000, &decoded));
    REX86_CHECK(context, decoded.Flow() == ControlFlow::kDirectJump);
    REX86_CHECK(context, decoded.DirectTarget(&target));
    REX86_CHECK_EQ(context, target, 0x2000U);

    // call rel32 with zero displacement lands at the next instruction
    REX86_CHECK(context, DecodeBytes(decoder,
                                     {0xE8, 0x00, 0x00, 0x00, 0x00}, 0x3000,
                                     &decoded));
    REX86_CHECK(context, decoded.Flow() == ControlFlow::kDirectCall);
    REX86_CHECK(context, decoded.DirectTarget(&target));
    REX86_CHECK_EQ(context, target, 0x3005U);

    // jnz rel8
    REX86_CHECK(context, DecodeBytes(decoder, {0x75, 0x02}, 0x4000, &decoded));
    REX86_CHECK(context, decoded.Flow() == ControlFlow::kConditionalBranch);
    REX86_CHECK(context, decoded.DirectTarget(&target));
    REX86_CHECK_EQ(context, target, 0x4004U);

    // ret
    REX86_CHECK(context, DecodeBytes(decoder, {0xC3}, 0x5000, &decoded));
    REX86_CHECK(context, decoded.Flow() == ControlFlow::kReturn);

    // call eax
    REX86_CHECK(context, DecodeBytes(decoder, {0xFF, 0xD0}, 0x6000, &decoded));
    REX86_CHECK(context, decoded.Flow() == ControlFlow::kIndirectCall);
    REX86_CHECK(context, !decoded.DirectTarget(&target));

    // jmp [eax]
    REX86_CHECK(context, DecodeBytes(decoder, {0xFF, 0x20}, 0x7000, &decoded));
    REX86_CHECK(context, decoded.Flow() == ControlFlow::kIndirectJump);

    // int 21h
    REX86_CHECK(context, DecodeBytes(decoder, {0xCD, 0x21}, 0x8000, &decoded));
    REX86_CHECK(context, decoded.Flow() == ControlFlow::kSoftwareInterrupt);

    // hlt
    REX86_CHECK(context, DecodeBytes(decoder, {0xF4}, 0x9000, &decoded));
    REX86_CHECK(context, decoded.Flow() == ControlFlow::kHalt);

    // fld tbyte ptr [eax]: the 80-bit memory format design #1 commits to
    REX86_CHECK(context, DecodeBytes(decoder, {0xDB, 0x28}, 0xA000, &decoded));
    REX86_CHECK_EQ(context, std::string(decoded.MnemonicName()), "fld");
    REX86_CHECK(context, decoded.IsX87());
    REX86_CHECK_EQ(context, decoded.OperandSignature(), "m80");
    REX86_CHECK(context, decoded.HasFloat80MemoryOperand());

    // fldcw [eax]
    REX86_CHECK(context, DecodeBytes(decoder, {0xD9, 0x28}, 0xB000, &decoded));
    REX86_CHECK_EQ(context, std::string(decoded.MnemonicName()), "fldcw");
    REX86_CHECK(context, decoded.IsX87());
    REX86_CHECK(context, !decoded.HasFloat80MemoryOperand());

    // fadd st0, st1 is x87 without any memory operand
    REX86_CHECK(context, DecodeBytes(decoder, {0xD8, 0xC1}, 0xC000, &decoded));
    REX86_CHECK(context, decoded.IsX87());
    REX86_CHECK(context, !decoded.HasFloat80MemoryOperand());

    // A lone operand-size prefix is not an instruction.
    REX86_CHECK(context, !DecodeBytes(decoder, {0x66}, 0xD000, &decoded));

    // Empty input is not an instruction either.
    REX86_CHECK(context, !decoder.Decode(nullptr, 0, 0xE000, &decoded));
}
