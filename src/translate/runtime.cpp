#include "translate/runtime.h"

#include <utility>

#include "translate/ir/frontend.h"
#include "translate/ir/optimize.h"

namespace rex86::translate
{

Translator::Translator(std::unique_ptr<Backend> backend, const std::uint32_t threshold)
    : backend_(std::move(backend)), threshold_(threshold)
{
}

bool Translator::StillValid(const ir::Block& block, const GuestMemory& memory)
{
    // A store into translated code, InvalidateCode or the host's own page
    // changes clear kTranslated, which raises the generation (design #34,
    // decision 2).
    for (std::uint32_t i = 0; i < block.page_count; ++i)
    {
        if (memory.pages().Generation(block.pages[i]) != block.generations[i])
        {
            return false;
        }
    }
    return true;
}

void Translator::Translate(Entry* entry, const CpuState& state, GuestMemory& memory,
                           const Features& features, const interp::GateFilter* gates)
{
    ir::FrontendOptions options;
    options.gates = gates;
    Translation translation;
    if (!ir::FormBlock(state, memory, features, options, &translation.block))
    {
        entry->state = State::kUntranslatable;
        entry->refused_generation = memory.pages().Generation(state.eip);
        return;
    }
    ir::Optimize(&translation.block);
    // A store to these pages now invalidates the translation; the block's
    // own stores into them leave the instruction to the interpreter.
    for (std::uint32_t i = 0; i < translation.block.page_count; ++i)
    {
        memory.pages().Add(translation.block.pages[i], kGuestPageSize, PageFlag::kTranslated);
    }
    switch (backend_->Compile(&translation))
    {
        case CompileStatus::kReady:
            entry->state = State::kTranslated;
            entry->translation = std::move(translation);
            ++translated_;
            break;
        case CompileStatus::kPending:
            // No asynchronous backend exists yet (#45); counted again.
            entry->count = 0;
            break;
        case CompileStatus::kFailed:
            entry->state = State::kUntranslatable;
            entry->refused_generation = memory.pages().Generation(state.eip);
            break;
    }
}

const Translation* Translator::Lookup(const CpuState& state, GuestMemory& memory,
                                      const Features& features, const interp::GateFilter* gates)
{
    if (entries_.size() >= kMaxEntries && entries_.find(state.eip) == entries_.end())
    {
        Flush();
    }
    Entry& entry = entries_[state.eip];
    switch (entry.state)
    {
        case State::kTranslated:
            if (StillValid(entry.translation.block, memory))
            {
                return &entry.translation;
            }
            entry = Entry{};
            break;
        case State::kUntranslatable:
            if (memory.pages().Generation(state.eip) == entry.refused_generation)
            {
                return nullptr;
            }
            entry = Entry{};
            break;
        case State::kCounting:
            break;
    }
    if (entry.count < threshold_)
    {
        ++entry.count;
        return nullptr;
    }
    Translate(&entry, state, memory, features, gates);
    return entry.state == State::kTranslated ? &entry.translation : nullptr;
}

void Translator::Flush()
{
    entries_.clear();
}

std::size_t Translator::FootprintBytes() const
{
    std::size_t bytes = backend_->FootprintBytes();
    for (const auto& item : entries_)
    {
        bytes += sizeof(item) + item.second.translation.block.insts.capacity() * sizeof(ir::Inst);
    }
    return bytes;
}

}  // namespace rex86::translate
