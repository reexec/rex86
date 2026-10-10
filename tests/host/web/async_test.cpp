// The asynchronous installer's test (design #48, decision 2): two Cpus,
// each translating with its own AsyncWebWasmModuleServices, run a loop frame
// by frame on Emscripten's main loop, polling between frames, so
// installations finish while the guests run. The interpreter runs the blocks
// until then; each result must equal an interpreter-only run, and translated
// blocks must have run once the installations arrived. The two guests place
// their code at different addresses, so a result delivered to the wrong Cpu
// leaves the wrong EIP. Runs under Node (ctest) and on a browser's main
// thread (tests/host/web/suite.js).

#if !defined(__EMSCRIPTEN__)
#error "the asynchronous installer's test runs under Emscripten only"
#endif

#include <emscripten.h>

#include <cstdint>
#include <cstdio>
#include <vector>

#include "host/web/async_wasm_module_services.h"
#include "rex86/cpu.h"

namespace
{

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

// mov esi, 300; O: mov ecx, 1000; L: add eax, ecx; mov [ecx*4 + 0x8000],
// eax; dec ecx; jnz L; dec esi; jnz O; hlt
constexpr std::uint8_t kProgram[] = {0xBE, 0x2C, 0x01, 0x00, 0x00, 0xB9, 0xE8, 0x03, 0x00, 0x00,
                                     0x01, 0xC8, 0x89, 0x04, 0x8D, 0x00, 0x80, 0x00, 0x00, 0x49,
                                     0x75, 0xF4, 0x4E, 0x75, 0xEC, 0xF4};

constexpr int kGuests = 2;

struct Guest
{
    std::vector<std::uint8_t> buffer = std::vector<std::uint8_t>(0x10000, 0xF4);
    rex86::GuestMemory memory{buffer.data(), 0x10000};
    QuietEnvironment environment;
    rex86::Cpu cpu{&memory, &environment, rex86::Features{}};

    explicit Guest(const std::uint32_t code)
    {
        memory.pages().Set(0, 0x10000, rex86::kPageReadWriteExecute);
        for (std::size_t i = 0; i < sizeof kProgram; ++i)
        {
            buffer[code + i] = kProgram[i];
        }
        cpu.state().eip = code;
        cpu.state().Set(rex86::Gpr::kEsp, 0x9000);
        cpu.SetTranslation(rex86::TranslationOptions{});
    }
};

struct Pair
{
    Guest* reference = nullptr;
    Guest* guest = nullptr;
    rex86::host::web::AsyncWebWasmModuleServices* services = nullptr;
    bool halted = false;
    bool ran_while_pending = false;
};

Pair g_pairs[kGuests];
int g_frames = 0;

void Finish(const bool ok, const char* why)
{
    std::uint64_t installed = 0;
    std::uint64_t failed = 0;
    std::uint64_t block_runs = 0;
    std::uint64_t translated_steps = 0;
    bool ran_while_pending = true;
    for (const Pair& pair : g_pairs)
    {
        const rex86::TranslationStats stats = pair.guest->cpu.translation_stats();
        installed += pair.services->installed();
        failed += pair.services->failed();
        block_runs += stats.block_runs;
        translated_steps += stats.translated_steps;
        ran_while_pending = ran_while_pending && pair.ran_while_pending;
    }
    std::printf("[rex86-async] guests=%d frames=%d installed=%llu failed=%llu block_runs=%llu "
                "translated_steps=%llu ran_while_pending=%d result=%s%s%s\n",
                kGuests, g_frames, static_cast<unsigned long long>(installed),
                static_cast<unsigned long long>(failed), static_cast<unsigned long long>(block_runs),
                static_cast<unsigned long long>(translated_steps), ran_while_pending ? 1 : 0,
                ok ? "ok" : "fail", ok ? "" : " reason=", ok ? "" : why);
    emscripten_cancel_main_loop();
    emscripten_force_exit(ok ? 0 : 1);
}

// The first failure of a halted pair, or nullptr.
const char* Check(const Pair& pair)
{
    const rex86::CpuState& a = pair.reference->cpu.state();
    const rex86::CpuState& b = pair.guest->cpu.state();
    if (a.gpr != b.gpr || a.eip != b.eip || a.eflags != b.eflags ||
        pair.reference->buffer != pair.guest->buffer)
    {
        return "the state differs from the interpreter's";
    }
    if (pair.services->installed() == 0 || pair.guest->cpu.translation_stats().block_runs == 0)
    {
        return "no installed translation ran";
    }
    if (!pair.ran_while_pending)
    {
        return "no frame ran with an installation pending";
    }
    return nullptr;
}

void Frame()
{
    ++g_frames;
    bool all_halted = true;
    for (Pair& pair : g_pairs)
    {
        if (pair.halted)
        {
            continue;
        }
        if (pair.services->pending() != 0)
        {
            pair.ran_while_pending = true;
        }
        const rex86::Event event = pair.guest->cpu.Run(20000);
        pair.services->Poll(pair.guest->cpu);
        if (event.reason == rex86::StopReason::kHalted)
        {
            pair.halted = true;
        }
        else if (event.reason != rex86::StopReason::kBudgetExhausted)
        {
            Finish(false, "a guest stopped without halting");
            return;
        }
        all_halted = all_halted && pair.halted;
    }
    if (!all_halted)
    {
        if (g_frames >= 100000)
        {
            Finish(false, "a guest did not halt");
        }
        return;
    }
    for (const Pair& pair : g_pairs)
    {
        if (const char* why = Check(pair))
        {
            Finish(false, why);
            return;
        }
    }
    Finish(true, "");
}

}  // namespace

int main()
{
    for (int i = 0; i < kGuests; ++i)
    {
        Pair& pair = g_pairs[i];
        const std::uint32_t code = 0x1000 + 0x1000 * static_cast<std::uint32_t>(i);
        pair.reference = new Guest(code);
        pair.reference->cpu.Run(100000000);
        pair.guest = new Guest(code);
        pair.services = new rex86::host::web::AsyncWebWasmModuleServices();
        rex86::TranslationOptions options;
        options.mode = rex86::TranslationMode::kWasm;
        options.threshold = 4;
        options.wasm = pair.services;
        if (!pair.guest->cpu.SetTranslation(options))
        {
            std::printf("[rex86-async] result=fail reason=kWasm refused\n");
            return 1;
        }
    }
    emscripten_set_main_loop(Frame, 0, false);
    return 0;
}
