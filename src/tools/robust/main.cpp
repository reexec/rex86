// rex86_robust: the robustness harness's driver (design #31). Runs a range
// of seeds, each one case twice (the determinism check) plus a decoder
// round, and reports every violated invariant with the seed that
// reproduces it.
//
// Usage: rex86_robust [--cases N] [--seed S] [--case S] [--verbose]
//   --cases N   cases to run (default 1000), seeds S .. S+N-1
//   --case S    one case, verbose

#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "rex86/cpu_state.h"
#include "rex86/environment.h"
#include "tools/robust/robust_case.h"

namespace
{

const char* const kReasons[8] = {"no_engine", "budget", "gate", "software_interrupt",
                                 "port_io", "fault", "halted", "stop_requested"};

void PrintEvent(const std::uint64_t budget, const void* event, const void* state)
{
    const auto& e = *static_cast<const rex86::Event*>(event);
    const auto& s = *static_cast<const rex86::CpuState*>(state);
    std::printf("  run budget=%" PRIu64 " -> %s retired=%" PRIu64 " fault=%u address=%08X eip=%08X\n",
                budget, kReasons[static_cast<unsigned>(e.reason) & 7u], e.steps,
                static_cast<unsigned>(e.fault_kind), e.fault_address, s.eip);
}

}  // namespace

int main(int argc, char** argv)
{
    std::uint64_t cases = 1000;
    std::uint64_t seed = 1;
    bool verbose = false;
    for (int i = 1; i < argc; ++i)
    {
        const bool has_value = i + 1 < argc;
        if (std::strcmp(argv[i], "--cases") == 0 && has_value)
        {
            cases = std::strtoull(argv[++i], nullptr, 0);
        }
        else if (std::strcmp(argv[i], "--seed") == 0 && has_value)
        {
            seed = std::strtoull(argv[++i], nullptr, 0);
        }
        else if (std::strcmp(argv[i], "--case") == 0 && has_value)
        {
            seed = std::strtoull(argv[++i], nullptr, 0);
            cases = 1;
            verbose = true;
            rex86::robust::SetEventHook(PrintEvent);
        }
        else if (std::strcmp(argv[i], "--verbose") == 0)
        {
            verbose = true;
        }
        else
        {
            std::fprintf(stderr, "usage: rex86_robust [--cases N] [--seed S] [--case S] [--verbose]\n");
            return 2;
        }
    }

    rex86::robust::CaseStats total;
    std::uint64_t violations = 0;
    std::uint64_t decoder_cases = 0;
    for (std::uint64_t n = 0; n < cases; ++n)
    {
        const std::uint64_t case_seed = seed + n;
        const rex86::robust::CaseResult result = rex86::robust::RunCaseTwice(
            [case_seed] { return rex86::robust::Random(case_seed); });
        rex86::robust::Random decoder_random(case_seed ^ 0x9E3779B97F4A7C15ull);
        std::string decoder_failure;
        bool decoder_ok = true;
        for (int round = 0; round < 32 && decoder_ok; ++round)
        {
            decoder_ok = rex86::robust::FuzzDecoder(decoder_random, &decoder_failure);
            ++decoder_cases;
        }
        if (!result.ok || !decoder_ok)
        {
            ++violations;
            std::printf("VIOLATION seed=%" PRIu64 ": %s\n", case_seed,
                        result.ok ? decoder_failure.c_str() : result.failure.c_str());
            continue;
        }
        total.runs += result.stats.runs;
        total.retired += result.stats.retired;
        for (std::size_t i = 0; i < total.reasons.size(); ++i) total.reasons[i] += result.stats.reasons[i];
        for (std::size_t i = 0; i < total.faults.size(); ++i) total.faults[i] += result.stats.faults[i];
        if (verbose)
        {
            std::printf("case seed=%" PRIu64 " runs=%" PRIu64 " retired=%" PRIu64 " digest=%016" PRIX64 "\n",
                        case_seed, result.stats.runs, result.stats.retired, result.digest);
        }
    }

    std::printf("[rex86-robust] cases=%" PRIu64 " seed=%" PRIu64 " runs=%" PRIu64 " retired=%" PRIu64
                " decoder_cases=%" PRIu64 " violations=%" PRIu64 "\n",
                cases, seed, total.runs, total.retired, decoder_cases, violations);
    std::printf("[rex86-robust] stops:");
    for (std::size_t i = 0; i < total.reasons.size(); ++i)
    {
        if (total.reasons[i] != 0) std::printf(" %s=%" PRIu64, kReasons[i], total.reasons[i]);
    }
    std::printf("\n[rex86-robust] faults:");
    for (std::size_t i = 0; i < total.faults.size(); ++i)
    {
        if (total.faults[i] != 0) std::printf(" %zu=%" PRIu64, i, total.faults[i]);
    }
    std::printf("\n[rex86-robust] result=%s\n", violations == 0 ? "ok" : "fail");
    return violations == 0 ? 0 : 1;
}
