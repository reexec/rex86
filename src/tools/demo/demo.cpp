// rex86_demo: the web demo's engine (design #62, decision 1). The page's
// Worker calls these C functions: pick a workload of the benchmark and an
// engine, run it a frame at a time, and read the counts after each frame.
// The machine is the benchmark's own (bench::Machine), so a frame here is a
// frame there and the figures reproduce rex86_bench's (README goal 5).
//
// Built by Emscripten alone, as a module factory (createRex86Demo) that
// stays alive between calls.

#include <emscripten/emscripten.h>

#include <cstdint>
#include <cstdio>
#include <iterator>
#include <memory>
#include <string>

#include "host/web/wasm_module_services.h"
#include "rex86/cpu.h"
#include "rex86/version.h"
#include "tools/bench/runner.h"
#include "tools/bench/workloads.h"

namespace
{

using rex86::bench::Image;
using rex86::bench::Machine;

// What the page reads after a frame, by index (the order of kStatNames).
enum Stat : int
{
    kRetired,
    kLaps,
    kLapInstructions,
    kFrameMs,
    kBlocksTranslated,
    kBlockRuns,
    kInterpreterExits,
    kTranslatedSteps,
    kModulesSubmitted,
    kModulesEvicted,
    kBlocksInvalidated,
    kEngineBytes,
    kWasmModulesInstalled,
    kWasmInstallMs,
    kWasmInstalledBytes,
    kWasmModulesRefused,
    kStatCount,
};

constexpr const char* kStatNames[kStatCount] = {
    "retired",           "laps",               "lap_instructions",   "frame_ms",
    "blocks_translated", "block_runs",         "interpreter_exits",  "translated_steps",
    "modules_submitted", "modules_evicted",    "blocks_invalidated", "engine_bytes",
    "wasm_modules",      "wasm_install_ms",    "wasm_bytes",         "wasm_refused",
};

constexpr const char* kEngineNames[] = {"interpreter", "wasm", "evaluator"};

struct Demo
{
    Image image;
    std::string image_error;
    bool image_built = false;
    // The installer outlives every Machine that uses it.
    rex86::host::web::WebWasmModuleServices wasm_services;
    rex86::TranslationOptions translation;
    std::unique_ptr<Machine> machine;
    double stats[kStatCount] = {};
    std::string failure;
    std::string info;
};

Demo& TheDemo()
{
    static Demo demo;
    if (!demo.image_built)
    {
        demo.image_built = true;
        if (!rex86::bench::BuildImage(&demo.image, &demo.image_error))
        {
            demo.image.workloads.clear();
        }
    }
    return demo;
}

void AppendQuoted(std::string* out, const char* text)
{
    out->push_back('"');
    for (const char* c = text; *c != '\0'; ++c)
    {
        if (*c == '"' || *c == '\\')
        {
            out->push_back('\\');
        }
        out->push_back(*c);
    }
    out->push_back('"');
}

}  // namespace

