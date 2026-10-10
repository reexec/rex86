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

Translator::~Translator()
{
    Flush();
}

void Translator::Forget(Entry* entry)
{
    if (entry->state == State::kTranslated)
    {
        backend_->Drop(entry->translation);
    }
    // A pending installation stays in pending_: the host may still be reading
    // its bytes, and Complete or Fail discards it when it answers.
    *entry = Entry{};
}

void Translator::Complete(const std::uint32_t ticket, const std::uint32_t* handles,
                          const std::uint32_t count)
{
    const auto waiting = pending_.find(ticket);
    if (waiting == pending_.end())
    {
        return;  // already answered
    }
    Entry* entry = nullptr;
    const auto found = entries_.find(waiting->second);
    if (found != entries_.end() && found->second.state == State::kPending &&
        found->second.translation.ticket == ticket)
    {
        entry = &found->second;
    }
    pending_.erase(waiting);
    backend_->Finished(ticket);
    if (entry == nullptr || count == 0)
    {
        backend_->Discard(handles, count);
        return;
    }
    entry->translation.handle = handles[0];
    entry->state = State::kTranslated;
    ++translated_;
    // The pages may have changed while it was installed: Lookup checks the
    // generations before every run.
}

void Translator::Fail(const std::uint32_t ticket)
{
    const auto waiting = pending_.find(ticket);
    if (waiting == pending_.end())
    {
        return;
    }
    const auto found = entries_.find(waiting->second);
    pending_.erase(waiting);
    backend_->Finished(ticket);
    if (found != entries_.end() && found->second.state == State::kPending &&
        found->second.translation.ticket == ticket)
    {
        found->second = Entry{};
        found->second.state = State::kUntranslatable;
        found->second.refused_generation = 0xFFFFFFFFu;  // retried after any code change
    }
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
    translation.ticket = next_ticket_++;
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
            entry->state = State::kPending;
            entry->translation = std::move(translation);
            pending_[entry->translation.ticket] = state.eip;
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
            Forget(&entry);
            break;
        case State::kPending:
            // The interpreter runs the block until the host installs it.
            return nullptr;
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
    for (auto& item : entries_)
    {
        Forget(&item.second);
    }
    entries_.clear();
    // Tickets still out are discarded when they complete.
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
