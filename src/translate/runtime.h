// The translation runtime (design #44, decision 3): one per Cpu while
// translation is on. It counts entries into block heads, translates hot
// blocks through the frontend and a backend, and hands the Cpu loop a
// translation that is still valid. Backends know only the IR.

#ifndef REX86_TRANSLATE_RUNTIME_H_
#define REX86_TRANSLATE_RUNTIME_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <unordered_map>
#include <vector>

#include "interp/interpreter.h"
#include "rex86/cpu_state.h"
#include "rex86/environment.h"
#include "rex86/guest_memory.h"
#include "translate/ir/evaluator.h"
#include "translate/ir/frontend.h"
#include "translate/ir/ir.h"

namespace rex86::translate
{

// A translated block: its optimized IR and whatever the backend keeps.
struct Translation
{
    ir::Block block;
    // The backend's: a wasm table index, for one.
    std::uint32_t handle = 0;
    // The batch it was compiled in (design #49), which also names an
    // asynchronous installation (design #45).
    std::uint32_t ticket = 0;
};

// The most blocks one batch holds (design #49): the asynchronous web
// adapter's export cap.
inline constexpr std::uint32_t kMaxBatchBlocks = 64;

enum class CompileStatus : std::uint8_t
{
    kReady,    // Run may be called now
    kPending,  // installed later (an asynchronous backend, #45)
    kFailed,   // never runnable; the block stays with the interpreter
};

class Backend
{
public:
    virtual ~Backend() = default;
    // Blocks one Compile takes together, 1 to kMaxBatchBlocks (design #49).
    [[nodiscard]] virtual std::uint32_t BatchCapacity() const { return 1; }
    // Compiles translations[0 .. count) as one unit named ticket. On kReady
    // every handle is set.
    virtual CompileStatus Compile(Translation* const* translations, std::uint32_t count,
                                  std::uint32_t ticket) = 0;
    virtual ir::ExitResult Run(const Translation& translation, CpuState& state,
                               GuestMemory& memory) = 0;
    // A ready translation is dropped: the backend may free what it holds.
    virtual void Drop(const Translation&) {}
    // A pending installation ended (completed, failed, or abandoned).
    virtual void Finished(std::uint32_t /*ticket*/) {}
    // Frees table entries an installation completed for a translation that
    // is gone.
    virtual void Discard(const std::uint32_t* /*handles*/, std::uint32_t /*count*/) {}
    // Bytes held beyond the translations' IR.
    [[nodiscard]] virtual std::size_t FootprintBytes() const { return 0; }
};

// The portable backend: runs the optimized IR with the evaluator.
class EvaluatorBackend final : public Backend
{
public:
    CompileStatus Compile(Translation* const*, std::uint32_t, std::uint32_t) override
    {
        return CompileStatus::kReady;
    }
    ir::ExitResult Run(const Translation& translation, CpuState& state,
                       GuestMemory& memory) override
    {
        return ir::Evaluate(translation.block, state, memory, &values_);
    }
    [[nodiscard]] std::size_t FootprintBytes() const override
    {
        return values_.capacity() * sizeof(std::uint32_t);
    }

private:
    std::vector<std::uint32_t> values_;
};

struct TranslatorLimits
{
    // Entries into a block head before it is translated.
    std::uint32_t threshold = 32;
    std::uint32_t max_block_instructions = ir::kMaxBlockInstructions;
    // Block entries the oldest queued block lets pass before a partial batch
    // is submitted (design #49, decision 2).
    std::uint32_t batch_wait = 4096;
    // Live modules (batches) beyond which the oldest is evicted; 0 for none
    // (design #49, decision 3).
    std::uint32_t max_modules = 0;
};

class Translator
{
public:
    // Entries beyond this drop every translation.
    static constexpr std::size_t kMaxEntries = 65536;

    Translator(std::unique_ptr<Backend> backend, const TranslatorLimits& limits);
    ~Translator();
    Translator(const Translator&) = delete;
    Translator& operator=(const Translator&) = delete;

    // An asynchronous backend's installation completed or failed. handles[i]
    // belongs to the batch's i-th block; one whose block is gone, or no
    // longer waits, is discarded.
    void Complete(std::uint32_t ticket, const std::uint32_t* handles, std::uint32_t count);
    void Fail(std::uint32_t ticket);

    // The runnable translation of the block at CS:EIP, or null. Counts an
    // entry, and translates the block once the count reaches the threshold.
    // The caller has checked ir::ModesSupported.
    // The common case, a translated block entered again with no batch due,
    // is inline through the cache (design #65); the rest is LookupSlow.
    const Translation* Lookup(const CpuState& state, GuestMemory& memory, const Features& features,
                              const interp::GateFilter* gates)
    {
        ++lookups_;
        if (queue_.empty() || lookups_ - queue_since_ < batch_wait_)
        {
            const CacheSlot& slot = cache_[CacheIndex(state.eip)];
            if (slot.entry != nullptr && slot.eip == state.eip && slot.entry->state == State::kTranslated &&
                StillValid(slot.entry->translation.block, memory))
            {
                return &slot.entry->translation;
            }
        }
        return LookupSlow(state, memory, features, gates);
    }

