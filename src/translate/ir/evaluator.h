// The IR evaluator (design #43, decision 5): a portable backend that runs
// a block's operations in order. It defines the IR's meaning; other
// backends must produce its results. Effects land in the state and memory
// at once, so the state is precise at every exit.

#ifndef REX86_TRANSLATE_IR_EVALUATOR_H_
#define REX86_TRANSLATE_IR_EVALUATOR_H_

#include <cstdint>

#include "rex86/cpu_state.h"
#include "rex86/guest_memory.h"
#include "translate/ir/ir.h"

namespace rex86::translate::ir
{

struct ExitResult
{
    ExitKind kind = ExitKind::kContinue;
    std::uint32_t eip = 0;
    // Guest instructions completed before the exit.
    std::uint32_t steps = 0;
};

// True when the interpreter's access would complete without a fault and
// without touching translated code (design #43, decision 3): the segment
// is writable for a write, a non-flat segment is present and the access
// fits its limit, the bytes lie in guest memory on mapped pages readable
// (writable for a write), and no page of a write has kTranslated.
bool AccessWouldSucceed(const CpuState& state, const GuestMemory& memory, Segment segment,
                        std::uint32_t offset, unsigned bytes, bool write);

// Runs the block from its first operation. On return EIP is the exit's.
ExitResult Evaluate(const Block& block, CpuState& state, GuestMemory& memory);

}  // namespace rex86::translate::ir

#endif  // REX86_TRANSLATE_IR_EVALUATOR_H_
