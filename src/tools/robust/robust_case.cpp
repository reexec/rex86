#include "tools/robust/robust_case.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>

#include "decode/decoder.h"
#include "rex86/cpu.h"

// Under AddressSanitizer the guard zones are poisoned, so a read beyond
// the guest buffer is caught too, not only a write (design #31, I2).
#if defined(__SANITIZE_ADDRESS__)
#define REX86_ROBUST_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define REX86_ROBUST_ASAN 1
#endif
#endif
#if defined(REX86_ROBUST_ASAN)
#include <sanitizer/asan_interface.h>
#endif

namespace rex86::robust
{

namespace
{

constexpr std::uint32_t kGuard = 4096;

EventHook g_event_hook = nullptr;
std::uint32_t g_max_memory = 0x100000u;
constexpr std::uint8_t kGuardByte = 0xA5;

std::uint64_t Fnv(std::uint64_t hash, const void* data, const std::size_t size)
{
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < size; ++i)
    {
        hash = (hash ^ bytes[i]) * 0x100000001B3ull;
    }
    return hash;
}

template <typename T>
std::uint64_t FnvValue(const std::uint64_t hash, const T value)
{
    return Fnv(hash, &value, sizeof value);
}

// The guest buffer between two guard zones.
class Arena
{
public:
    explicit Arena(const std::uint32_t size) : bytes_(size + 2 * kGuard, 0), size_(size)
    {
        std::fill(bytes_.begin(), bytes_.begin() + kGuard, kGuardByte);
        std::fill(bytes_.end() - kGuard, bytes_.end(), kGuardByte);
        Poison(true);
    }

    ~Arena()
    {
        Poison(false);
    }

    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;

    std::uint8_t* guest()
    {
        return bytes_.data() + kGuard;
    }

    std::uint32_t size() const
    {
        return size_;
    }

    bool GuardsIntact()
    {
        Poison(false);
        bool intact = true;
        for (std::uint32_t i = 0; i < kGuard; ++i)
        {
            intact = intact && bytes_[i] == kGuardByte && bytes_[kGuard + size_ + i] == kGuardByte;
        }
        Poison(true);
        return intact;
    }

    void TamperGuard()
    {
        Poison(false);
        bytes_[kGuard + size_] ^= 0xFF;
        Poison(true);
    }

private:
    void Poison(const bool poison)
    {
#if defined(REX86_ROBUST_ASAN)
        if (poison)
        {
            ASAN_POISON_MEMORY_REGION(bytes_.data(), kGuard);
            ASAN_POISON_MEMORY_REGION(bytes_.data() + kGuard + size_, kGuard);
        }
        else
        {
            ASAN_UNPOISON_MEMORY_REGION(bytes_.data(), kGuard);
            ASAN_UNPOISON_MEMORY_REGION(bytes_.data() + kGuard + size_, kGuard);
        }
#else
        static_cast<void>(poison);
#endif
    }

    std::vector<std::uint8_t> bytes_;
    std::uint32_t size_ = 0;
};

// A host that answers from the case's random stream.
class RandomEnvironment final : public Environment
{
public:
    RandomEnvironment(Random& random, const std::uint32_t size) : random_(random), size_(size)
    {
    }

    bool LoadDescriptor(std::uint16_t, Descriptor* descriptor) override
    {
        if (random_.Chance(15))
        {
            return false;
        }
        *descriptor = Descriptor{};
        if (random_.Chance(30))
        {
            descriptor->base = random_.Chance(50) ? random_.Below(size_)
                                                  : static_cast<std::uint32_t>(random_.Bits(32));
            descriptor->limit = random_.Chance(50) ? random_.Below(size_)
                                                   : static_cast<std::uint32_t>(random_.Bits(32));
            descriptor->present = random_.Chance(90);
            descriptor->executable = random_.Chance(50);
            descriptor->writable = random_.Chance(70);
            descriptor->default_32bit = random_.Chance(80);
        }
        return true;
    }

    bool PortRead(std::uint16_t, std::uint8_t, std::uint32_t* value) override
    {
        if (!random_.Chance(50))
        {
            return false;
        }
        *value = static_cast<std::uint32_t>(random_.Bits(32));
        return true;
    }

