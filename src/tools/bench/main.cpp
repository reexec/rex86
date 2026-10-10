// rex86_bench: the benchmark harness (design #27). Synthetic workloads run
// on the core frame by frame; the report is MIPS, the frame-time
// distribution, time to first frame and the real-time ratio against the
// target boards' reference CPUs, as [rex86-bench] key=value lines.
//
// Usage: rex86_bench [--frames N] [--budget N] [--ipc X] [--only a,b]
//                    [--engine interpreter|evaluator|wasm] [--smoke] [--dump <dir>]

#if defined(REX86_HOST_WEB)
#include "host/web/wasm_module_services.h"
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "rex86/cpu.h"
#include "rex86/version.h"
#include "tools/bench/runner.h"
#include "tools/bench/workloads.h"

namespace
{

using rex86::bench::Board;
using rex86::bench::Image;
using rex86::bench::Options;
using rex86::bench::Workload;
using rex86::bench::WorkloadResult;

const char* ArchName()
{
#if defined(__wasm32__)
    return "wasm32";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "aarch64";
#elif defined(__x86_64__) || defined(_M_X64)
    return "x86-64";
#elif defined(__i386__) || defined(_M_IX86)
    return "x86";
#else
    return "unknown";
#endif
}

const char* ConfigName()
{
#if defined(REX86_BENCH_CONFIG)
    return REX86_BENCH_CONFIG[0] == '\0' ? "unspecified" : REX86_BENCH_CONFIG;
#else
    return "unspecified";
#endif
}

const char* EngineName(const rex86::Engine engine)
{
    switch (engine)
    {
        case rex86::Engine::kNone:
            return "none";
        case rex86::Engine::kInterpreter:
            return "interpreter";
        case rex86::Engine::kTranslator:
            return "translator";
    }
    return "unknown";
}

void Usage()
{
    std::fprintf(stderr,
                 "usage: rex86_bench [--frames N] [--budget N] [--ipc X] [--only a,b]\n"
                 "                   [--engine interpreter|evaluator|wasm] [--smoke] [--dump <dir>]\n");
}

std::vector<std::string> SplitCommas(const std::string& text)
{
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= text.size())
    {
        const std::size_t comma = text.find(',', start);
        const std::size_t end = comma == std::string::npos ? text.size() : comma;
        if (end > start)
        {
            parts.push_back(text.substr(start, end - start));
        }
        if (comma == std::string::npos)
        {
            break;
        }
        start = comma + 1;
    }
    return parts;
}

bool DumpWorkloads(const Image& image, const std::string& directory)
{
    const std::string bin_path = directory + "/image.bin";
    std::FILE* bin = std::fopen(bin_path.c_str(), "wb");
    if (bin == nullptr)
    {
        std::fprintf(stderr, "cannot write %s\n", bin_path.c_str());
        return false;
    }
    const std::size_t written =
        std::fwrite(image.code.data(), 1, image.code.size(), bin);
    std::fclose(bin);
    if (written != image.code.size())
    {
        return false;
    }
    const std::string txt_path = directory + "/image.txt";
    std::FILE* txt = std::fopen(txt_path.c_str(), "w");
    if (txt == nullptr)
    {
        std::fprintf(stderr, "cannot write %s\n", txt_path.c_str());
        return false;
    }
    std::fprintf(txt, "# rex86_bench workload image (design #27)\n");
    std::fprintf(txt, "version=%s\n", rex86::VersionString());
    std::fprintf(txt, "image=image.bin load_address=0x%08X size=%zu\n", image.base,
                 image.code.size());
    std::fprintf(txt, "memory_bytes=0x%08X\n", rex86::bench::kMemoryBytes);
    std::fprintf(txt, "code=0x%08X+0x%08X rx\n", rex86::bench::kCodeBase,
                 rex86::bench::kCodeSize);
    std::fprintf(txt, "data=0x%08X+0x%08X rw zero-filled\n", rex86::bench::kDataBase,
                 rex86::bench::kDataSize);
    std::fprintf(txt, "stack=0x%08X+0x%08X rw initial_esp=0x%08X\n",
                 rex86::bench::kStackBase, rex86::bench::kStackSize,
                 rex86::bench::kStackTop);
    std::fprintf(txt, "x87_table=0x%08X count=%u value=i*0.5 binary64\n",
                 rex86::bench::kX87Table, rex86::bench::kX87TableCount);
    std::fprintf(txt, "x87_constants two=0x%08X one=0x%08X binary64\n",
                 rex86::bench::kX87Two, rex86::bench::kX87One);
    std::fprintf(txt, "initial_state: flat 32-bit segments, eflags=0x00000002, fninit\n");
    for (const Workload& workload : image.workloads)
    {
        std::fprintf(txt, "workload=%s entry=0x%08X gate=0x%08X expected_edi=0x%08X\n",
                     workload.name.c_str(), workload.entry, workload.gate,
                     workload.expected_edi);
    }
    std::fclose(txt);
    return true;
}

