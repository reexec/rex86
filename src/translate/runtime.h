// The translation runtime (design #44, decision 3): one per Cpu while
// translation is on. It counts entries into block heads, translates hot
// blocks through the frontend and a backend, and hands the Cpu loop a
// translation that is still valid. Backends know only the IR.

#ifndef REX86_TRANSLATE_RUNTIME_H_
#define REX86_TRANSLATE_RUNTIME_H_

#include <cstddef>
#include <cstdint>
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
    // Names an asynchronous installation (design #45).
    std::uint32_t ticket = 0;
};

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
    virtual CompileStatus Compile(Translation* translation) = 0;
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
    CompileStatus Compile(Translation*) override { return CompileStatus::kReady; }
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

class Translator
{
public:
    // Entries beyond this drop every translation.
    static constexpr std::size_t kMaxEntries = 65536;

    Translator(std::unique_ptr<Backend> backend, std::uint32_t threshold,
               std::uint32_t max_block_instructions = ir::kMaxBlockInstructions);
    ~Translator();
    Translator(const Translator&) = delete;
    Translator& operator=(const Translator&) = delete;

    // An asynchronous backend's installation completed or failed. A ticket
    // whose head is gone, or no longer waits, is discarded.
    void Complete(std::uint32_t ticket, const std::uint32_t* handles, std::uint32_t count);
    void Fail(std::uint32_t ticket);

    // The runnable translation of the block at CS:EIP, or null. Counts an
    // entry, and translates the block once the count reaches the threshold.
    // The caller has checked ir::ModesSupported.
    const Translation* Lookup(const CpuState& state, GuestMemory& memory, const Features& features,
                              const interp::GateFilter* gates);

    ir::ExitResult Run(const Translation& translation, CpuState& state, GuestMemory& memory)
    {
        return backend_->Run(translation, state, memory);
    }

    // Drops every translation and count.
    void Flush();

    [[nodiscard]] std::size_t FootprintBytes() const;
    [[nodiscard]] std::size_t entry_count() const { return entries_.size(); }
    [[nodiscard]] std::uint64_t translated_count() const { return translated_; }

private:
    enum class State : std::uint8_t
    {
        kCounting,
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
        Translation translation;
    };

    static bool StillValid(const ir::Block& block, const GuestMemory& memory);
    // Resets an entry, telling the backend about what it held.
    void Forget(Entry* entry);
    void Translate(Entry* entry, const CpuState& state, GuestMemory& memory,
                   const Features& features, const interp::GateFilter* gates);

    std::unique_ptr<Backend> backend_;
    std::uint32_t threshold_;
    std::uint32_t max_block_instructions_;
    std::unordered_map<std::uint32_t, Entry> entries_;
    // Pending installations: ticket to block head.
    std::unordered_map<std::uint32_t, std::uint32_t> pending_;
    std::uint32_t next_ticket_ = 1;
    std::uint64_t translated_ = 0;
};

}  // namespace rex86::translate

#endif  // REX86_TRANSLATE_RUNTIME_H_