    bool PortWrite(std::uint16_t, std::uint8_t, std::uint32_t) override
    {
        return random_.Chance(50);
    }

    bool InterruptTarget(std::uint8_t, std::uint16_t* cs, std::uint32_t* eip) override
    {
        if (!random_.Chance(50))
        {
            return false;
        }
        *cs = static_cast<std::uint16_t>(random_.Bits(16));
        *eip = random_.Below(size_);
        return true;
    }

    std::uint64_t ReadTimeStampCounter() override
    {
        return random_.Bits(64);
    }

    void Cpuid(std::uint32_t, std::uint32_t, std::uint32_t registers[4]) override
    {
        for (int i = 0; i < 4; ++i)
        {
            registers[i] = static_cast<std::uint32_t>(random_.Bits(32));
        }
    }

    void OnCodePageWritten(std::uint32_t) override
    {
        ++code_page_writes;
    }

    std::uint64_t code_page_writes = 0;

private:
    Random& random_;
    std::uint32_t size_ = 0;
};

std::uint32_t PickSize(Random& random)
{
    std::uint32_t size = 0;
    const std::uint32_t pick = random.Below(100);
    if (pick < 60)
    {
        size = kGuestPageSize * (1 + random.Below(16));
    }
    else if (pick < 90)
    {
        size = 0x10000u * (1 + random.Below(4));
    }
    else
    {
        size = 0x40000u * (1 + random.Below(4));
    }
    size = std::min(size, g_max_memory);
    if (random.Chance(20))
    {
        size -= random.Below(kGuestPageSize - 16);  // not a page multiple
    }
    return size;
}

// A benign case (design #31, decision 2) keeps nearly every page
// read-write-execute so execution runs long; a hostile one spreads them.
PageFlag PickPage(Random& random, const bool benign)
{
    const std::uint32_t pick = benign ? 33 + random.Below(67) : random.Below(100);
    PageFlag flags = kPageReadWriteExecute;
    if (pick < 5) flags = PageFlag::kNone;
    else if (pick < 8) flags = PageFlag::kMapped;
    else if (pick < 14) flags = PageFlag::kMapped | PageFlag::kRead;
    else if (pick < 24) flags = kPageReadExecute;
    else if (pick < 30) flags = kPageReadWrite;
    else if (pick < 33) flags = PageFlag::kMapped | PageFlag::kWrite;
    if (Has(flags, PageFlag::kMapped) && random.Chance(10))
    {
        flags = flags | PageFlag::kTranslated;
    }
    return flags;
}

// Bytes rich in prefixes and 0F opcodes, with half of the ModRM bytes in
// register form and small displacement bytes, so more instructions decode
// and their memory operands land inside the guest buffer.
void Soup(Random& random, std::uint8_t* out, const std::uint32_t size, const bool benign)
{
    static constexpr std::uint8_t kPrefixes[] = {0x66, 0x67, 0xF2, 0xF3, 0xF0, 0x26,
                                                 0x2E, 0x36, 0x3E, 0x64, 0x65};
    std::uint32_t at = 0;
    while (at < size)
    {
        while (at < size && random.Chance(15))
        {
            out[at++] = kPrefixes[random.Below(sizeof kPrefixes)];
        }
        if (at < size && random.Chance(30))
        {
            out[at++] = 0x0F;
        }
        if (at < size)
        {
            out[at++] = static_cast<std::uint8_t>(random.Bits(8));  // opcode
        }
        if (at < size)
        {
            const auto modrm = static_cast<std::uint8_t>(random.Bits(8));
            out[at++] = random.Chance(50) ? static_cast<std::uint8_t>(0xC0 | modrm) : modrm;
        }
        const std::uint32_t tail = random.Below(6);
        for (std::uint32_t i = 0; i < tail && at < size; ++i)
        {
            out[at++] = static_cast<std::uint8_t>(benign && random.Chance(70) ? 0
                                                  : random.Chance(50) ? random.Below(16)
                                                                      : random.Bits(8));
        }
    }
}

void RandomizeState(Random& random, CpuState* s, const std::uint32_t size, const bool benign)
{
    s->Reset();
    for (std::uint32_t& reg : s->gpr)
    {
        reg = benign             ? random.Below(size / 2)
            : random.Chance(70) ? random.Below(size + 64)
                                : static_cast<std::uint32_t>(random.Bits(32));
    }
    if (benign)
    {
        s->eip = random.Below(size);
        s->eflags = static_cast<std::uint32_t>(0x202u | (random.Bits(12) & 0x4D5u));
        return;
    }
    const std::uint32_t pick = random.Below(100);
    s->eip = pick < 85 ? random.Below(size)
           : pick < 95 ? size - random.Below(16)
                       : static_cast<std::uint32_t>(random.Bits(32));
    s->eflags = random.Chance(50)
        ? static_cast<std::uint32_t>(random.Bits(32))
        : static_cast<std::uint32_t>(0x2u | (random.Bits(12) & 0xCD5u) | (random.Chance(50) ? 0x200u : 0u));
    for (SegmentRegister& segment : s->segments)
    {
        if (!random.Chance(15))
        {
            continue;
        }
        segment.selector = static_cast<std::uint16_t>(random.Bits(16));
        segment.base = random.Chance(50) ? random.Below(size) : static_cast<std::uint32_t>(random.Bits(32));
        segment.limit = random.Chance(50) ? random.Below(size) : static_cast<std::uint32_t>(random.Bits(32));
        segment.present = random.Chance(90);
        segment.executable = random.Chance(50);
        segment.writable = random.Chance(70);
        segment.default_32bit = random.Chance(80);
    }
    X87State& x87 = s->x87;
    for (auto& reg : x87.registers)
    {
        for (std::uint8_t& b : reg) b = static_cast<std::uint8_t>(random.Bits(8));
    }
    if (random.Chance(70))
    {
        x87.control_word = static_cast<std::uint16_t>(random.Bits(16));
        x87.status_word = static_cast<std::uint16_t>(random.Bits(16));
        x87.tag_word = static_cast<std::uint16_t>(random.Bits(16));
    }
    x87.last_opcode = static_cast<std::uint16_t>(random.Bits(16));
    x87.last_instruction_pointer = static_cast<std::uint32_t>(random.Bits(32));
    x87.last_operand_pointer = static_cast<std::uint32_t>(random.Bits(32));
    for (auto& reg : s->sse.xmm)
    {
        for (std::uint8_t& b : reg) b = static_cast<std::uint8_t>(random.Bits(8));
    }
    s->sse.mxcsr = random.Chance(70) ? static_cast<std::uint32_t>(random.Bits(16) & 0xFFBFu)
                                     : static_cast<std::uint32_t>(random.Bits(32));
}

Features RandomFeatures(Random& random)
{
    Features f;
    f.x87 = random.Chance(85);
    f.cmov = random.Chance(80);
    f.mmx = random.Chance(80);
    f.fxsr = random.Chance(80);
    f.sse = random.Chance(75);
    f.sse2 = random.Chance(10);
    f.segments_16bit = random.Chance(30);
    return f;
}

unsigned Bit(const bool value, const unsigned shift)
{
    return (value ? 1u : 0u) << shift;
}

std::uint64_t DigestState(std::uint64_t h, const CpuState& s)
{
    for (const std::uint32_t reg : s.gpr) h = FnvValue(h, reg);
    h = FnvValue(h, s.eip);
    h = FnvValue(h, s.eflags);
    for (const SegmentRegister& seg : s.segments)
    {
        h = FnvValue(h, seg.selector);
        h = FnvValue(h, seg.base);
        h = FnvValue(h, seg.limit);
        h = FnvValue(h, static_cast<std::uint8_t>(Bit(seg.present, 0) | Bit(seg.executable, 1) |
                                                   Bit(seg.writable, 2) | Bit(seg.default_32bit, 3)));
    }
    for (const auto& reg : s.x87.registers) h = Fnv(h, reg.data(), reg.size());
    h = FnvValue(h, s.x87.control_word);
    h = FnvValue(h, s.x87.status_word);
    h = FnvValue(h, s.x87.tag_word);
    h = FnvValue(h, s.x87.last_opcode);
    h = FnvValue(h, s.x87.last_instruction_pointer);
    h = FnvValue(h, s.x87.last_instruction_selector);
    h = FnvValue(h, s.x87.last_operand_pointer);
    h = FnvValue(h, s.x87.last_operand_selector);
    for (const auto& reg : s.sse.xmm) h = Fnv(h, reg.data(), reg.size());
    return FnvValue(h, s.sse.mxcsr);
}

std::uint64_t DigestEvent(std::uint64_t h, const Event& e)
{
    h = FnvValue(h, static_cast<std::uint8_t>(e.reason));
    h = FnvValue(h, e.gate_address);
    h = FnvValue(h, e.vector);
    h = FnvValue(h, static_cast<std::uint8_t>(e.fault_kind));
    h = FnvValue(h, e.fault_address);
    h = FnvValue(h, static_cast<std::uint8_t>(Bit(e.fault_on_write, 0) | Bit(e.fault_on_fetch, 1) |
                                               Bit(e.port_is_write, 2)));
    h = FnvValue(h, e.port);
    h = FnvValue(h, e.port_width);
    h = FnvValue(h, e.port_value);
    return FnvValue(h, e.steps);
}

// I5: the event keeps the Run contract.
std::string CheckEvent(const Event& e, const std::uint64_t budget, const bool stop_requested,
                       const Cpu& cpu)
{
    const auto reason = static_cast<unsigned>(e.reason);
    if (reason > static_cast<unsigned>(StopReason::kStopRequested))
    {
        return "I5: stop reason out of range";
    }
    if (e.reason == StopReason::kNoEngine)
    {
        return "I5: kNoEngine from a core with an interpreter";
    }
    if (e.steps > budget)
    {
        return "I5: ran more steps than the budget";
    }
    if (e.reason == StopReason::kBudgetExhausted && e.steps != budget)
    {
        return "I5: kBudgetExhausted before the budget ran out";
    }
    if (e.reason == StopReason::kStopRequested && !stop_requested)
    {
        return "I5: kStopRequested without a request";
    }
    if (e.reason == StopReason::kFault &&
        (e.fault_kind == FaultKind::kNone ||
         static_cast<unsigned>(e.fault_kind) > static_cast<unsigned>(FaultKind::kOther)))
    {
        return "I5: a fault without a valid fault kind";
    }
    if (e.reason == StopReason::kGate && !cpu.IsGate(e.gate_address))
    {
        return "I5: kGate at an unregistered address";
    }
    return {};
}

// The memory checks: I2 (guards), I3 (protected pages), I4 (attributes).
class MemoryChecker
{
public:
    MemoryChecker(Arena& arena, const GuestMemory& memory) : arena_(arena)
    {
        const PageAttributeTable& pages = memory.pages();
        initial_.resize(pages.page_count());
        for (std::uint32_t p = 0; p < pages.page_count(); ++p)
        {
            initial_[p] = pages.Get(p << kGuestPageShift);
        }
        protected_ = std::vector<std::uint8_t>(arena.guest(), arena.guest() + arena.size());
    }

