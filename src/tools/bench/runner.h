// The frame loop and its statistics (design #27, decisions 3, 4 and 6). A
// frame is one Run(budget), as a consumer's host loop does it; a lap that
// ends mid-frame is verified against the model, the state is reset and the
// remaining budget runs on, so every frame retires exactly the budget.

#ifndef REX86_TOOLS_BENCH_RUNNER_H_
#define REX86_TOOLS_BENCH_RUNNER_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "rex86/cpu.h"
#include "tools/bench/workloads.h"

namespace rex86::bench
{

struct Options
{
    std::uint64_t frame_budget = 1000000;
    unsigned frames = 20;
    // Design #27, decision 4: the reference CPUs' sustained IPC is inferred,
    // not measured.
    double ipc = 1.0;
    // The engine: null keeps the Cpu's default (the interpreter, or forced
    // translation in a REX86_FORCE_TRANSLATION build).
    const TranslationOptions* translation = nullptr;
};

struct WorkloadResult
{
    std::string name;
    bool verified = false;
    std::string failure;
    std::uint64_t laps = 0;
    std::uint64_t lap_instructions = 0;
    std::uint64_t retired = 0;
    // The sum of the frame times.
    double seconds = 0.0;
    std::vector<double> frame_ms;
    // From Machine construction (workload load included) to the end of the
    // first frame.
    double first_frame_ms = 0.0;

    [[nodiscard]] double Mips() const;
    // The engines' memory and the translation counts at the end of the run
    // (goal 7's instrumentation; design #44).
    std::size_t engine_bytes = 0;
    TranslationStats translation;
};

// A frame-time percentile of the result: p in [0, 1], taken at index
// ceil(p * n) - 1 of the sorted times. Zero when there is no frame.
double Percentile(const std::vector<double>& values, double p);

// The target boards' reference CPUs (#21 design, decision 4).
struct Board
{
    const char* name;
    const char* cpu;
    double clock_mhz;
};

inline constexpr Board kBoards[] = {
    {"ez2dj1", "AMD K6-2", 400.0},
    {"mk3", "Mendocino Celeron", 400.0},
    {"mk5", "Tualatin Celeron", 1300.0},
    {"ez2dj2", "Tualatin Celeron", 1400.0},
};

inline constexpr double kFrameHz = 60.0;

// clock * ipc / 60: one frame's worth of the reference CPU's instructions.
std::uint64_t FrameInstructions(const Board& board, double ipc);

// A guest machine holding one workload: the memory, the environment and
// the Cpu, with the gate registered.
class Machine
{
public:
    // translation, when given, selects the engine (design #44); a native
    // backend will take a BenchCodeCache here as well (design #27,
    // decision 6).
    Machine(const Image& image, const Workload& workload,
            const TranslationOptions* translation = nullptr);
    ~Machine();

    Machine(const Machine&) = delete;
    Machine& operator=(const Machine&) = delete;

    [[nodiscard]] bool loaded() const
    {
        return loaded_;
    }

    [[nodiscard]] Cpu& cpu()
    {
        return *cpu_;
    }

    [[nodiscard]] GuestMemory& memory()
    {
        return memory_;
    }

    // Registers, EIP, ESP, EFLAGS and the x87 back to the lap's start.
    void ResetLap();

    // Runs the workload for one frame of `budget` instructions. False
    // with a message in *failure when a lap ends with the wrong result,
    // a fault or an unexpected stop. *laps and *lap_instructions are
    // updated as laps complete.
    bool RunFrame(std::uint64_t budget, std::uint64_t* laps,
                  std::uint64_t* lap_instructions, std::string* failure);

private:
    class BenchEnvironment;

    const Workload& workload_;
    std::vector<std::uint8_t> buffer_;
    GuestMemory memory_;
    std::unique_ptr<BenchEnvironment> environment_;
    std::unique_ptr<Cpu> cpu_;
    bool loaded_ = false;
    // What the current lap has retired so far, across frames.
    std::uint64_t lap_so_far_ = 0;
};

// Runs the workload for options.frames frames and gathers the result.
WorkloadResult RunWorkload(const Image& image, const Workload& workload,
                           const Options& options);

// One verified lap, no timing: the ctest smoke.
WorkloadResult RunSmoke(const Image& image, const Workload& workload);

}  // namespace rex86::bench

#endif  // REX86_TOOLS_BENCH_RUNNER_H_