    ir::ExitResult Run(const Translation& translation, CpuState& state, GuestMemory& memory)
    {
        return backend_->Run(translation, state, memory);
    }

    // Drops every translation and count.
    void Flush();

    [[nodiscard]] std::size_t FootprintBytes() const;
    [[nodiscard]] std::size_t entry_count() const { return entries_.size(); }
    [[nodiscard]] std::uint64_t translated_count() const { return translated_; }
    [[nodiscard]] std::uint64_t modules_submitted() const { return modules_submitted_; }
    [[nodiscard]] std::uint64_t modules_evicted() const { return modules_evicted_; }
    [[nodiscard]] std::uint64_t invalidated_count() const { return invalidated_; }
    [[nodiscard]] std::size_t live_modules() const { return modules_.size(); }
    [[nodiscard]] std::size_t queued() const { return queue_.size(); }

private:
    enum class State : std::uint8_t
    {
        kCounting,
        kQueued,  // formed, waiting in the batch queue (design #49)
        kPending,
        kTranslated,
        kUntranslatable,
    };

    struct Entry
    {
        State state = State::kCounting;
        std::uint32_t count = 0;
        // For kUntranslatable: the head's page generation when refused, so
        // that a change to the code retries.
        std::uint32_t refused_generation = 0;
        // For kCounting: the head's page generation the count belongs to.
        // The count is of entries during which that page kept its code, so
        // a write to it starts the count over (design #60).
        std::uint32_t count_generation = 0;
        // Times this block's translation was dropped, by the module budget
        // (design #49, decision 3) or because its code changed (design #60):
        // each doubles the entries it needs before it is translated again
        // (from at least one), up to kMaxBackoffShift, so that neither a
        // working set beyond the budget nor code rewritten while it runs
        // translates without end.
        std::uint8_t backoff = 0;
        Translation translation;
    };

    static constexpr std::uint8_t kMaxBackoffShift = 6;

    // A direct-mapped cache in front of entries_ (design #65): the
    // dispatch's common case, a translated block entered again, skips the
    // hash. An entry's address is stable until Flush, which clears this.
    struct CacheSlot
    {
        std::uint32_t eip = 0;
        Entry* entry = nullptr;
    };
    static constexpr std::uint32_t kCacheBits = 10;

    static std::uint32_t CacheIndex(const std::uint32_t eip)
    {
        return (eip * 0x9E3779B1u) >> (32u - kCacheBits);
    }

    // A submitted batch (design #49, decision 3): its blocks' heads in
    // batch order, and how many of them still hold its code.
    struct Module
    {
        std::vector<std::uint32_t> heads;
        std::uint32_t live = 0;
    };

    // A store into translated code, InvalidateCode or the host's own page
    // changes clear kTranslated, which raises the generation (design #34,
    // decision 2).
    static bool StillValid(const ir::Block& block, const GuestMemory& memory)
    {
        for (std::uint32_t i = 0; i < block.page_count; ++i)
        {
            if (memory.pages().Generation(block.pages[i]) != block.generations[i])
            {
                return false;
            }
        }
        return true;
    }
    // Lookup past the cache: a batch due, a miss, counting and translating.
    const Translation* LookupSlow(const CpuState& state, GuestMemory& memory, const Features& features,
                                  const interp::GateFilter* gates);
    // Resets the entry of the block at head, telling the backend, the batch
    // queue and the module records about what it held; keep_backoff keeps
    // its backoff, which only Flush clears.
    void Forget(Entry* entry, std::uint32_t head, bool keep_backoff = false);
    // One more doubling of the entries the block needs, up to the cap.
    static void BackOff(Entry* entry);
    void Translate(Entry* entry, const CpuState& state, GuestMemory& memory,
                   const Features& features, const interp::GateFilter* gates);
    // Hands the queued blocks still valid to the backend as one batch.
    void Submit(const GuestMemory& memory);
    // A member of the module is gone.
    void Leave(std::uint32_t ticket);
    void EvictOldest();
    // The blocks of a batch that never installs.
    void Refuse(std::uint32_t ticket, const std::vector<std::uint32_t>& heads);

    std::unique_ptr<Backend> backend_;
    std::uint32_t threshold_;
    std::uint32_t max_block_instructions_;
    std::uint32_t batch_wait_;
    std::uint32_t max_modules_;
    std::unordered_map<std::uint32_t, Entry> entries_;
    std::array<CacheSlot, std::size_t{1} << kCacheBits> cache_{};
    // Heads of kQueued blocks, oldest first, and the lookup count when the
    // oldest joined.
    std::vector<std::uint32_t> queue_;
    std::uint64_t queue_since_ = 0;
    std::uint64_t lookups_ = 0;
    // Live modules, oldest (lowest ticket) first.
    std::map<std::uint32_t, Module> modules_;
    // Pending installations: ticket to the batch's heads.
    std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> pending_;
    std::uint32_t next_ticket_ = 1;
    std::uint64_t translated_ = 0;
    std::uint64_t modules_submitted_ = 0;
    std::uint64_t modules_evicted_ = 0;
    // Translations, queued or installed, dropped because their code changed.
    std::uint64_t invalidated_ = 0;
};

}  // namespace rex86::translate

#endif  // REX86_TRANSLATE_RUNTIME_H_