extern "C" {

// A JSON object: the version, the workloads, the engines, the boards and
// the names of the counts rex86_demo_stat returns.
EMSCRIPTEN_KEEPALIVE const char* rex86_demo_info()
{
    Demo& demo = TheDemo();
    std::string& out = demo.info;
    out = "{\"version\":";
    AppendQuoted(&out, rex86::VersionString());
    out += ",\"image_error\":";
    AppendQuoted(&out, demo.image_error.c_str());
    out += ",\"workloads\":[";
    for (std::size_t i = 0; i < demo.image.workloads.size(); ++i)
    {
        out += i == 0 ? "" : ",";
        AppendQuoted(&out, demo.image.workloads[i].name.c_str());
    }
    out += "],\"engines\":[";
    for (std::size_t i = 0; i < std::size(kEngineNames); ++i)
    {
        out += i == 0 ? "" : ",";
        AppendQuoted(&out, kEngineNames[i]);
    }
    out += "],\"boards\":[";
    bool first = true;
    for (const rex86::bench::Board& board : rex86::bench::kBoards)
    {
        char clock[32];
        std::snprintf(clock, sizeof clock, "%.0f", board.clock_mhz);
        out += first ? "{\"name\":" : ",{\"name\":";
        first = false;
        AppendQuoted(&out, board.name);
        out += ",\"cpu\":";
        AppendQuoted(&out, board.cpu);
        out += ",\"clock_mhz\":";
        out += clock;
        out += "}";
    }
    out += "],\"stats\":[";
    for (int i = 0; i < kStatCount; ++i)
    {
        out += i == 0 ? "" : ",";
        AppendQuoted(&out, kStatNames[i]);
    }
    out += "]}";
    return out.c_str();
}

// Loads workload (an index into info's list) on engine (0 interpreter,
// 1 wasm, 2 evaluator) with translation's defaults. 1 when the machine
// loaded and the engine asked for is the one running, 0 otherwise.
EMSCRIPTEN_KEEPALIVE int rex86_demo_start(const int workload, const int engine)
{
    Demo& demo = TheDemo();
    demo.machine.reset();
    demo.failure.clear();
    for (double& stat : demo.stats)
    {
        stat = 0.0;
    }
    if (workload < 0 || static_cast<std::size_t>(workload) >= demo.image.workloads.size() ||
        engine < 0 || engine >= static_cast<int>(std::size(kEngineNames)))
    {
        demo.failure = "no such workload or engine";
        return 0;
    }
    demo.translation = rex86::TranslationOptions{};
    if (engine == 1)
    {
        demo.translation.mode = rex86::TranslationMode::kWasm;
        demo.translation.wasm = &demo.wasm_services;
    }
    else if (engine == 2)
    {
        demo.translation.mode = rex86::TranslationMode::kEvaluator;
    }
    demo.machine = std::make_unique<Machine>(demo.image, demo.image.workloads[static_cast<std::size_t>(workload)],
                                             &demo.translation);
    if (!demo.machine->loaded())
    {
        demo.failure = "the workload did not load";
        demo.machine.reset();
        return 0;
    }
    const rex86::Engine wanted = engine == 0 ? rex86::Engine::kInterpreter : rex86::Engine::kTranslator;
    if (demo.machine->cpu().ActiveEngine() != wanted)
    {
        demo.failure = "the engine is not available in this build";
        demo.machine.reset();
        return 0;
    }
    return 1;
}

// Runs one frame of budget steps. 1 when every lap that ended matched the
// reference model, 0 with rex86_demo_failure's message otherwise.
EMSCRIPTEN_KEEPALIVE int rex86_demo_frame(const double budget)
{
    Demo& demo = TheDemo();
    if (!demo.machine)
    {
        demo.failure = demo.failure.empty() ? "not started" : demo.failure;
        return 0;
    }
    std::uint64_t laps = static_cast<std::uint64_t>(demo.stats[kLaps]);
    std::uint64_t lap_instructions = static_cast<std::uint64_t>(demo.stats[kLapInstructions]);
    const auto steps = static_cast<std::uint64_t>(budget);
    const double start = emscripten_get_now();
    const bool ok = demo.machine->RunFrame(steps, &laps, &lap_instructions, &demo.failure);
    const double ms = emscripten_get_now() - start;
    const rex86::TranslationStats translation = demo.machine->cpu().translation_stats();
    demo.stats[kRetired] += static_cast<double>(steps);
    demo.stats[kLaps] = static_cast<double>(laps);
    demo.stats[kLapInstructions] = static_cast<double>(lap_instructions);
    demo.stats[kFrameMs] = ms;
    demo.stats[kBlocksTranslated] = static_cast<double>(translation.blocks_translated);
    demo.stats[kBlockRuns] = static_cast<double>(translation.block_runs);
    demo.stats[kInterpreterExits] = static_cast<double>(translation.interpreter_exits);
    demo.stats[kTranslatedSteps] = static_cast<double>(translation.translated_steps);
    demo.stats[kModulesSubmitted] = static_cast<double>(translation.modules_submitted);
    demo.stats[kModulesEvicted] = static_cast<double>(translation.modules_evicted);
    demo.stats[kBlocksInvalidated] = static_cast<double>(translation.blocks_invalidated);
    demo.stats[kEngineBytes] = static_cast<double>(demo.machine->cpu().EngineMemoryBytes());
    demo.stats[kWasmModulesInstalled] = static_cast<double>(demo.wasm_services.installed());
    demo.stats[kWasmInstallMs] = demo.wasm_services.install_ms();
    demo.stats[kWasmInstalledBytes] = static_cast<double>(demo.wasm_services.installed_bytes());
    demo.stats[kWasmModulesRefused] = static_cast<double>(demo.wasm_services.failed());
    if (!ok)
    {
        demo.machine.reset();
    }
    return ok ? 1 : 0;
}

// One count after the last frame (index into info's stats); the wasm
// installer's counts are for the whole process.
EMSCRIPTEN_KEEPALIVE double rex86_demo_stat(const int index)
{
    return index >= 0 && index < kStatCount ? TheDemo().stats[index] : 0.0;
}

EMSCRIPTEN_KEEPALIVE const char* rex86_demo_failure()
{
    return TheDemo().failure.c_str();
}

EMSCRIPTEN_KEEPALIVE void rex86_demo_stop()
{
    TheDemo().machine.reset();
}

}  // extern "C"
