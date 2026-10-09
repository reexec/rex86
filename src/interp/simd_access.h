// What the MMX, SSE and FXSAVE instruction files share (design #29,
// decisions 3 and 4): MM and XMM register access, the MMX entry into the
// x87 state (TOP, tags, bits 79:64, #MF), and 64- and 128-bit memory
// operands with SSE's 16-byte alignment check. Stores check every piece
// before writing any, so a fault leaves memory untouched.

#ifndef REX86_INTERP_SIMD_ACCESS_H_
#define REX86_INTERP_SIMD_ACCESS_H_

#include <array>
#include <cstdint>

#include "interp/access.h"

namespace rex86::interp::simd
{

using Xmm = std::array<std::uint8_t, 16>;

// Register numbers, or -1 when the operand is not that kind of register.
int MmIndex(const ZydisDecodedOperand& operand);
int XmmIndex(const ZydisDecodedOperand& operand);
bool IsGpr(const ZydisDecodedOperand& operand);
bool IsMemory(const ZydisDecodedOperand& operand);

// MMn is the low 64 bits of x87 physical register n. Writing sets the
// register's bits 79:64 to all ones (SDM Vol. 1 9.5.1).
std::uint64_t ReadMm(const CpuState& state, unsigned index);
void WriteMm(CpuState& state, unsigned index, std::uint64_t value);

// What every MMX instruction but EMMS does to the x87 state: TOP = 0 and
// every tag valid. Called once the instruction can no longer fault.
void EnterMmx(CpuState& state);

// #MF before an MMX instruction (EMMS included) when an unmasked x87
// exception is pending. False with the fault filled in.
bool CheckPendingX87(Ctx* ctx);

// The 16-byte alignment check of SSE's aligned operands: #GP before any
// access when the operand's linear address is not a multiple of 16.
bool CheckAligned16(Ctx* ctx, const ZydisDecodedOperand& operand);

// `count` bytes of a memory operand, read or written in dword-sized
// pieces through the segment and page checks. A write checks every piece
// first.
bool ReadMemory(Ctx* ctx, const ZydisDecodedOperand& operand, unsigned count,
                std::uint8_t* bytes);
bool WriteMemory(Ctx* ctx, const ZydisDecodedOperand& operand, unsigned count,
                 const std::uint8_t* bytes);

// The source of an MMX instruction: an MM register or a memory operand of
// the operand's size (zero-extended to 64 bits), or a general register
// for MOVD.
bool ReadMmSource(Ctx* ctx, const ZydisDecodedOperand& operand,
                  std::uint64_t* value);

// The 128-bit source of an SSE instruction: an XMM register or a memory
// operand of the operand's size (16, 8 or 4 bytes, the rest zero), with
// the alignment check when `aligned`.
bool ReadXmmSource(Ctx* ctx, const ZydisDecodedOperand& operand, bool aligned,
                   Xmm* value);

std::uint32_t Lane32(const Xmm& value, unsigned lane);
void SetLane32(Xmm* value, unsigned lane, std::uint32_t lane_value);
std::uint64_t Half64(const Xmm& value, unsigned half);
void SetHalf64(Xmm* value, unsigned half, std::uint64_t half_value);

}  // namespace rex86::interp::simd

#endif  // REX86_INTERP_SIMD_ACCESS_H_
