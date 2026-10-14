// The translation runtime's batches and module budget (design #49),
// driven directly through a backend that records what it is given.

#include <cstdint>
#include <memory>
#include <vector>

#include "rex86/cpu.h"
#include "test_support.h"
#include "translate/ir/evaluator.h"
#include "translate/runtime.h"

namespace
{

namespace translate = rex86::translate;

class QuietEnvironment final : public rex86::Environment
{
public:
    bool LoadDescriptor(std::uint16_t, rex86::Descriptor* d) override
    {
        *d = rex86::Descriptor{};
        return true;
    }
    bool PortRead(std::uint16_t, std::uint8_t, std::uint32_t*) override { return false; }
    bool PortWrite(std::uint16_t, std::uint8_t, std::uint32_t) override { return false; }
    bool InterruptTarget(std::uint8_t, std::uint16_t*, std::uint32_t*) override { return false; }
    std::uint64_t ReadTimeStampCounter() override { return 0; }
    void Cpuid(std::uint32_t, std::uint32_t, std::uint32_t r[4]) override
    {
        r[0] = r[1] = r[2] = r[3] = 0;
    }
};

// What the backend saw, kept outside it since the Translator owns it.
struct Log
{
    translate::CompileStatus answer = translate::CompileStatus::kReady;
    std::vector<std::uint32_t> batch_sizes;
    std::vector<std::uint32_t> tickets;
    std::vector<std::uint32_t> dropped;
    std::uint32_t discarded = 0;
    std::uint32_t finished = 0;
    std::uint32_t next_handle = 100;
};

class RecordingBackend final : public translate::Backend
{
public:
    RecordingBackend(Log* log, const std::uint32_t capacity) : log_(log), capacity_(capacity) {}

    [[nodiscard]] std::uint32_t BatchCapacity() const override { return capacity_; }
    translate::CompileStatus Compile(translate::Translation* const* translations,
                                     const std::uint32_t count, const std::uint32_t ticket) override
    {
        log_->batch_sizes.push_back(count);
        log_->tickets.push_back(ticket);
        if (log_->answer == translate::CompileStatus::kReady)
        {
            for (std::uint32_t i = 0; i < count; ++i)
            {
                translations[i]->handle = log_->next_handle++;
            }
        }
        return log_->answer;
    }
    translate::ir::ExitResult Run(const translate::Translation& translation, rex86::CpuState& state,
                                  rex86::GuestMemory& memory, const translate::RunLimits&) override
    {
        return translate::ir::Evaluate(translation.block, state, memory);
    }
    void Drop(const translate::Translation& translation) override
    {
        log_->dropped.push_back(translation.handle);
    }
    void Finished(std::uint32_t) override { ++log_->finished; }
    void Discard(const std::uint32_t*, const std::uint32_t count) override { log_->discarded += count; }

private:
    Log* log_;
    std::uint32_t capacity_;
};

// Blocks of one JMP SHORT to the next instruction each: kPerPage on each of
// the two code pages.
constexpr std::uint32_t kPageA = 0x1000;
constexpr std::uint32_t kPageB = 0x2000;
constexpr std::uint32_t kPerPage = 16;

struct Rig
{
    std::vector<std::uint8_t> buffer = std::vector<std::uint8_t>(0x10000, 0xF4);  // hlt
    rex86::GuestMemory memory{buffer.data(), 0x10000};
    QuietEnvironment environment;
    rex86::Cpu cpu{&memory, &environment, rex86::Features{}};
    rex86::CpuState state;
    Log log;
    std::unique_ptr<translate::Translator> translator;

    Rig(const std::uint32_t capacity, const translate::TranslatorLimits& limits)
    {
        memory.pages().Set(0, 0x10000, rex86::kPageReadWriteExecute);
        for (const std::uint32_t page : {kPageA, kPageB})
        {
            for (std::uint32_t i = 0; i < kPerPage; ++i)
            {
                buffer[page + 2 * i] = 0xEB;  // jmp short +0
                buffer[page + 2 * i + 1] = 0x00;
            }
        }
        cpu.state().Seg(rex86::Segment::kCs).executable = true;
        state = cpu.state();
        translator = std::make_unique<translate::Translator>(
            std::make_unique<RecordingBackend>(&log, capacity), limits);
    }

