#ifndef REX86_CPU_H_
#define REX86_CPU_H_

#include <atomic>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_set>
#include <vector>

#include "rex86/cpu_state.h"
#include "rex86/environment.h"
#include "rex86/guest_memory.h"

// One virtual CPU: a CpuState, the memory it runs in, and the host it talks
// to. A guest thread is one Cpu; the host schedules between them (re2DJ runs
// one guest thread at a time and switches inside imports, which is exactly
// this shape), so the core creates no host threads of its own.
namespace rex86
{

namespace interp
{
class DecodeCache;
}

enum class Engine : std::uint8_t
{
    // No engine is available in this build (task #1).
    kNone,
    kInterpreter,
    kTranslator,
};

class Cpu
{
public:
    // code_cache may be null: the core then never uses a translation backend
    // and runs on the interpreter alone.
    Cpu(GuestMemory* memory,
        Environment* environment,
        const Features& features,
        CodeCacheServices* code_cache = nullptr);
    ~Cpu();
    // Movable, not copyable: a Cpu owns its interpreter's decode cache.
    Cpu(Cpu&&) noexcept;
    Cpu& operator=(Cpu&&) noexcept;

    [[nodiscard]] CpuState& state()
    {
        return state_;
    }

    [[nodiscard]] const CpuState& state() const
    {
        return state_;
    }

    [[nodiscard]] GuestMemory* memory() const
    {
        return memory_;
    }

    [[nodiscard]] Environment* environment() const
    {
        return environment_;
    }

    // Null when the host gave no code cache, in which case no translation
    // backend is ever used.
    [[nodiscard]] CodeCacheServices* code_cache() const
    {
        return code_cache_;
    }

    [[nodiscard]] const Features& features() const
    {
        return features_;
    }

    // Which engine Run would use. kNone until an engine exists.
    [[nodiscard]] Engine ActiveEngine() const;

    // Bytes the engines hold beyond the Cpu itself: today the interpreter's
    // decode cache, made after a short warm-up (design #34); the
    // translation backends' code caches join it. For goal 7's resource
    // instrumentation.
    [[nodiscard]] std::size_t EngineMemoryBytes() const;

    // Gates: linear addresses at which execution stops with kGate before
    // the instruction there runs.
    void RegisterGate(std::uint32_t linear_address);
    void UnregisterGate(std::uint32_t linear_address);
    [[nodiscard]] bool IsGate(std::uint32_t linear_address) const;
    [[nodiscard]] std::size_t gate_count() const
    {
        return gates_.size();
    }

    // External interrupts. RaiseInterrupt marks the vector pending; the core
    // delivers it at the next instruction boundary where EFLAGS.IF is set
    // (and at STI, IRET and HLT), asking Environment::InterruptTarget where
    // to go. Highest vector first, as an 8259 in its default priority
    // ordering would present them to the CPU after the host's own ordering.
    void RaiseInterrupt(std::uint8_t vector);
    [[nodiscard]] bool HasPendingInterrupt() const;
    // The pending vector that would be delivered next, or false when none.
    bool NextPendingInterrupt(std::uint8_t* vector) const;
    void ClearPendingInterrupt(std::uint8_t vector);

    // Runs until a stop reason or until instruction_budget instructions have
    // retired. Without an engine it returns kNoEngine at once, retiring no
    // instruction.
    Event Run(std::uint64_t instruction_budget);
    // One instruction, for the comparison harness.
    Event Step();
    // Asks a Run on another host thread to return kStopRequested at its next
    // instruction boundary.
    void RequestStop();

    // Tells the engines that [address, address + size) changed under them,
    // as when the host writes code into guest memory. Clears kTranslated on
    // those pages, which raises their generation and drops every cached
    // decode or translation over them. The interpreter caches decodes too
    // (design #34), so a host changing code that has already run must call
    // this.
    void InvalidateCode(std::uint32_t address, std::uint32_t size);

private:
    // A flag RequestStop may set from another host thread. std::atomic is
    // not movable, so this moves by value to keep Cpu movable; moving a Cpu
    // while another thread uses it is a race regardless.
    class AtomicFlag
    {
    public:
        AtomicFlag() = default;
        AtomicFlag(AtomicFlag&& other) noexcept : value_(other.value_.load(std::memory_order_relaxed)) {}
        AtomicFlag& operator=(AtomicFlag&& other) noexcept
        {
            value_.store(other.value_.load(std::memory_order_relaxed), std::memory_order_relaxed);
            return *this;
        }

        [[nodiscard]] std::atomic<bool>& value()
        {
            return value_;
        }

    private:
        std::atomic<bool> value_{false};
    };

    Event RunUntilStop(std::uint64_t instruction_budget);
    // Recomputes attention_ from the pending interrupts and stop request.
    void RefreshAttention();

    CpuState state_;
    GuestMemory* memory_ = nullptr;
    Environment* environment_ = nullptr;
    CodeCacheServices* code_cache_ = nullptr;
    Features features_;
    std::unordered_set<std::uint32_t> gates_;
    // A bit filter over gates_ for the interpreter's block loop (design
    // #35); empty while there are no gates.
    std::vector<std::uint64_t> gate_filter_;
    std::bitset<256> pending_interrupts_;
    AtomicFlag stop_requested_;
    // Raised while a stop request or an interrupt is pending, so that the
    // interpreter's block loop returns to Run's checks (design #35).
    AtomicFlag attention_;
    // The interpreter's decode cache, made once the Cpu has retired
    // kDecodeCacheWarmup instructions so short-lived Cpus never pay for it.
    std::unique_ptr<interp::DecodeCache> decode_cache_;
    std::uint64_t retired_total_ = 0;
    // Set for one boundary after MOV SS, POP SS or an IF-enabling STI.
    bool interrupt_shadow_ = false;
};

}  // namespace rex86

#endif  // REX86_CPU_H_
