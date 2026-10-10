// The interpreter: the core's correctness reference. interp::Step executes
// exactly one instruction against the architectural state, computing flags
// eagerly and never going through the IR. Cpu::Run owns the loop around it
// (budget, gates, interrupt delivery, stop requests); no x86 semantics live
// there. See docs/design/20261007-i011-interpreter-core.md.

#ifndef REX86_INTERP_INTERPRETER_H_
#define REX86_INTERP_INTERPRETER_H_

#include <atomic>
#include <cstdint>

#include "rex86/cpu_state.h"
#include "rex86/environment.h"
#include "rex86/guest_memory.h"

namespace rex86::decode
{
struct DecodedInstruction;
}

namespace rex86::interp
{

class DecodeCache;

// The longest IA-32 instruction.
inline constexpr unsigned kFetchWindow = 15;

// True when the instruction's feature is enabled; a disabled feature's
// instruction raises #UD. The translation frontend asks the same question
// before lowering an instruction (design #43).
bool FeatureEnabled(const decode::DecodedInstruction& decoded, const Features& features);

// Fetches up to kFetchWindow bytes at CS:EIP as the interpreter does,
// stopping at the CS limit (*stopped_at_limit) or the first byte not
// mapped readable and executable; returns the count fetched.
unsigned Fetch(const CpuState& state, const GuestMemory& memory, std::uint8_t* bytes,
               bool* stopped_at_limit);

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
    // The instruction did not retire and stopped restartably for the host
    // (a declined INS/OUTS iteration). Completed iterations are
    // architectural; resuming re-executes with what remains.
    kStopped,
    // A REP string stopped between iterations without an event: its step
    // allowance ran out or attention was raised (design #32). Completed
    // iterations are architectural, EIP still addresses the instruction,
    // and executing it again continues with what remains.
    kPartial,
    // The mnemonic decodes but this increment does not implement it. The
    // event carries kIllegalInstruction; callers inside the repository
    // (the SST runner) use the distinction to count coverage honestly.
    kUnimplemented,
};

struct StepBudget;

struct StepResult
{
    StepStatus status = StepStatus::kRetired;
    Event event;
    // MOV SS, POP SS or an IF-enabling STI retired: no external interrupt
    // at the next boundary.
    bool inhibit_interrupts = false;
    // An input, not a result: the budget RunBlock lends a REP string
    // (design #32), null elsewhere. It rides here because the block loop
    // passes its StepResult to every instruction already; a seventh
    // argument went on the stack and cost the hot loop a few percent.
    StepBudget* budget = nullptr;
};

// Executes one instruction at CS:EIP, a REP string to completion. With a
// cache, decodes are kept and reused while their pages' generations hold
// (design #34); without one, every instruction is fetched and decoded
// afresh.
StepResult Step(CpuState& state, GuestMemory& memory,
                Environment& environment, const Features& features,
                DecodeCache* cache = nullptr);

// The Cpu's gates as RunBlock sees them (design #35): a 65,536-bit filter
// over linear addresses. A clear bit proves no gate is there; a set bit ends
// the block so that the Cpu's loop consults the exact set.
struct GateFilter
{
    static constexpr std::uint32_t kWords = (1u << 16) / 64u;

    static std::uint32_t Bit(const std::uint32_t linear)
    {
        return (linear * 0x9E3779B1u) >> 16;
    }

    [[nodiscard]] bool MayContain(const std::uint32_t linear) const
    {
        const std::uint32_t bit = Bit(linear);
        return ((words[bit >> 6] >> (bit & 63u)) & 1u) != 0;
    }

    const std::uint64_t* words = nullptr;
};

struct BlockLimits
{
    // At least one step runs; the block stops once this many ran, a REP
    // string between its iterations if need be (design #32).
    std::uint64_t max_steps = 1;
    // Read before every instruction after the first and between a REP
    // string's iterations: the Cpu raises it for a stop request or a
    // pending interrupt, which its loop handles.
    const std::atomic<bool>* attention = nullptr;
    // Null when the Cpu has no gates.
    const GateFilter* gates = nullptr;
};

struct BlockResult
{
    // The last instruction's result; kRetired or kPartial when the block
    // ended on a limit rather than an event.
    StepResult last;
    std::uint64_t steps = 0;
};

// Executes instructions from CS:EIP as Step would, one after another, until
// one does not simply retire, the limit is reached, attention is raised, or
// the next instruction may be at a gate. A REP string stops between
// iterations on the limit or attention (kPartial). Every instruction keeps Step's
// semantics, precise faults included; only the Cpu loop's checks between
// instructions are skipped while nothing can need them.
BlockResult RunBlock(CpuState& state, GuestMemory& memory,
                     Environment& environment, const Features& features,
                     DecodeCache* cache, const BlockLimits& limits);

// Pushes the FLAGS/CS/IP frame for an accepted external interrupt, clears
// IF and TF and jumps to the target the host supplied. Returns false with
// a fault event when the frame cannot be pushed or the target selector is
// invalid.
bool EnterInterrupt(CpuState& state, GuestMemory& memory,
                    Environment& environment, std::uint16_t target_cs,
                    std::uint32_t target_eip, Event* fault_event);

}  // namespace rex86::interp

#endif  // REX86_INTERP_INTERPRETER_H_
