#include "rex86/cpu.h"

#include "interp/interpreter.h"

namespace rex86
{

Cpu::Cpu(GuestMemory* memory,
         Environment* environment,
         const Features& features,
         CodeCacheServices* code_cache)
    : memory_(memory), environment_(environment), code_cache_(code_cache), features_(features)
{
    state_.Reset();
}

Engine Cpu::ActiveEngine() const
{
    // The interpreter exists from task #11 on. A translation backend will
    // answer kTranslator here only when code_cache_ is non-null and the
    // backend is built.
    return Engine::kInterpreter;
}

void Cpu::RegisterGate(std::uint32_t linear_address)
{
    gates_.insert(linear_address);
}

void Cpu::UnregisterGate(std::uint32_t linear_address)
{
    gates_.erase(linear_address);
}

bool Cpu::IsGate(std::uint32_t linear_address) const
{
    return gates_.find(linear_address) != gates_.end();
}

void Cpu::RaiseInterrupt(std::uint8_t vector)
{
    pending_interrupts_.set(vector);
}

bool Cpu::HasPendingInterrupt() const
{
    return pending_interrupts_.any();
}

bool Cpu::NextPendingInterrupt(std::uint8_t* vector) const
{
    for (int candidate = 255; candidate >= 0; --candidate)
    {
        if (pending_interrupts_.test(static_cast<std::size_t>(candidate)))
        {
            *vector = static_cast<std::uint8_t>(candidate);
            return true;
        }
    }
    return false;
}

void Cpu::ClearPendingInterrupt(std::uint8_t vector)
{
    pending_interrupts_.reset(vector);
}

Event Cpu::Run(std::uint64_t instruction_budget)
{
    // The loop owns what is not an instruction's semantics: stop requests,
    // gates (checked before the instruction at the gate runs), external
    // interrupt delivery at boundaries with IF set, and the budget. The
    // x86 semantics live in interp::Step alone.
    Event event;
    std::uint64_t retired = 0;

    while (true)
    {
        if (stop_requested_)
        {
            stop_requested_ = false;
            event.reason = StopReason::kStopRequested;
            event.instructions_retired = retired;
            return event;
        }

        const SegmentRegister& cs = state_.Seg(Segment::kCs);
        const std::uint32_t ip_mask =
            cs.default_32bit ? 0xFFFFFFFFu : 0xFFFFu;
        const std::uint32_t linear = cs.base + (state_.eip & ip_mask);
        if (IsGate(linear))
        {
            event.reason = StopReason::kGate;
            event.gate_address = linear;
            event.instructions_retired = retired;
            return event;
        }

        if (!interrupt_shadow_ && (state_.eflags & kEflagsInterrupt) != 0 &&
            HasPendingInterrupt())
        {
            std::uint8_t vector = 0;
            NextPendingInterrupt(&vector);
            std::uint16_t target_cs = 0;
            std::uint32_t target_eip = 0;
            if (!environment_->InterruptTarget(vector, &target_cs,
                                               &target_eip))
            {
                // The host keeps the vector pending and takes the event.
                event.reason = StopReason::kSoftwareInterrupt;
                event.vector = vector;
                event.instructions_retired = retired;
                return event;
            }
            Event fault_event;
            if (!interp::EnterInterrupt(state_, *memory_, *environment_,
                                        target_cs, target_eip, &fault_event))
            {
                fault_event.instructions_retired = retired;
                return fault_event;
            }
            ClearPendingInterrupt(vector);
            continue;
        }

        if (retired >= instruction_budget)
        {
            event.reason = StopReason::kBudgetExhausted;
            event.instructions_retired = retired;
            return event;
        }

        const interp::StepResult step =
            interp::Step(state_, *memory_, *environment_, features_);
        interrupt_shadow_ = step.inhibit_interrupts;
        switch (step.status)
        {
            case interp::StepStatus::kRetired:
                ++retired;
                break;
            case interp::StepStatus::kRetiredAndStopped:
                ++retired;
                event = step.event;
                event.instructions_retired = retired;
                return event;
            case interp::StepStatus::kFaulted:
            case interp::StepStatus::kUnimplemented:
            default:
                event = step.event;
                event.instructions_retired = retired;
                return event;
        }
    }
}

Event Cpu::Step()
{
    return Run(1);
}

void Cpu::RequestStop()
{
    stop_requested_ = true;
}

void Cpu::InvalidateCode(std::uint32_t address, std::uint32_t size)
{
    if (memory_ != nullptr)
    {
        memory_->pages().Remove(address, size, PageFlag::kTranslated);
    }
}

}  // namespace rex86
