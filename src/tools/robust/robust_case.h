// The robustness harness (design #31): one case is an arbitrary guest
// memory with guard zones, arbitrary CPU state and features, a randomly
// answering host and a few Run calls with host interventions between
// them, checked against invariants that must hold for any input. Every
// choice comes from one random source, a seed or libFuzzer's bytes, so a
// case reproduces exactly.

#ifndef REX86_TOOLS_ROBUST_ROBUST_CASE_H_
#define REX86_TOOLS_ROBUST_ROBUST_CASE_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <random>
#include <string>

namespace rex86::robust
{

// The random source: libFuzzer's bytes while they last, then a generator
// seeded from them; or a seed alone.
class Random
{
public:
    explicit Random(std::uint64_t seed);
    Random(const std::uint8_t* data, std::size_t size);

    std::uint64_t Bits(unsigned count);
    std::uint32_t Below(std::uint32_t bound);
    bool Chance(unsigned percent);

private:
    const std::uint8_t* data_ = nullptr;
    std::size_t size_ = 0;
    std::size_t used_ = 0;
    std::mt19937_64 generator_;
};

// What one case did, for the driver's summary.
struct CaseStats
{
    std::uint64_t runs = 0;
    std::uint64_t retired = 0;
    // By StopReason and by FaultKind (indices are the enumerators).
    std::array<std::uint64_t, 8> reasons = {};
    std::array<std::uint64_t, 16> faults = {};
};

struct CaseResult
{
    bool ok = true;
    // The first violated invariant (design #31, decision 1).
    std::string failure;
    // A digest of the events, the final state, memory and page attributes,
    // for the determinism check (I6).
    std::uint64_t digest = 0;
    CaseStats stats;
};

// The largest guest memory a case may pick (default 1 MiB). libFuzzer keeps
// it small so more cases run per second.
void SetMaxMemory(std::uint32_t bytes);

// Called after every Run with the event and the state, for --case.
using EventHook = void (*)(std::uint64_t budget, const void* event, const void* state);
void SetEventHook(EventHook hook);

// Generates and runs one case. code, when given, is copied into guest
// memory at the initial EIP, so libFuzzer's mutations act on instructions.
CaseResult RunCase(Random& random, const std::uint8_t* code = nullptr, std::size_t code_size = 0);

// Runs a case twice from identically built random sources and adds the
// determinism check. make_random builds a fresh source each call.
template <typename MakeRandom>
CaseResult RunCaseTwice(const MakeRandom& make_random, const std::uint8_t* code = nullptr,
                        const std::size_t code_size = 0)
{
    Random first_random = make_random();
    CaseResult first = RunCase(first_random, code, code_size);
    if (!first.ok)
    {
        return first;
    }
    Random second_random = make_random();
    const CaseResult second = RunCase(second_random, code, code_size);
    if (!second.ok)
    {
        return second;
    }
    if (first.digest != second.digest)
    {
        first.ok = false;
        first.failure = "I6: the same case ran differently twice";
    }
    return first;
}

// Decodes 0-16 random bytes in both modes; false with a message when a
// successful decode breaks the decoder's own contract.
bool FuzzDecoder(Random& random, std::string* failure);

// Test hooks for the invariant checkers (unit tests only): run a case
// whose guest deliberately breaks one invariant, to show the checker sees
// it. 1 = a store into a write-protected page, 2 = a write into the guard
// zone, 3 = a forged event over budget.
CaseResult RunSabotagedCase(Random& random, int sabotage);

}  // namespace rex86::robust

#endif  // REX86_TOOLS_ROBUST_ROBUST_CASE_H_
