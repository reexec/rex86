#include "rex86/cpu.h"

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
    // Task #1 ships the contract without an engine. The interpreter (task
    // of phase 1) answers kInterpreter here, and a translation backend
    // answers kTranslator only when code_cache_ is non-null.
    return Engine::kNone;
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
    static_cast<void>(instruction_budget);
    Event event;
    if (stop_requested_)
    {
        stop_requested_ = false;
        event.reason = StopReason::kStopRequested;
        return event;
    }
    // No engine exists yet. This is reported, not imitated: a caller that
    // reads kNoEngine knows nothing ran, where a dummy "budget exhausted"
    // would let an idle core pass for a working one.
    event.reason = StopReason::kNoEngine;
    event.instructions_retired = 0;
    return event;
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
