// Operand and memory access for the interpreter: register file mapping,
// effective-address generation (address-size wrap), segmentation (base,
// limit, faults) and guest memory reads and writes with the kTranslated
// store check. This is the one place goal 1's address semantics live on
// the interpreter path.

#ifndef REX86_INTERP_ACCESS_H_
#define REX86_INTERP_ACCESS_H_

#include <cstdint>

#include "decode/decoder.h"
#include "rex86/cpu_state.h"
#include "rex86/environment.h"
#include "rex86/guest_memory.h"

namespace rex86::interp
{

// One instruction's execution context. `fault` is filled by the first
// failing access; later helpers become no-ops once `faulted` is set, so
// semantics code can run straight-line and check once.
struct Ctx
{
    CpuState& state;
    GuestMemory& memory;
    Environment& environment;
    const decode::DecodedInstruction& decoded;
    Event fault;
    bool faulted = false;
    // Set by MOV SS, POP SS and an STI that enabled IF: no external
    // interrupt at the next boundary (design #15, decision 2).
    bool inhibit_interrupts = false;
    // Set by a REP string instruction: a fault keeps the completed
    // iterations' register updates, which are architectural. Every other
    // fault restores the integer state the instruction started from
    // (design #17, decision 6).
    bool keep_partial_state = false;

    void Fault(FaultKind kind, std::uint32_t address, bool on_write);
};

// General-purpose register access by Zydis register id. Width comes from
// the register itself (AL/AX/EAX...); 8- and 16-bit writes merge.
std::uint32_t ReadGpr(const CpuState& state, ZydisRegister reg);
void WriteGpr(CpuState& state, ZydisRegister reg, std::uint32_t value);

// The segment a memory operand uses, overrides included. The raw prefix
// bytes are consulted as well as Zydis's resolution: Zydis reads a 0x3E
// before an indirect CALL/JMP as CET's notrack hint, but on IA-32 it is a
// DS override and the guests this core serves predate CET.
Segment SegmentOf(const decode::DecodedInstruction& decoded,
                  const ZydisDecodedOperand& operand);

// The effective address of a memory operand, wrapped at the instruction's
// address width.
std::uint32_t EffectiveAddress(const Ctx& ctx,
                               const ZydisDecodedOperand& operand);

// Data access through a segment: limit check (kStackFault through SS,
// kGeneralProtection otherwise), then guest memory (kAccessViolation),
// little-endian. width_bits is 8, 16 or 32. A store to a kTranslated page
// clears the flag and reports OnCodePageWritten before the store.
bool ReadVirtual(Ctx* ctx, Segment segment, std::uint32_t offset,
                 unsigned width_bits, std::uint32_t* value);
bool WriteVirtual(Ctx* ctx, Segment segment, std::uint32_t offset,
                  unsigned width_bits, std::uint32_t value);

// Explicit-operand read/write: register, memory or immediate (immediates
// never write). Values are zero-extended to 32 bits.
bool ReadOperand(Ctx* ctx, const ZydisDecodedOperand& operand,
                 std::uint32_t* value);
bool WriteOperand(Ctx* ctx, const ZydisDecodedOperand& operand,
                  std::uint32_t value);

// A near branch target must lie within the CS limit, or the branch
// faults with kGeneralProtection before anything is committed (SDM: JMP,
// CALL, RET). A flat CS never fails.
bool CheckBranchTarget(Ctx* ctx, std::uint32_t target);

// Loads a segment register through Environment::LoadDescriptor, for
// every selector including null (design #15, decision 1). CS must come
// back present and executable, SS present and writable; any refusal is a
// kGeneralProtection fault with the register left unchanged.
bool LoadSegment(Ctx* ctx, Segment segment, std::uint16_t selector);

// Stack operations at the given width (16 or 32), using SS and the stack
// pointer width SS.D selects.
bool Push(Ctx* ctx, unsigned width_bits, std::uint32_t value);
bool Pop(Ctx* ctx, unsigned width_bits, std::uint32_t* value);

}  // namespace rex86::interp

#endif  // REX86_INTERP_ACCESS_H_
