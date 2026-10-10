// The x86-to-IR frontend (design #43, decision 4): forms a basic block at
// CS:EIP and lowers the instructions it covers. With the interpreter it is
// the only place that knows x86 semantics; backends see only the IR.

#ifndef REX86_TRANSLATE_IR_FRONTEND_H_
#define REX86_TRANSLATE_IR_FRONTEND_H_

#include <cstdint>

#include "decode/decoder.h"
#include "interp/interpreter.h"
#include "rex86/cpu_state.h"
#include "rex86/environment.h"
#include "rex86/guest_memory.h"
#include "translate/ir/ir.h"

namespace rex86::translate::ir
{

inline constexpr unsigned kMaxBlockInstructions = 64;

struct FrontendOptions
{
    unsigned max_instructions = kMaxBlockInstructions;
    // A block ends before an address the filter may contain (the first
    // instruction excepted: the Cpu loop has already checked it). Null
    // when there are no gates.
    const interp::GateFilter* gates = nullptr;
};

// True when the frontend lowers this instruction.
bool Covered(const decode::DecodedInstruction& decoded, const Features& features);

// True when a block can be formed under this state's modes: a flat 32-bit
// CS and a 32-bit SS. A block assumes them; whoever runs it checks first.
bool ModesSupported(const CpuState& state);

// Forms the block starting at CS:EIP. Returns false, leaving *block
// unspecified, when no block can start there: the modes are not supported,
// or the first instruction cannot be fetched, decoded or lowered.
bool FormBlock(const CpuState& state, const GuestMemory& memory, const Features& features,
               const FrontendOptions& options, Block* block);

}  // namespace rex86::translate::ir

#endif  // REX86_TRANSLATE_IR_FRONTEND_H_
