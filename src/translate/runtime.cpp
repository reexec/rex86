#include "translate/runtime.h"

#include <algorithm>
#include <utility>

#include "translate/ir/frontend.h"
#include "translate/ir/optimize.h"

namespace rex86::translate
{

Translator::Translator(std::unique_ptr<Backend> backend, const TranslatorLimits& limits)
    : backend_(std::move(backend)),
      threshold_(limits.threshold),
      max_block_instructions_(limits.max_block_instructions == 0 ? 1u : limits.max_block_instructions),
      batch_wait_(limits.batch_wait),
      max_modules_(limits.max_modules)
{
}

Translator::~Translator()
{
    Flush();
}

void Translator::Forget(Entry* entry, const std::uint32_t head)
{
    switch (entry->state)
    {
        case State::kTranslated:
            backend_->Drop(entry->translation);
            Leave(entry->translation.ticket);
            break;
        case State::kPending:
            // The installation stays in pending_: the host may still be
            // reading its bytes, and Complete or Fail discards this block's
            // part when it answers.
            Leave(entry->translation.ticket);
            break;
        case State::kQueued:
            queue_.erase(std::find(queue_.begin(), queue_.end(), head));
            break;
        case State::kCounting:
        case State::kUntranslatable:
            break;
    }
    *entry = Entry{};
}

void Translator::Leave(const std::uint32_t ticket)
{
    const auto module = modules_.find(ticket);
    if (module != modules_.end() && --module->second.live == 0)
    {
        modules_.erase(module);
    }
}

void Translator::EvictOldest()
{
    const auto oldest = modules_.begin();
    const std::uint32_t ticket = oldest->first;
    const std::vector<std::uint32_t> heads = oldest->second.heads;
    for (const std::uint32_t head : heads)
    {
        const auto found = entries_.find(head);
        if (found != entries_.end() &&
            (found->second.state == State::kTranslated || found->second.state == State::kPending) &&
            found->second.translation.ticket == ticket)
        {
            Forget(&found->second, head);
        }
    }
    modules_.erase(ticket);  // should its count have drifted
    ++modules_evicted_;
}

void Translator::Refuse(const std::uint32_t ticket, const std::vector<std::uint32_t>& heads)
{
    for (const std::uint32_t head : heads)
    {
        const auto found = entries_.find(head);
        if (found != entries_.end() && found->second.state == State::kPending &&
            found->second.translation.ticket == ticket)
        {
            found->second = Entry{};
            found->second.state = State::kUntranslatable;
            // No page has this generation: the block counts up to the
            // threshold and tries again, as the host may succeed later.
            found->second.refused_generation = 0xFFFFFFFFu;
        }
    }
    modules_.erase(ticket);
}

void Translator::Complete(const std::uint32_t ticket, const std::uint32_t* handles,
                          const std::uint32_t count)
{
    const auto waiting = pending_.find(ticket);
    if (waiting == pending_.end())
    {
        return;  // already answered
    }
    const std::vector<std::uint32_t> heads = std::move(waiting->second);
    pending_.erase(waiting);
    backend_->Finished(ticket);
    if (count != heads.size())
    {
        backend_->Discard(handles, count);
        Refuse(ticket, heads);
        return;
    }
    for (std::uint32_t i = 0; i < count; ++i)
    {
        const auto found = entries_.find(heads[i]);
        if (found == entries_.end() || found->second.state != State::kPending ||
            found->second.translation.ticket != ticket)
        {
            backend_->Discard(&handles[i], 1);
            continue;
        }
        found->second.translation.handle = handles[i];
        found->second.state = State::kTranslated;
        ++translated_;
        // The pages may have changed while it was installed: Lookup checks
        // the generations before every run.
    }
}

void Translator::Fail(const std::uint32_t ticket)
{
    const auto waiting = pending_.find(ticket);
    if (waiting == pending_.end())
    {
        return;
    }
    const std::vector<std::uint32_t> heads = std::move(waiting->second);
    pending_.erase(waiting);
    backend_->Finished(ticket);
    Refuse(ticket, heads);
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
    options.max_instructions = max_block_instructions_;
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
    entry->state = State::kQueued;
    entry->translation = std::move(translation);
    if (queue_.empty())
    {
        queue_since_ = lookups_;
    }
    queue_.push_back(state.eip);
    if (queue_.size() >= std::clamp(backend_->BatchCapacity(), 1u, kMaxBatchBlocks))
    {
        Submit(memory);
    }
}

void Translator::Submit(const GuestMemory& memory)
{
    std::vector<std::uint32_t> heads;
    std::vector<Translation*> batch;
    for (const std::uint32_t head : queue_)
    {
        Entry& entry = entries_.find(head)->second;
        if (!StillValid(entry.translation.block, memory))
        {
            entry = Entry{};  // changed while queued: counted again
            continue;
        }
        heads.push_back(head);
        batch.push_back(&entry.translation);
    }
    queue_.clear();
    if (batch.empty())
    {
        return;
    }
    while (max_modules_ != 0 && modules_.size() >= max_modules_)
    {
        EvictOldest();
    }
    const std::uint32_t ticket = next_ticket_++;
    for (Translation* translation : batch)
    {
        translation->ticket = ticket;
    }
    ++modules_submitted_;
    const auto count = static_cast<std::uint32_t>(batch.size());
    const CompileStatus status = backend_->Compile(batch.data(), count, ticket);
    for (const std::uint32_t head : heads)
    {
        Entry& entry = entries_.find(head)->second;
        switch (status)
        {
            case CompileStatus::kReady:
                entry.state = State::kTranslated;
                ++translated_;
                break;
            case CompileStatus::kPending:
                entry.state = State::kPending;
                break;
            case CompileStatus::kFailed:
                entry = Entry{};
                entry.state = State::kUntranslatable;
                entry.refused_generation = memory.pages().Generation(head);
                break;
        }
    }
    if (status == CompileStatus::kFailed)
    {
        return;
    }
    if (status == CompileStatus::kPending)
    {
        pending_[ticket] = heads;
    }
    modules_[ticket] = Module{std::move(heads), count};
}

const Translation* Translator::Lookup(const CpuState& state, GuestMemory& memory,
                                      const Features& features, const interp::GateFilter* gates)
{
    if (entries_.size() >= kMaxEntries && entries_.find(state.eip) == entries_.end())
    {
        Flush();
    }
    ++lookups_;
    if (!queue_.empty() && lookups_ - queue_since_ >= batch_wait_)
    {
        Submit(memory);
    }
    Entry& entry = entries_[state.eip];
    switch (entry.state)
    {
        case State::kTranslated:
            if (StillValid(entry.translation.block, memory))
            {
                return &entry.translation;
            }
            Forget(&entry, state.eip);
            break;
        case State::kQueued:
        case State::kPending:
            // The interpreter runs the block until its batch is installed.
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
        Forget(&item.second, item.first);
    }
    entries_.clear();
    queue_.clear();
    modules_.clear();
    // Tickets still out are discarded when they complete.
}

std::size_t Translator::FootprintBytes() const
{
    std::size_t bytes = backend_->FootprintBytes() + queue_.capacity() * sizeof(std::uint32_t);
    for (const auto& item : entries_)
    {
        bytes += sizeof(item) + item.second.translation.block.insts.capacity() * sizeof(ir::Inst);
    }
    for (const auto& item : modules_)
    {
        bytes += sizeof(item) + item.second.heads.capacity() * sizeof(std::uint32_t);
    }
    return bytes;
}

}  // namespace rex86::translate
