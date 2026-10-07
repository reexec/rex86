// The interpreter: the core's correctness reference. interp::Step executes
// exactly one instruction against the architectural state, computing flags
// eagerly and never going through the IR. Cpu::Run owns the loop around it
// (budget, gates, interrupt delivery, stop requests); no x86 semantics live
// there. See docs/design/20261007-i011-interpreter-core.md.

#ifndef REX86_INTERP_INTERPRETER_H_
#define REX86_INTERP_INTERPRETER_H_

#include <cstdint>

#include "rex86/cpu_state.h"
#include "rex86/environment.h"
#include "rex86/guest_memory.h"

namespace rex86::interp
{

enum class StepStatus : std::uint8_t
{
    // The instruction retired; state advanced.
    kRetired,
    // The instruction retired and execution must stop (HLT, INT n, port
    // I/O the host declined). The event says why.
    kRetiredAndStopped,
    // The instruction did not retire: a fault. EIP still addresses the
    // faulting instruction. The event carries the fault.
    kFaulted,
    // The mnemonic decodes but this increment does not implement it. The
    // event carries kIllegalInstruction; callers inside the repository
    // (the SST runner) use the distinction to count coverage honestly.
    kUnimplemented,
};

struct StepResult
{
    StepStatus status = StepStatus::kRetired;
    Event event;
};

// Executes one instruction at CS:EIP.
StepResult Step(CpuState& state, GuestMemory& memory,
                Environment& environment, const Features& features);

// Pushes the FLAGS/CS/IP frame for an accepted external interrupt, clears
// IF and TF and jumps to the target the host supplied. Returns false with
// a fault event when the frame cannot be pushed or the target selector is
// invalid.
bool EnterInterrupt(CpuState& state, GuestMemory& memory,
                    Environment& environment, std::uint16_t target_cs,
                    std::uint32_t target_eip, Event* fault_event);

}  // namespace rex86::interp

#endif  // REX86_INTERP_INTERPRETER_H_
