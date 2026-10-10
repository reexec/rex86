#include "rex86/cpu.h"

#include <algorithm>
#include <atomic>

#include "interp/decode_cache.h"
#include "interp/interpreter.h"
#include "translate/ir/frontend.h"
#include "translate/runtime.h"
#include "translate/wasm/backend.h"

namespace rex86
{

namespace
{

// The process-wide default for new Cpus (design #45). A
// REX86_FORCE_TRANSLATION build starts it at {kEvaluator, 0}, so that the
// whole test suite runs through translation (design #44, decision 4).
TranslationOptions& DefaultTranslationStorage()
{
    static TranslationOptions options = [] {
        TranslationOptions initial;
#if defined(REX86_FORCE_TRANSLATION)
        initial.mode = TranslationMode::kEvaluator;
        initial.threshold = 0;
#endif
        return initial;
    }();
    return options;
}

}  // namespace

void SetDefaultTranslation(const TranslationOptions& options)
{
    DefaultTranslationStorage() = options;
}

const TranslationOptions& DefaultTranslation()
{
    return DefaultTranslationStorage();
}

Cpu::Cpu(GuestMemory* memory,
         Environment* environment,
         const Features& features,
         CodeCacheServices* code_cache)
    : memory_(memory), environment_(environment), code_cache_(code_cache), features_(features)
{
    state_.Reset();
    SetTranslation(DefaultTranslationStorage());
}

Cpu::~Cpu() = default;
Cpu::Cpu(Cpu&&) noexcept = default;
Cpu& Cpu::operator=(Cpu&&) noexcept = default;

namespace
{

// Steps run before a Cpu makes its decode cache (design #34).
// A build may set it to 0 so that every test and fuzz runs through the
// cache (CI's sanitizer job does).
#if defined(REX86_DECODE_CACHE_WARMUP)
constexpr std::uint64_t kDecodeCacheWarmup = REX86_DECODE_CACHE_WARMUP;
#else
constexpr std::uint64_t kDecodeCacheWarmup = 64;
#endif

}  // namespace

Engine Cpu::ActiveEngine() const
{
    return translator_ ? Engine::kTranslator : Engine::kInterpreter;
}

bool Cpu::SetTranslation(const TranslationOptions& options)
{
    translation_ = options;
    interpret_next_ = false;
    translation_stats_ = TranslationStats{};
    switch (options.mode)
    {
        case TranslationMode::kOff:
            translator_.reset();
            return true;
        case TranslationMode::kEvaluator:
            translator_ = std::make_unique<translate::Translator>(
                std::make_unique<translate::EvaluatorBackend>(), options.threshold,
                options.max_block_instructions);
            return true;
        case TranslationMode::kWasm:
            if (!translate::wasm::Runnable() || options.wasm == nullptr)
            {
                // Refused, not imitated (AGENTS.md): translation stays off.
                translator_.reset();
                translation_.mode = TranslationMode::kOff;
                return false;
            }
            translator_ = std::make_unique<translate::Translator>(
                std::make_unique<translate::wasm::WasmBackend>(options.wasm), options.threshold,
                options.max_block_instructions);
            return true;
    }
    return false;
}

void Cpu::CompleteWasmModule(const std::uint32_t ticket, const std::uint32_t* table_indices,
                             const std::uint32_t count)
{
    if (translator_)
    {
        translator_->Complete(ticket, table_indices, count);
    }
    else if (translation_.wasm != nullptr && count != 0)
    {
        translation_.wasm->Release(table_indices, count);
    }
}

void Cpu::FailWasmModule(const std::uint32_t ticket)
{
    if (translator_)
    {
        translator_->Fail(ticket);
    }
}

TranslationStats Cpu::translation_stats() const
{
    TranslationStats stats = translation_stats_;
    stats.blocks_translated = translator_ ? translator_->translated_count() : 0;
    return stats;
}

std::size_t Cpu::EngineMemoryBytes() const
{
    return (decode_cache_ ? decode_cache_->FootprintBytes() : 0u) +
           (translator_ ? translator_->FootprintBytes() : 0u);
}

void Cpu::RegisterGate(std::uint32_t linear_address)
{
    gates_.insert(linear_address);
    // A gate inside a translated block would run without its check
    // (design #42, decision 5).
    if (translator_)
    {
        translator_->Flush();
    }
    if (gate_filter_.empty())
    {
        gate_filter_.assign(interp::GateFilter::kWords, 0);
    }
    const std::uint32_t bit = interp::GateFilter::Bit(linear_address);
    gate_filter_[bit >> 6] |= std::uint64_t{1} << (bit & 63u);
}

void Cpu::UnregisterGate(std::uint32_t linear_address)
{
    if (gates_.erase(linear_address) == 0)
    {
        return;
    }
    // Bits are shared, so the filter is rebuilt from what remains.
    gate_filter_.clear();
    if (gates_.empty())
    {
        return;
    }
    gate_filter_.assign(interp::GateFilter::kWords, 0);
    for (const std::uint32_t gate : gates_)
    {
        const std::uint32_t bit = interp::GateFilter::Bit(gate);
        gate_filter_[bit >> 6] |= std::uint64_t{1} << (bit & 63u);
    }
}

bool Cpu::IsGate(std::uint32_t linear_address) const
{
    return gates_.find(linear_address) != gates_.end();
}

void Cpu::RaiseInterrupt(std::uint8_t vector)
{
    // Runs on the thread that runs this Cpu (pending_interrupts_ is not
    // atomic), so a relaxed store suffices.
    pending_interrupts_.set(vector);
    attention_.value().store(true, std::memory_order_relaxed);
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
    RefreshAttention();
}

void Cpu::RefreshAttention()
{
    // RequestStop may run on another host thread: it stores the request
    // before raising attention, and this clears attention before reading
    // the request, so a request is never left without attention.
    attention_.value().store(pending_interrupts_.any());
    if (stop_requested_.value().load())
    {
        attention_.value().store(true);
    }
}

Event Cpu::Run(std::uint64_t step_budget)
{
    const Event event = RunUntilStop(step_budget);
    steps_total_ += event.steps;
    return event;
}

std::uint32_t Cpu::LinearEip() const
{
    const SegmentRegister& cs = state_.Seg(Segment::kCs);
    return cs.base + (state_.eip & (cs.default_32bit ? 0xFFFFFFFFu : 0xFFFFu));
}

bool Cpu::InstructionInProgress() const
{
    return in_progress_ && LinearEip() == in_progress_linear_;
}

Event Cpu::RunUntilStop(std::uint64_t step_budget)
{
    // The loop owns what is not an instruction's semantics: stop requests,
    // gates (checked before the instruction at the gate runs), external
    // interrupt delivery at boundaries with IF set, and the budget. The
    // x86 semantics live in interp::Step alone. A boundary is an
    // instruction boundary or one between a REP string's iterations
    // (design #32).
    Event event;
    std::uint64_t steps = 0;

    while (true)
    {
        if (stop_requested_.value().load(std::memory_order_relaxed))
        {
            stop_requested_.value().store(false);
            RefreshAttention();
            event.reason = StopReason::kStopRequested;
            event.steps = steps;
            return event;
        }

        // A REP string resumed between iterations already passed its gate
        // check when it started.
        const std::uint32_t linear = LinearEip();
        if (!gates_.empty() && IsGate(linear) &&
            !(in_progress_ && linear == in_progress_linear_))
        {
            event.reason = StopReason::kGate;
            event.gate_address = linear;
            event.steps = steps;
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
                event.steps = steps;
                return event;
            }
            Event fault_event;
            if (!interp::EnterInterrupt(state_, *memory_, *environment_,
                                        target_cs, target_eip, &fault_event))
            {
                fault_event.steps = steps;
                return fault_event;
            }
            ClearPendingInterrupt(vector);
            continue;
        }

        if (steps >= step_budget)
        {
            event.reason = StopReason::kBudgetExhausted;
            event.steps = steps;
            return event;
        }

        // A translated block at a block head (design #44, decision 1). It
        // runs only when the budget covers it whole; an exit that leaves an
        // instruction to the interpreter makes the next step interpreted.
        const interp::GateFilter filter{gate_filter_.data()};
        const interp::GateFilter* const gates = gate_filter_.empty() ? nullptr : &filter;
        if (translator_ && !interpret_next_ && !interrupt_shadow_ && !in_progress_ &&
            translate::ir::ModesSupported(state_))
        {
            const translate::Translation* translation =
                translator_->Lookup(state_, *memory_, features_, gates);
            if (translation != nullptr &&
                translation->block.instruction_count <= step_budget - steps)
            {
                const translate::ir::ExitResult exit =
                    translator_->Run(*translation, state_, *memory_);
                steps += exit.steps;
                interpret_next_ = exit.kind == translate::ir::ExitKind::kInterpret;
                ++translation_stats_.block_runs;
                translation_stats_.translated_steps += exit.steps;
                translation_stats_.interpreter_exits += interpret_next_ ? 1u : 0u;
                continue;
            }
        }

        // A block runs until something needs this loop again: the budget,
        // the end of the warm-up, attention (a stop request or a pending
        // interrupt), a possible gate, or an instruction that did not simply
        // retire (design #35). The first two and attention may stop a REP
        // string between its iterations (design #32).
        interp::BlockLimits limits;
        limits.max_steps = step_budget - steps;
        if (!decode_cache_)
        {
            const std::uint64_t run = steps_total_ + steps;
            if (run >= kDecodeCacheWarmup)
            {
                decode_cache_ = std::make_unique<interp::DecodeCache>();
            }
            else
            {
                limits.max_steps = std::min(limits.max_steps, kDecodeCacheWarmup - run);
            }
        }
        limits.attention = &attention_.value();
        limits.gates = gates;
        if (translator_)
        {
            // Every block head comes back to the dispatch above.
            limits.stop_after_branch = true;
            if (interpret_next_)
            {
                limits.max_steps = 1;
                interpret_next_ = false;
            }
        }
        const interp::BlockResult block =
            interp::RunBlock(state_, *memory_, *environment_, features_, decode_cache_.get(), limits);
        steps += block.steps;
        interrupt_shadow_ = block.last.inhibit_interrupts;
        // A REP string stopped between iterations continues at the next pass
        // of this loop, or in the next Run once the budget is spent.
        in_progress_ = block.last.status == interp::StepStatus::kPartial;
        if (in_progress_)
        {
            in_progress_linear_ = LinearEip();
            continue;
        }
        if (block.last.status != interp::StepStatus::kRetired)
        {
            event = block.last.event;
            event.steps = steps;
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
    stop_requested_.value().store(true);
    attention_.value().store(true);
}

void Cpu::InvalidateCode(std::uint32_t address, std::uint32_t size)
{
    if (memory_ != nullptr)
    {
        memory_->pages().Remove(address, size, PageFlag::kTranslated);
    }
}

}  // namespace rex86
