#include "tools/bench/runner.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace rex86::bench
{

// The workloads load no selectors, use no ports and take no interrupts;
// anything that reaches the host is a failure the frame loop reports.
class Machine::BenchEnvironment final : public Environment
{
public:
    bool LoadDescriptor(std::uint16_t, Descriptor* descriptor) override
    {
        *descriptor = Descriptor{};
        return true;
    }

    bool PortRead(std::uint16_t, std::uint8_t, std::uint32_t*) override
    {
        return false;
    }

    bool PortWrite(std::uint16_t, std::uint8_t, std::uint32_t) override
    {
        return false;
    }

    bool InterruptTarget(std::uint8_t, std::uint16_t*, std::uint32_t*) override
    {
        return false;
    }

    std::uint64_t ReadTimeStampCounter() override
    {
        return 0;
    }

    void Cpuid(std::uint32_t, std::uint32_t, std::uint32_t registers[4]) override
    {
        registers[0] = registers[1] = registers[2] = registers[3] = 0;
    }
};

namespace
{

using Clock = std::chrono::steady_clock;

double MillisecondsBetween(const Clock::time_point from, const Clock::time_point to)
{
    return std::chrono::duration<double, std::milli>(to - from).count();
}

const char* StopReasonName(const StopReason reason)
{
    switch (reason)
    {
        case StopReason::kNoEngine:
            return "no_engine";
        case StopReason::kBudgetExhausted:
            return "budget_exhausted";
        case StopReason::kGate:
            return "gate";
        case StopReason::kSoftwareInterrupt:
            return "software_interrupt";
        case StopReason::kPortIo:
            return "port_io";
        case StopReason::kFault:
            return "fault";
        case StopReason::kHalted:
            return "halted";
        case StopReason::kStopRequested:
            return "stop_requested";
    }
    return "unknown";
}

std::string Format(const char* format, const unsigned long long a,
                   const unsigned long long b)
{
    char text[160];
    std::snprintf(text, sizeof text, format, a, b);
    return text;
}

}  // namespace

double WorkloadResult::Mips() const
{
    return seconds > 0.0 ? static_cast<double>(retired) / seconds / 1e6 : 0.0;
}

double Percentile(const std::vector<double>& values, const double p)
{
    if (values.empty())
    {
        return 0.0;
    }
    std::vector<double> sorted = values;
    std::sort(sorted.begin(), sorted.end());
    const double scaled = std::ceil(p * static_cast<double>(sorted.size()));
    std::size_t index = scaled <= 1.0 ? 0 : static_cast<std::size_t>(scaled) - 1;
    index = std::min(index, sorted.size() - 1);
    return sorted[index];
}

std::uint64_t FrameInstructions(const Board& board, const double ipc)
{
    return static_cast<std::uint64_t>(
        std::llround(board.clock_mhz * 1e6 * ipc / kFrameHz));
}

Machine::Machine(const Image& image, const Workload& workload)
    : workload_(workload),
      buffer_(kMemoryBytes, 0),
      memory_(buffer_.data(), kMemoryBytes),
      environment_(std::make_unique<BenchEnvironment>())
{
    loaded_ = LoadImage(image, &memory_);
    cpu_ = std::make_unique<Cpu>(&memory_, environment_.get(), Features{});
    cpu_->RegisterGate(workload_.gate);
    ResetLap();
}

Machine::~Machine() = default;

void Machine::ResetLap()
{
    CpuState& state = cpu_->state();
    state.Reset();
    state.eip = workload_.entry;
    state.Set(Gpr::kEsp, kStackTop);
    lap_so_far_ = 0;
}

bool Machine::RunFrame(const std::uint64_t budget, std::uint64_t* laps,
                       std::uint64_t* lap_instructions, std::string* failure)
{
    std::uint64_t remaining = budget;
    while (remaining > 0)
    {
        const Event event = cpu_->Run(remaining);
        remaining -= event.instructions_retired;
        lap_so_far_ += event.instructions_retired;
        if (event.reason == StopReason::kBudgetExhausted)
        {
            break;
        }
        if (event.reason != StopReason::kGate)
        {
            *failure = std::string("stopped with ") + StopReasonName(event.reason);
            if (event.reason == StopReason::kFault)
            {
                *failure += Format(" kind=%llu address=%08llX",
                                   static_cast<unsigned long long>(event.fault_kind),
                                   event.fault_address);
            }
            *failure += Format(" eip=%08llX retired=%llu", cpu_->state().eip,
                               lap_so_far_);
            lap_so_far_ = 0;
            return false;
        }
        const std::uint32_t edi = cpu_->state().Get(Gpr::kEdi);
        if (edi != workload_.expected_edi)
        {
            *failure = Format("edi expected=%08llX core=%08llX",
                              workload_.expected_edi, edi);
            lap_so_far_ = 0;
            return false;
        }
        if (*laps == 0)
        {
            *lap_instructions = lap_so_far_;
        }
        else if (lap_so_far_ != *lap_instructions)
        {
            *failure = Format("lap retired %llu, the first lap %llu", lap_so_far_,
                              *lap_instructions);
            lap_so_far_ = 0;
            return false;
        }
        ++*laps;
        ResetLap();
    }
    return true;
}

WorkloadResult RunWorkload(const Image& image, const Workload& workload,
                           const Options& options)
{
    WorkloadResult result;
    result.name = workload.name;
    const Clock::time_point start = Clock::now();
    Machine machine(image, workload);
    if (!machine.loaded())
    {
        result.failure = "the image did not load";
        return result;
    }
    for (unsigned frame = 0; frame < options.frames; ++frame)
    {
        const Clock::time_point frame_start = Clock::now();
        if (!machine.RunFrame(options.frame_budget, &result.laps,
                              &result.lap_instructions, &result.failure))
        {
            return result;
        }
        const Clock::time_point frame_end = Clock::now();
        const double ms = MillisecondsBetween(frame_start, frame_end);
        result.frame_ms.push_back(ms);
        result.seconds += ms / 1000.0;
        result.retired += options.frame_budget;
        if (frame == 0)
        {
            result.first_frame_ms = MillisecondsBetween(start, frame_end);
        }
    }
    result.verified = result.laps > 0;
    if (!result.verified)
    {
        result.failure = "no lap completed within the frames; raise --budget or --frames";
    }
    return result;
}

WorkloadResult RunSmoke(const Image& image, const Workload& workload)
{
    WorkloadResult result;
    result.name = workload.name;
    Machine machine(image, workload);
    if (!machine.loaded())
    {
        result.failure = "the image did not load";
        return result;
    }
    // At least two laps, so that the second checks the retired count is
    // deterministic. A frame of a million instructions holds several laps
    // of every workload; the loop covers a longer one.
    while (result.laps < 2)
    {
        if (!machine.RunFrame(1u << 20, &result.laps, &result.lap_instructions,
                              &result.failure))
        {
            return result;
        }
    }
    result.retired = result.laps * result.lap_instructions;
    result.verified = true;
    return result;
}

}  // namespace rex86::bench