    static std::uint32_t Head(const std::uint32_t page, const std::uint32_t i) { return page + 2 * i; }

    const translate::Translation* Enter(const std::uint32_t head)
    {
        state.eip = head;
        return translator->Lookup(state, memory, rex86::Features{}, nullptr);
    }
};

translate::TranslatorLimits Limits(const std::uint32_t batch_wait, const std::uint32_t max_modules)
{
    translate::TranslatorLimits limits;
    limits.threshold = 0;
    limits.batch_wait = batch_wait;
    limits.max_modules = max_modules;
    return limits;
}

// A full batch goes to the backend at once, as one ticket; until then the
// interpreter runs the queued blocks.
void FullBatchTests(rex86::test::Context& context)
{
    Rig rig(4, Limits(1000, 0));
    for (std::uint32_t i = 0; i < 3; ++i)
    {
        REX86_CHECK(context, rig.Enter(Rig::Head(kPageA, i)) == nullptr);
    }
    REX86_CHECK_EQ(context, rig.translator->queued(), std::size_t{3});
    REX86_CHECK(context, rig.log.batch_sizes.empty());
    // A queued block entered again stays with the interpreter.
    REX86_CHECK(context, rig.Enter(Rig::Head(kPageA, 0)) == nullptr);
    const translate::Translation* fourth = rig.Enter(Rig::Head(kPageA, 3));
    REX86_CHECK(context, fourth != nullptr);
    REX86_CHECK(context, (rig.log.batch_sizes == std::vector<std::uint32_t>{4}));
    REX86_CHECK_EQ(context, rig.translator->queued(), std::size_t{0});
    REX86_CHECK_EQ(context, rig.translator->live_modules(), std::size_t{1});
    REX86_CHECK_EQ(context, rig.translator->modules_submitted(), std::uint64_t{1});
    REX86_CHECK_EQ(context, rig.translator->translated_count(), std::uint64_t{4});
    const translate::Translation* first = rig.Enter(Rig::Head(kPageA, 0));
    REX86_CHECK(context, first != nullptr && fourth != nullptr && first->ticket == fourth->ticket);
}

// A partial batch goes once batch_wait entries have passed since its oldest
// block joined.
void PartialBatchTests(rex86::test::Context& context)
{
    Rig rig(4, Limits(3, 0));
    REX86_CHECK(context, rig.Enter(Rig::Head(kPageA, 0)) == nullptr);  // queued
    REX86_CHECK(context, rig.Enter(Rig::Head(kPageA, 0)) == nullptr);
    REX86_CHECK(context, rig.Enter(Rig::Head(kPageA, 0)) == nullptr);
    REX86_CHECK(context, rig.log.batch_sizes.empty());
    REX86_CHECK(context, rig.Enter(Rig::Head(kPageA, 0)) != nullptr);
    REX86_CHECK(context, (rig.log.batch_sizes == std::vector<std::uint32_t>{1}));
}

// A queued block whose page changed is left out of the batch and counted
// again; the others go.
void StaleQueueTests(rex86::test::Context& context)
{
    Rig rig(3, Limits(1000, 0));
    REX86_CHECK(context, rig.Enter(Rig::Head(kPageA, 0)) == nullptr);
    REX86_CHECK(context, rig.Enter(Rig::Head(kPageB, 0)) == nullptr);
    rig.memory.pages().Remove(kPageA, 0x1000, rex86::PageFlag::kTranslated);  // raises the generation
    REX86_CHECK(context, rig.Enter(Rig::Head(kPageB, 1)) != nullptr);
    REX86_CHECK(context, (rig.log.batch_sizes == std::vector<std::uint32_t>{2}));
    REX86_CHECK_EQ(context, rig.translator->invalidated_count(), std::uint64_t{1});
    // Page A's block is counted again, backing off (design #60): 1 << 1
    // entries, then queued.
    REX86_CHECK(context, rig.Enter(Rig::Head(kPageA, 0)) == nullptr);
    REX86_CHECK(context, rig.Enter(Rig::Head(kPageA, 0)) == nullptr);
    REX86_CHECK_EQ(context, rig.translator->queued(), std::size_t{0});
    REX86_CHECK(context, rig.Enter(Rig::Head(kPageA, 0)) == nullptr);
    REX86_CHECK_EQ(context, rig.translator->queued(), std::size_t{1});
}

// An asynchronous batch: handles match blocks in order; blocks gone before
// the answer give their handles back; a count that does not match fails
// the batch.
void PendingBatchTests(rex86::test::Context& context)
{
    Rig rig(3, Limits(1000, 0));
    rig.log.answer = translate::CompileStatus::kPending;
    for (std::uint32_t i = 0; i < 3; ++i)
    {
        REX86_CHECK(context, rig.Enter(Rig::Head(kPageA, i)) == nullptr);
    }
    REX86_CHECK_EQ(context, rig.log.tickets.size(), std::size_t{1});
    const std::uint32_t first_ticket = rig.log.tickets.at(0);
    REX86_CHECK(context, rig.Enter(Rig::Head(kPageA, 1)) == nullptr);  // still pending
    // Page B's blocks form a second batch, then page A's second block's code
    // changes before the first answer.
    for (std::uint32_t i = 0; i < 3; ++i)
    {
        rig.Enter(Rig::Head(kPageB, i));
    }
    const std::uint32_t second_ticket = rig.log.tickets.at(1);
    rig.memory.pages().Remove(kPageA, 0x1000, rex86::PageFlag::kTranslated);
    rig.Enter(Rig::Head(kPageA, 1));  // stale only once installed: nothing changes yet

    const std::uint32_t handles[] = {7, 8, 9};
    rig.translator->Complete(first_ticket, handles, 3);
    REX86_CHECK_EQ(context, rig.log.finished, std::uint32_t{1});
    REX86_CHECK_EQ(context, rig.translator->translated_count(), std::uint64_t{3});
    // Installed, but its page changed: dropped at the next entry.
    REX86_CHECK(context, rig.Enter(Rig::Head(kPageA, 2)) == nullptr);
    REX86_CHECK(context, (rig.log.dropped == std::vector<std::uint32_t>{9}));
    rig.translator->Complete(first_ticket, handles, 3);  // answered already: ignored
    REX86_CHECK_EQ(context, rig.log.finished, std::uint32_t{1});

    // A count that does not match the batch fails it: its handles come
    // back, and its blocks count up to the threshold and try again.
    rig.translator->Complete(second_ticket, handles, 2);
    REX86_CHECK_EQ(context, rig.log.discarded, std::uint32_t{2});
    REX86_CHECK_EQ(context, rig.translator->live_modules(), std::size_t{1});
    REX86_CHECK(context, rig.Enter(Rig::Head(kPageB, 0)) == nullptr);
    // Page B's first is queued; page A's third, dropped above, backs off
    // (design #60).
    REX86_CHECK_EQ(context, rig.translator->queued(), std::size_t{1});
    REX86_CHECK_EQ(context, rig.log.tickets.size(), std::size_t{2});

    // A batch flushed before its answer: every handle comes back.
    Rig flushed(2, Limits(1000, 0));
    flushed.log.answer = translate::CompileStatus::kPending;
    flushed.Enter(Rig::Head(kPageA, 0));
    flushed.Enter(Rig::Head(kPageA, 1));
    flushed.translator->Flush();
    REX86_CHECK_EQ(context, flushed.translator->live_modules(), std::size_t{0});
    flushed.translator->Complete(flushed.log.tickets.at(0), handles, 2);
    REX86_CHECK_EQ(context, flushed.log.discarded, std::uint32_t{2});
    REX86_CHECK_EQ(context, flushed.translator->translated_count(), std::uint64_t{0});
}

// At the budget the oldest module goes whole; its blocks translate again
// when entered.
void BudgetTests(rex86::test::Context& context)
{
    Rig rig(2, Limits(1000, 2));
    for (std::uint32_t i = 0; i < 4; ++i)
    {
        rig.Enter(Rig::Head(kPageA, i));  // two modules: {0, 1}, {2, 3}
    }
    REX86_CHECK_EQ(context, rig.translator->live_modules(), std::size_t{2});
    REX86_CHECK(context, rig.log.dropped.empty());
    rig.Enter(Rig::Head(kPageA, 4));
    REX86_CHECK(context, rig.Enter(Rig::Head(kPageA, 5)) != nullptr);  // evicts {0, 1}
    REX86_CHECK_EQ(context, rig.translator->modules_evicted(), std::uint64_t{1});
    REX86_CHECK(context, (rig.log.dropped == std::vector<std::uint32_t>{100, 101}));
    REX86_CHECK_EQ(context, rig.translator->live_modules(), std::size_t{2});
    // Block 2 still runs; block 0 is counted again (two entries, its
    // backoff) and queued.
    REX86_CHECK(context, rig.Enter(Rig::Head(kPageA, 2)) != nullptr);
    rig.Enter(Rig::Head(kPageA, 0));
    rig.Enter(Rig::Head(kPageA, 0));
    REX86_CHECK_EQ(context, rig.translator->queued(), std::size_t{0});
    REX86_CHECK(context, rig.Enter(Rig::Head(kPageA, 0)) == nullptr);
    REX86_CHECK_EQ(context, rig.translator->queued(), std::size_t{1});

    // A module whose blocks are all gone no longer counts against the budget.
    Rig left(1, Limits(1000, 2));
    left.Enter(Rig::Head(kPageA, 0));
    left.Enter(Rig::Head(kPageB, 0));
    left.memory.pages().Remove(kPageA, 0x1000, rex86::PageFlag::kTranslated);
    left.Enter(Rig::Head(kPageA, 0));  // dropped: its module goes
    REX86_CHECK_EQ(context, left.translator->live_modules(), std::size_t{1});
    left.Enter(Rig::Head(kPageA, 0));
    left.Enter(Rig::Head(kPageA, 0));  // installed again after its backoff
    REX86_CHECK_EQ(context, left.translator->live_modules(), std::size_t{2});
    REX86_CHECK_EQ(context, left.translator->modules_evicted(), std::uint64_t{0});
}

// An evicted block needs twice the entries before it is translated again,
// so that a working set beyond the budget does not translate without end.
void EvictionBackoffTests(rex86::test::Context& context)
{
    translate::TranslatorLimits limits = Limits(1000, 1);
    limits.threshold = 2;
    Rig rig(1, limits);
    const auto enter = [&rig](const std::uint32_t head, const int times) {
        const translate::Translation* last = nullptr;
        for (int i = 0; i < times; ++i)
        {
            last = rig.Enter(head);
        }
        return last;
    };
    REX86_CHECK(context, enter(Rig::Head(kPageA, 0), 2) == nullptr);
    REX86_CHECK(context, enter(Rig::Head(kPageA, 0), 1) != nullptr);  // the third entry
    REX86_CHECK(context, enter(Rig::Head(kPageB, 0), 3) != nullptr);  // evicts page A's
    REX86_CHECK_EQ(context, rig.translator->modules_evicted(), std::uint64_t{1});
    REX86_CHECK(context, enter(Rig::Head(kPageA, 0), 4) == nullptr);  // 2 << 1 entries
    REX86_CHECK(context, enter(Rig::Head(kPageA, 0), 1) != nullptr);  // evicts page B's
    REX86_CHECK(context, enter(Rig::Head(kPageB, 0), 4) == nullptr);
    REX86_CHECK(context, enter(Rig::Head(kPageB, 0), 1) != nullptr);  // evicts page A's again
    REX86_CHECK(context, enter(Rig::Head(kPageA, 0), 8) == nullptr);  // 2 << 2 entries
    REX86_CHECK(context, enter(Rig::Head(kPageA, 0), 1) != nullptr);

    // A threshold of 0 backs off from one entry: 1 << 1, then 1 << 2.
    Rig forced(1, Limits(1000, 1));
    REX86_CHECK(context, forced.Enter(Rig::Head(kPageA, 0)) != nullptr);
    REX86_CHECK(context, forced.Enter(Rig::Head(kPageB, 0)) != nullptr);  // evicts page A's
    REX86_CHECK(context, forced.Enter(Rig::Head(kPageA, 0)) == nullptr);
    REX86_CHECK(context, forced.Enter(Rig::Head(kPageA, 0)) == nullptr);
    REX86_CHECK(context, forced.Enter(Rig::Head(kPageA, 0)) != nullptr);  // the third entry
}

// A translation dropped because its code changed backs off like an evicted
// one, and entries count only while the head's page keeps its code
// (design #60).
void InvalidationBackoffTests(rex86::test::Context& context)
{
    translate::TranslatorLimits limits = Limits(1000, 0);
    limits.threshold = 2;
    Rig rig(1, limits);
    const auto enter = [&rig](const std::uint32_t head, const int times) {
        const translate::Translation* last = nullptr;
        for (int i = 0; i < times; ++i)
        {
            last = rig.Enter(head);
        }
        return last;
    };
    const auto write_page_a = [&rig] {
        rig.memory.pages().Add(kPageA, 0x1000, rex86::PageFlag::kTranslated);
        rig.memory.pages().Remove(kPageA, 0x1000, rex86::PageFlag::kTranslated);
    };
    REX86_CHECK(context, enter(Rig::Head(kPageA, 0), 2) == nullptr);
    REX86_CHECK(context, enter(Rig::Head(kPageA, 0), 1) != nullptr);
    write_page_a();
    REX86_CHECK(context, enter(Rig::Head(kPageA, 0), 4) == nullptr);  // dropped, then 2 << 1
    REX86_CHECK_EQ(context, rig.translator->invalidated_count(), std::uint64_t{1});
    REX86_CHECK(context, enter(Rig::Head(kPageA, 0), 1) != nullptr);
    REX86_CHECK(context, (rig.log.dropped == std::vector<std::uint32_t>{100}));
    write_page_a();
    REX86_CHECK(context, enter(Rig::Head(kPageA, 0), 8) == nullptr);  // 2 << 2
    REX86_CHECK(context, enter(Rig::Head(kPageA, 0), 1) != nullptr);

    // A write to the page while the block counts starts the count over.
    Rig counting(1, limits);
    counting.Enter(Rig::Head(kPageA, 0));
    counting.Enter(Rig::Head(kPageA, 0));
    counting.memory.pages().Add(kPageA, 0x1000, rex86::PageFlag::kTranslated);
    counting.memory.pages().Remove(kPageA, 0x1000, rex86::PageFlag::kTranslated);
    REX86_CHECK(context, counting.Enter(Rig::Head(kPageA, 0)) == nullptr);
    REX86_CHECK(context, counting.Enter(Rig::Head(kPageA, 0)) == nullptr);
    REX86_CHECK(context, counting.Enter(Rig::Head(kPageA, 0)) != nullptr);
    REX86_CHECK_EQ(context, counting.translator->invalidated_count(), std::uint64_t{0});

    // The backoff stops doubling at 64 times the threshold.
    translate::TranslatorLimits one = Limits(1000, 0);
    one.threshold = 1;
    Rig capped(1, one);
    for (int round = 0; round < 8; ++round)
    {
        const translate::Translation* translated = nullptr;
        for (int i = 0; i < 200 && translated == nullptr; ++i)
        {
            translated = capped.Enter(Rig::Head(kPageA, 0));
        }
        REX86_CHECK(context, translated != nullptr);
        capped.memory.pages().Remove(kPageA, 0x1000, rex86::PageFlag::kTranslated);
    }
    int entries = 0;
    while (capped.Enter(Rig::Head(kPageA, 0)) == nullptr && entries < 1000)
    {
        ++entries;
    }
    REX86_CHECK_EQ(context, entries, 64);  // 1 << 6, the dropping entry the first
}

// Flush empties the queue and the module records.
void FlushTests(rex86::test::Context& context)
{
    Rig rig(2, Limits(1000, 0));
    rig.Enter(Rig::Head(kPageA, 0));
    rig.Enter(Rig::Head(kPageA, 1));  // a module
    rig.Enter(Rig::Head(kPageA, 2));  // queued
    rig.translator->Flush();
    REX86_CHECK_EQ(context, rig.translator->queued(), std::size_t{0});
    REX86_CHECK_EQ(context, rig.translator->live_modules(), std::size_t{0});
    REX86_CHECK(context, (rig.log.dropped == std::vector<std::uint32_t>{100, 101} ||
                          rig.log.dropped == std::vector<std::uint32_t>{101, 100}));
    REX86_CHECK(context, rig.Enter(Rig::Head(kPageA, 2)) == nullptr);
    REX86_CHECK_EQ(context, rig.translator->queued(), std::size_t{1});
}

}  // namespace

void RunTranslateBatchTests(rex86::test::Context& context)
{
    FullBatchTests(context);
    PartialBatchTests(context);
    StaleQueueTests(context);
    PendingBatchTests(context);
    BudgetTests(context);
    EvictionBackoffTests(context);
    InvalidationBackoffTests(context);
    FlushTests(context);
}
