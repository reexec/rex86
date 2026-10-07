// Byte-granular memory operand access for the x87 instructions, whose
// operands run from 2 to 108 bytes. Shared by exec_x87.cpp and
// exec_x87_env.cpp.

#ifndef REX86_INTERP_X87_ACCESS_H_
#define REX86_INTERP_X87_ACCESS_H_

#include <cstdint>

#include "interp/access.h"

namespace rex86::interp::x87
{

// Reads or writes `count` bytes of the memory operand starting `offset`
// bytes into it, in naturally sized pieces (dwords, then a word, then a
// byte) so segment-limit checks see the access widths the x87 uses.
bool ReadOperandBytes(Ctx* ctx, const ZydisDecodedOperand& operand,
                      std::uint32_t offset, unsigned count,
                      std::uint8_t* bytes);
bool WriteOperandBytes(Ctx* ctx, const ZydisDecodedOperand& operand,
                       std::uint32_t offset, unsigned count,
                       const std::uint8_t* bytes);

// The memory operand of the instruction, or null for register forms.
const ZydisDecodedOperand* MemoryOperand(const decode::DecodedInstruction& d);

}  // namespace rex86::interp::x87

#endif  // REX86_INTERP_X87_ACCESS_H_
