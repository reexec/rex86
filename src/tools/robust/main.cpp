// rex86_robust: the robustness harness's driver (design #31). Runs a range
// of seeds, each one case twice (the determinism check) plus a decoder
// round, and reports every violated invariant with the seed that
// reproduces it. A watchdog (#59) ends the run when one case takes longer
// than a limit, since a core that stops advancing would otherwise leave a
// harness that never ends; it names the case's seed.
//
// Usage: rex86_robust [--cases N] [--seed S] [--case S] [--verbose]
//                     [--case-seconds N]
//   --cases N          cases to run (default 1000), seeds S .. S+N-1
//   --case S           one case, verbose
//   --case-seconds N   the watchdog's limit per case (default 60, 0 off);
//                      absent on wasm32 without threads

#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

// Emscripten without pthreads has no second thread for the watchdog.
#if !defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__)
#define REX86_ROBUST_WATCHDOG 1
#include <condition_variable>
#include <mutex>
#include <thread>
#endif

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

#if defined(REX86_ROBUST_WATCHDOG)

// Watches the case in progress from a second thread. Each Enter starts a new
// case and its clock; a case still in progress past the limit is reported
// as a violation with its seed and the process exits at once, the stuck
// thread being beyond reach.
class Watchdog
{
public:
    explicit Watchdog(const unsigned seconds) : limit_(seconds)
    {
        if (seconds != 0)
        {
            thread_ = std::thread([this] { Watch(); });
        }
    }

    ~Watchdog()
    {
        if (thread_.joinable())
        {
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                done_ = true;
            }
            wake_.notify_one();
            thread_.join();
        }
    }

    Watchdog(const Watchdog&) = delete;
    Watchdog& operator=(const Watchdog&) = delete;

    void Enter(const std::uint64_t seed)
    {
        if (!thread_.joinable())
        {
            return;
        }
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            seed_ = seed;
            ++generation_;
        }
        wake_.notify_one();
    }

private:
    void Watch()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        while (!done_)
        {
            // Generation 0 is before the first case: wait without a limit.
            const std::uint64_t generation = generation_;
            const auto moved = [&] { return done_ || generation_ != generation; };
            if (generation == 0)
            {
                wake_.wait(lock, moved);
                continue;
            }
            if (wake_.wait_for(lock, limit_, moved))
            {
                continue;
            }
            std::printf("VIOLATION seed=%" PRIu64 ": watchdog: the case ran longer than %u s\n", seed_,
                        static_cast<unsigned>(limit_.count()));
            std::printf("[rex86-robust] result=fail\n");
            std::fflush(stdout);
            std::_Exit(1);
        }
    }

    const std::chrono::seconds limit_;
    std::mutex mutex_;
    std::condition_variable wake_;
    bool done_ = false;
    std::uint64_t seed_ = 0;
    std::uint64_t generation_ = 0;
    std::thread thread_;
};

#else

class Watchdog
{
public:
    explicit Watchdog(unsigned) {}
    void Enter(std::uint64_t) {}
};

#endif

}  // namespace

int main(int argc, char** argv)
{
    std::uint64_t cases = 1000;
    std::uint64_t seed = 1;
    bool verbose = false;
    unsigned case_seconds = 60;
    // A pause inside the first case, for the watchdog's own test.
    unsigned stall_ms = 0;
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
        else if (std::strcmp(argv[i], "--case-seconds") == 0 && has_value)
        {
            case_seconds = static_cast<unsigned>(std::strtoul(argv[++i], nullptr, 0));
        }
        else if (std::strcmp(argv[i], "--test-stall-ms") == 0 && has_value)
        {
            stall_ms = static_cast<unsigned>(std::strtoul(argv[++i], nullptr, 0));
        }
        else
        {
            std::fprintf(stderr, "usage: rex86_robust [--cases N] [--seed S] [--case S] [--verbose]"
                                 " [--case-seconds N]\n");
            return 2;
        }
    }

    rex86::robust::CaseStats total;
    std::uint64_t violations = 0;
    std::uint64_t decoder_cases = 0;
    Watchdog watchdog(case_seconds);
    for (std::uint64_t n = 0; n < cases; ++n)
    {
        const std::uint64_t case_seed = seed + n;
        watchdog.Enter(case_seed);
#if defined(REX86_ROBUST_WATCHDOG)
        if (n == 0 && stall_ms != 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(stall_ms));
        }
#else
        static_cast<void>(stall_ms);
#endif
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