    std::string Check(const GuestMemory& memory)
    {
        if (!arena_.GuardsIntact())
        {
            return "I2: a guard zone outside GuestMemory changed";
        }
        const PageAttributeTable& pages = memory.pages();
        for (std::uint32_t p = 0; p < pages.page_count(); ++p)
        {
            const auto now = static_cast<std::uint8_t>(pages.Get(p << kGuestPageShift));
            const auto was = static_cast<std::uint8_t>(initial_[p]);
            const auto translated = static_cast<std::uint8_t>(PageFlag::kTranslated);
            // Only kTranslated may change: cleared by a store or the host,
            // set by the interpreter's decode cache on pages it executed
            // (design #34, decision 5), which must be executable.
            if ((now & ~translated) != (was & ~translated))
            {
                return "I4: a page attribute changed";
            }
            if ((now & translated & ~was) != 0 && !Has(initial_[p], kPageReadExecute))
            {
                return "I4: kTranslated set on a page that is not executable";
            }
            if (Has(initial_[p], PageFlag::kMapped | PageFlag::kWrite))
            {
                continue;
            }
            const std::uint32_t begin = p << kGuestPageShift;
            const std::uint32_t end = std::min(arena_.size(), begin + kGuestPageSize);
            if (std::memcmp(arena_.guest() + begin, protected_.data() + begin, end - begin) != 0)
            {
                return "I3: a page without write permission changed";
            }
        }
        return {};
    }

private:
    Arena& arena_;
    std::vector<PageFlag> initial_;
    std::vector<std::uint8_t> protected_;
};

CaseResult Execute(Random& random, const int sabotage, const std::uint8_t* code,
                   const std::size_t code_size)
{
    CaseResult result;
    const std::uint32_t size = PickSize(random);
    const bool benign = random.Chance(50);
    Arena arena(size);
    GuestMemory memory(arena.guest(), size);
    for (std::uint32_t page = 0; page < memory.pages().page_count(); ++page)
    {
        memory.pages().Set(page << kGuestPageShift, 1, PickPage(random, benign));
    }
    if (sabotage == 1)
    {
        memory.pages().Set(0, 1, PageFlag::kMapped | PageFlag::kRead);
    }
    if (random.Chance(50))
    {
        for (std::uint32_t i = 0; i < size; i += 8)
        {
            const std::uint64_t value = random.Bits(64);
            for (std::uint32_t j = 0; j < 8 && i + j < size; ++j)
            {
                arena.guest()[i + j] = static_cast<std::uint8_t>(value >> (8 * j));
            }
        }
    }
    else
    {
        Soup(random, arena.guest(), size, benign);
    }

    RandomEnvironment environment(random, size);
    Cpu cpu(&memory, &environment, RandomFeatures(random));
    RandomizeState(random, &cpu.state(), size, benign);
    if (code != nullptr)
    {
        const std::uint32_t at = cpu.state().eip < size ? cpu.state().eip : 0;
        const std::size_t count = std::min<std::size_t>(code_size, size - at);
        std::memcpy(arena.guest() + at, code, count);
    }
    MemoryChecker checker(arena, memory);

    std::uint64_t h = 0xCBF29CE484222325ull;
    const std::uint32_t runs = 1 + random.Below(24);
    for (std::uint32_t run = 0; run < runs; ++run)
    {
        CpuState& s = cpu.state();
        if (random.Chance(20)) cpu.RaiseInterrupt(static_cast<std::uint8_t>(random.Bits(8)));
        if (random.Chance(5)) cpu.ClearPendingInterrupt(static_cast<std::uint8_t>(random.Bits(8)));
        if (random.Chance(10)) cpu.RegisterGate(random.Below(size));
        if (random.Chance(5)) cpu.InvalidateCode(random.Below(size), random.Below(0x3000));
        if (random.Chance(10)) s.gpr[random.Below(8)] = static_cast<std::uint32_t>(random.Bits(32));
        const bool stop_requested = random.Chance(3);
        if (stop_requested) cpu.RequestStop();
        const std::uint64_t budget =
            random.Chance(5) ? 0 : 1 + random.Below(random.Chance(20) ? 20000 : 2000);

        Event event = cpu.Run(budget);
        if (g_event_hook != nullptr)
        {
            g_event_hook(budget, &event, &cpu.state());
        }
        if (sabotage == 3)
        {
            event.reason = StopReason::kBudgetExhausted;
            event.steps = budget + 1;
        }
        if (sabotage == 1 && run == 0)
        {
            arena.guest()[0] ^= 0xFF;
        }
        if (sabotage == 2 && run == 0)
        {
            arena.TamperGuard();
        }
        ++result.stats.runs;
        result.stats.retired += event.steps;
        result.stats.reasons[static_cast<std::size_t>(event.reason) & 7u]++;
        if (event.reason == StopReason::kFault)
        {
            result.stats.faults[static_cast<std::size_t>(event.fault_kind) & 15u]++;
        }
        std::string failure = CheckEvent(event, budget, stop_requested, cpu);
        if (failure.empty()) failure = checker.Check(memory);
        if (!failure.empty())
        {
            result.ok = false;
            result.failure = failure;
            return result;
        }
        h = DigestEvent(h, event);

        // The host's answer to the stop.
        bool end = false;
        switch (event.reason)
        {
            case StopReason::kFault:
                // Mostly step over the faulting byte and go on, as a host
                // emulating the fault handler would; sometimes jump away
                // or give up.
                // A fetch fault stepped over byte by byte would stay on the
                // same page; move away instead.
                if (random.Chance(5)) end = true;
                else if (event.fault_on_fetch || event.fault_address == s.Seg(Segment::kCs).base + s.eip ||
                         random.Chance(30))
                    s.eip = random.Below(size);
                else s.eip += 1;
                break;
            case StopReason::kGate:
                cpu.UnregisterGate(event.gate_address);
                break;
            case StopReason::kHalted:
                if (random.Chance(50)) s.eflags |= kEflagsInterrupt;
                break;
            case StopReason::kSoftwareInterrupt:
                if (random.Chance(50)) cpu.ClearPendingInterrupt(event.vector);
                break;
            case StopReason::kPortIo:
                if (random.Chance(50)) s.eip += 1;
                break;
            default:
                break;
        }
        if (end) break;
    }
    h = DigestState(h, cpu.state());
    h = Fnv(h, arena.guest(), size);
    for (std::uint32_t p = 0; p < memory.pages().page_count(); ++p)
    {
        h = FnvValue(h, static_cast<std::uint8_t>(memory.pages().Get(p << kGuestPageShift)));
    }
    result.digest = FnvValue(h, environment.code_page_writes);
    return result;
}

}  // namespace