void PrintResult(const WorkloadResult& result, const Options& options)
{
    if (!result.verified)
    {
        std::printf("[rex86-bench] workload=%s verified=fail reason=\"%s\"\n",
                    result.name.c_str(), result.failure.c_str());
        return;
    }
    const double mips = result.Mips();
    std::printf("[rex86-bench] workload=%s frames=%zu frame_budget=%llu retired=%llu "
                "seconds=%.3f mips=%.3f frame_ms_p50=%.3f frame_ms_p99=%.3f "
                "frame_ms_max=%.3f first_frame_ms=%.3f laps=%llu lap_instructions=%llu "
                "verified=ok\n",
                result.name.c_str(), result.frame_ms.size(),
                static_cast<unsigned long long>(options.frame_budget),
                static_cast<unsigned long long>(result.retired), result.seconds, mips,
                rex86::bench::Percentile(result.frame_ms, 0.50),
                rex86::bench::Percentile(result.frame_ms, 0.99),
                rex86::bench::Percentile(result.frame_ms, 1.00),
                result.first_frame_ms,
                static_cast<unsigned long long>(result.laps),
                static_cast<unsigned long long>(result.lap_instructions));
    std::printf("[rex86-bench] workload=%s engine_bytes=%zu blocks_translated=%llu "
                "block_runs=%llu interpreter_exits=%llu translated_steps=%llu\n",
                result.name.c_str(), result.engine_bytes,
                static_cast<unsigned long long>(result.translation.blocks_translated),
                static_cast<unsigned long long>(result.translation.block_runs),
                static_cast<unsigned long long>(result.translation.interpreter_exits),
                static_cast<unsigned long long>(result.translation.translated_steps));
    for (const Board& board : rex86::bench::kBoards)
    {
        const std::uint64_t frame_instructions =
            rex86::bench::FrameInstructions(board, options.ipc);
        const double frame_ms =
            mips > 0.0 ? static_cast<double>(frame_instructions) / (mips * 1e6) * 1e3
                       : 0.0;
        const double ratio = mips / (board.clock_mhz * options.ipc);
        std::printf("[rex86-bench] workload=%s board=%s cpu=\"%s\" clock_mhz=%.0f "
                    "ipc=%.2f frame_instructions=%llu frame_ms_at_board=%.3f "
                    "realtime_ratio=%.5f\n",
                    result.name.c_str(), board.name, board.cpu, board.clock_mhz,
                    options.ipc, static_cast<unsigned long long>(frame_instructions),
                    frame_ms, ratio);
    }
}

}  // namespace

int main(int argc, char** argv)
{
    Options options;
    bool smoke = false;
    std::string dump_directory;
    std::string engine = "interpreter";
    std::vector<std::string> only;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        const bool has_value = i + 1 < argc;
        if (arg == "--smoke")
        {
            smoke = true;
        }
        else if (arg == "--frames" && has_value)
        {
            options.frames = static_cast<unsigned>(std::strtoul(argv[++i], nullptr, 0));
        }
        else if (arg == "--budget" && has_value)
        {
            options.frame_budget = std::strtoull(argv[++i], nullptr, 0);
        }
        else if (arg == "--ipc" && has_value)
        {
            options.ipc = std::strtod(argv[++i], nullptr);
        }
        else if (arg == "--only" && has_value)
        {
            only = SplitCommas(argv[++i]);
        }
        else if (arg == "--engine" && has_value)
        {
            engine = argv[++i];
        }
        else if (arg == "--dump" && has_value)
        {
            dump_directory = argv[++i];
        }
        else
        {
            Usage();
            return 2;
        }
    }
    if (options.frames == 0 || options.frame_budget == 0 || options.ipc <= 0.0)
    {
        Usage();
        return 2;
    }

    Image image;
    std::string error;
    if (!rex86::bench::BuildImage(&image, &error))
    {
        std::fprintf(stderr, "cannot build the workload image: %s\n", error.c_str());
        return 2;
    }

    std::printf("[rex86-bench] version=%s config=%s arch=%s pointer_bytes=%u "
                "image_bytes=%zu workloads=%zu\n",
                rex86::VersionString(), ConfigName(), ArchName(),
                static_cast<unsigned>(sizeof(void*)), image.code.size(),
                image.workloads.size());

    if (!dump_directory.empty())
    {
        if (!DumpWorkloads(image, dump_directory))
        {
            std::printf("[rex86-bench] result=fail\n");
            return 1;
        }
        std::printf("[rex86-bench] dump=%s files=image.bin,image.txt\n",
                    dump_directory.c_str());
    }

    // The engine the harness measures: the interpreter, or translation on
    // the IR evaluator (design #44). Any other request is refused
    // explicitly rather than measured as if it were there (design #27,
    // decision 6).
    rex86::TranslationOptions translation;