Random::Random(const std::uint64_t seed) : generator_(seed)
{
}

Random::Random(const std::uint8_t* data, const std::size_t size)
    : data_(data), size_(size), generator_(Fnv(0xCBF29CE484222325ull, data, size))
{
}

std::uint64_t Random::Bits(const unsigned count)
{
    std::uint64_t value = 0;
    const unsigned bytes = count >= 64 ? 8u : (count + 7u) / 8u;
    if (used_ + bytes <= size_)
    {
        for (unsigned i = 0; i < bytes; ++i)
        {
            value |= static_cast<std::uint64_t>(data_[used_ + i]) << (8 * i);
        }
        used_ += bytes;
    }
    else
    {
        used_ = size_;
        value = generator_();
    }
    return count >= 64 ? value : value & ((std::uint64_t{1} << count) - 1u);
}

std::uint32_t Random::Below(const std::uint32_t bound)
{
    return bound == 0 ? 0u : static_cast<std::uint32_t>(Bits(32) % bound);
}

bool Random::Chance(const unsigned percent)
{
    return Below(100) < percent;
}

void SetMaxMemory(const std::uint32_t bytes)
{
    g_max_memory = std::max<std::uint32_t>(bytes, kGuestPageSize);
}

void SetEventHook(const EventHook hook)
{
    g_event_hook = hook;
}

CaseResult RunCase(Random& random, const std::uint8_t* code, const std::size_t code_size)
{
    return Execute(random, 0, code, code_size);
}

CaseResult RunSabotagedCase(Random& random, const int sabotage)
{
    return Execute(random, sabotage, nullptr, 0);
}

bool FuzzDecoder(Random& random, std::string* failure)
{
    static const decode::Decoder decoder32(decode::Decoder::Mode::kLegacy32);
    static const decode::Decoder decoder16(decode::Decoder::Mode::kLegacy16);
    std::uint8_t bytes[16] = {};
    const std::uint32_t length = random.Below(17);
    if (random.Chance(50))
    {
        Soup(random, bytes, length, false);
    }
    else
    {
        for (std::uint32_t i = 0; i < length; ++i) bytes[i] = static_cast<std::uint8_t>(random.Bits(8));
    }
    for (const decode::Decoder* decoder : {&decoder32, &decoder16})
    {
        decode::DecodedInstruction d;
        bool truncated = false;
        const std::uint32_t address = static_cast<std::uint32_t>(random.Bits(32));
        if (!decoder->Decode(bytes, length, address, &d, &truncated))
        {
            continue;
        }
        if (d.Length() < 1 || d.Length() > 15 || d.Length() > length)
        {
            *failure = "decoder: a decoded length outside 1..15 or the input";
            return false;
        }
        std::uint32_t target = 0;
        static_cast<void>(d.MnemonicName());
        static_cast<void>(d.IsaSetName());
        static_cast<void>(d.IsX87());
        static_cast<void>(d.OperandSignature());
        static_cast<void>(d.HasFloat80MemoryOperand());
        static_cast<void>(d.Flow());
        static_cast<void>(d.DirectTarget(&target));
    }
    return true;
}

}  // namespace rex86::robust