#if defined(REX86_HOST_WEB)
    rex86::host::web::WebWasmModuleServices wasm_services;
#endif
    if (engine == "evaluator")
    {
        translation.mode = rex86::TranslationMode::kEvaluator;
    }
#if defined(REX86_HOST_WEB)
    else if (engine == "wasm")
    {
        translation.mode = rex86::TranslationMode::kWasm;
        translation.wasm = &wasm_services;
    }
#endif
    else if (engine != "interpreter")
    {
        std::printf("[rex86-bench] engine=%s engine_available=false\n", engine.c_str());
        std::printf("[rex86-bench] result=fail\n");
        return 2;
    }
    options.translation = &translation;
    {
        rex86::bench::Machine probe(image, image.workloads.front(), &translation);
        const rex86::Engine active = probe.cpu().ActiveEngine();
        const rex86::Engine wanted = engine == "interpreter" ? rex86::Engine::kInterpreter
                                                              : rex86::Engine::kTranslator;
        std::printf("[rex86-bench] engine=%s engine_available=%s mode=%s frames=%u "
                    "frame_budget=%llu ipc=%.2f\n",
                    engine == "interpreter" ? EngineName(active) : engine.c_str(),
                    active == wanted ? "true" : "false",
                    smoke ? "smoke" : "measure", options.frames,
                    static_cast<unsigned long long>(options.frame_budget), options.ipc);
    }

    bool ok = true;
    unsigned selected = 0;
    for (const Workload& workload : image.workloads)
    {
        if (!only.empty())
        {
            bool wanted = false;
            for (const std::string& name : only)
            {
                wanted = wanted || name == workload.name;
            }
            if (!wanted)
            {
                continue;
            }
        }
        ++selected;
        if (smoke)
        {
            const WorkloadResult result = rex86::bench::RunSmoke(image, workload);
            if (result.verified)
            {
                std::printf("[rex86-bench] workload=%s smoke=ok lap_instructions=%llu "
                            "expected_edi=0x%08X\n",
                            result.name.c_str(),
                            static_cast<unsigned long long>(result.lap_instructions),
                            workload.expected_edi);
            }
            else
            {
                std::printf("[rex86-bench] workload=%s smoke=fail reason=\"%s\"\n",
                            result.name.c_str(), result.failure.c_str());
                ok = false;
            }
            continue;
        }
        const WorkloadResult result = rex86::bench::RunWorkload(image, workload, options);
        PrintResult(result, options);
        std::fflush(stdout);
        ok = ok && result.verified;
    }
    if (selected == 0)
    {
        std::fprintf(stderr, "no workload matched --only\n");
        ok = false;
    }
#if defined(REX86_HOST_WEB)
    if (engine == "wasm")
    {
        std::printf("[rex86-bench] wasm modules_installed=%llu install_ms=%.3f install_us_per_module=%.1f\n",
                    static_cast<unsigned long long>(wasm_services.installed()), wasm_services.install_ms(),
                    wasm_services.installed() == 0
                        ? 0.0
                        : 1000.0 * wasm_services.install_ms() / static_cast<double>(wasm_services.installed()));
    }
#endif

    // Goal 7's instrumentation: what the core and the harness hold. The
    // page table keeps a flag byte and a 4-byte generation per page; the
    // engines' memory (the decode cache today, design #34) is read from a
    // Machine that has run past the cache's warm-up. The translation
    // backends' code caches join it.
    std::size_t engine_bytes = 0;
    {
        rex86::bench::Machine machine(image, image.workloads.front());
        std::uint64_t laps = 0;
        std::uint64_t lap_instructions = 0;
        std::string ignored;
        machine.RunFrame(1000, &laps, &lap_instructions, &ignored);
        engine_bytes = machine.cpu().EngineMemoryBytes();
    }
    std::printf("[rex86-bench] memory guest_bytes=%u page_table_bytes=%u cpu_bytes=%u "
                "cpu_state_bytes=%u engine_bytes=%zu\n",
                rex86::bench::kMemoryBytes,
                static_cast<unsigned>((rex86::bench::kMemoryBytes / rex86::kGuestPageSize) *
                                      (sizeof(rex86::PageFlag) + sizeof(std::uint32_t))),
                static_cast<unsigned>(sizeof(rex86::Cpu)),
                static_cast<unsigned>(sizeof(rex86::CpuState)), engine_bytes);
    std::printf("[rex86-bench] result=%s\n", ok ? "ok" : "fail");
    return ok ? 0 : 1;
}
