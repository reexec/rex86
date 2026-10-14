#include <algorithm>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

#include "rex86/cpu.h"
#include "test_support.h"
#include "translate/ir/frontend.h"
#include "translate/ir/optimize.h"
#include "translate/wasm/backend.h"
#include "translate/wasm/codegen.h"

#if defined(REX86_HOST_WEB)
#include "host/web/wasm_module_services.h"
#endif

namespace
{

using rex86::Gpr;
namespace ir = rex86::translate::ir;
namespace wasm = rex86::translate::wasm;

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

constexpr std::uint32_t kCode = 0x1000;

struct Rig
{
    std::vector<std::uint8_t> buffer = std::vector<std::uint8_t>(0x10000, 0xF4);  // hlt
    rex86::GuestMemory memory{buffer.data(), 0x10000};
    QuietEnvironment environment;
    rex86::Cpu cpu{&memory, &environment, rex86::Features{}};

    explicit Rig(std::initializer_list<std::uint8_t> code)
    {
        memory.pages().Set(0, 0x10000, rex86::kPageReadWriteExecute);
        std::uint32_t at = kCode;
        for (const std::uint8_t b : code)
        {
            buffer[at++] = b;
        }
        cpu.state().eip = kCode;
        cpu.state().Set(Gpr::kEsp, 0x9000);
        cpu.SetTranslation(rex86::TranslationOptions{});
    }
};

bool Contains(const wasm::Bytes& bytes, const std::string& text)
{
    return std::search(bytes.begin(), bytes.end(), text.begin(), text.end()) != bytes.end();
}

// The module of a small block has the shape the host's installer expects.
void ModuleFormatTests(rex86::test::Context& context)
{
    Rig rig({0x83, 0xC0, 0x01, 0x89, 0x18, 0xF4});  // add eax, 1; mov [eax], ebx; hlt
    ir::Block block;
    REX86_CHECK(context, ir::FormBlock(rig.cpu.state(), rig.memory, rex86::Features{}, {}, &block));
    ir::Optimize(&block);
    const ir::Block* blocks[] = {&block, &block};
    const wasm::Bytes module = wasm::CompileModule(blocks, 2, 7);
    REX86_CHECK(context, module.size() > 8);
    REX86_CHECK(context, std::memcmp(module.data(), "\0asm\x01\0\0\0", 8) == 0);
    // Sections in order: type, import, function, export, code.
    std::vector<std::uint8_t> ids;
    std::size_t at = 8;
    while (at < module.size())
    {
        ids.push_back(module[at++]);
        std::uint32_t size = 0;
        unsigned shift = 0;
        while (true)
        {
            const std::uint8_t b = module[at++];
            size |= static_cast<std::uint32_t>(b & 0x7F) << shift;
            shift += 7;
            if ((b & 0x80) == 0)
            {
                break;
            }
        }
        at += size;
    }
    REX86_CHECK(context, at == module.size());
    REX86_CHECK(context, (ids == std::vector<std::uint8_t>{1, 2, 3, 7, 10}));
    REX86_CHECK(context, Contains(module, "env") && Contains(module, "memory") && Contains(module, "table"));
    REX86_CHECK(context, Contains(module, "b0") && Contains(module, "b1"));
    REX86_CHECK_EQ(context, wasm::EncodeExit(ir::ExitKind::kInterpret, 5), std::uint32_t{11});
    const ir::ExitResult decoded = wasm::DecodeExit(11, 0x1234);
    REX86_CHECK(context, decoded.kind == ir::ExitKind::kInterpret && decoded.steps == 5 &&
                             decoded.eip == 0x1234);

    // Outside wasm32, or without services, kWasm is refused and stays off.
    rex86::TranslationOptions options;
    options.mode = rex86::TranslationMode::kWasm;
    REX86_CHECK(context, !rig.cpu.SetTranslation(options));
    REX86_CHECK(context, rig.cpu.translation().mode == rex86::TranslationMode::kOff);
    REX86_CHECK(context, rig.cpu.ActiveEngine() == rex86::Engine::kInterpreter);
}

#if defined(REX86_HOST_WEB)

// mov ecx, 1000; L: add eax, ecx; mov [ecx*4 + 0x8000], eax; dec ecx; jnz L; hlt
const std::initializer_list<std::uint8_t> kLoop = {0xB9, 0xE8, 0x03, 0x00, 0x00, 0x01, 0xC8, 0x89,
                                                   0x04, 0x8D, 0x00, 0x80, 0x00, 0x00, 0x49, 0x75,
                                                   0xF4, 0xF4};

bool SameMachine(const Rig& a, const Rig& b)
{
    return a.cpu.state().gpr == b.cpu.state().gpr && a.cpu.state().eip == b.cpu.state().eip &&
           a.cpu.state().eflags == b.cpu.state().eflags && a.buffer == b.buffer;
}

// Defers every installation until the test answers it.
class DeferringServices final : public rex86::WasmModuleServices
{
public:
    rex86::WasmInstall Install(std::uint32_t ticket, const std::uint8_t* bytes, std::size_t size,
                               std::uint32_t export_count, std::uint32_t*) override
    {
        waiting.push_back({ticket, bytes, size, export_count});
        return rex86::WasmInstall::kPending;
    }
    void Release(const std::uint32_t* indices, std::uint32_t count) override
    {
        real.Release(indices, count);
    }

    struct Request
    {
        std::uint32_t ticket;
        const std::uint8_t* bytes;
        std::size_t size;
        std::uint32_t export_count;
    };
    std::vector<Request> waiting;
    rex86::host::web::WebWasmModuleServices real;
};

void WasmRunTests(rex86::test::Context& context)
{
    rex86::host::web::WebWasmModuleServices services;
    rex86::TranslationOptions options;
    options.mode = rex86::TranslationMode::kWasm;
    options.threshold = 0;
    options.wasm = &services;
    options.blocks_per_module = 1;  // batches have their own test

    Rig interpreted(kLoop);
    Rig translated(kLoop);
    REX86_CHECK(context, translated.cpu.SetTranslation(options));
    const rex86::Event a = interpreted.cpu.Run(1000000);
    const rex86::Event b = translated.cpu.Run(1000000);
    REX86_CHECK(context, a.reason == rex86::StopReason::kHalted && b.reason == a.reason);
    REX86_CHECK_EQ(context, b.steps, a.steps);
    REX86_CHECK(context, SameMachine(interpreted, translated));
    REX86_CHECK(context, translated.cpu.translation_stats().translated_steps > 3000);
    REX86_CHECK(context, services.installed() >= 1);

    // An installer that answers later: the interpreter runs until then.
    // One block per module, so that each request is one block.
    DeferringServices deferring;
    options.wasm = &deferring;
    options.blocks_per_module = 1;
    Rig deferred(kLoop);
    REX86_CHECK(context, deferred.cpu.SetTranslation(options));
    deferred.cpu.Run(200);
    REX86_CHECK(context, !deferring.waiting.empty());
    REX86_CHECK_EQ(context, deferred.cpu.translation_stats().block_runs, std::uint64_t{0});
    for (const DeferringServices::Request& request : deferring.waiting)
    {
        std::uint32_t index = 0;
        const rex86::WasmInstall answer =
            deferring.real.Install(request.ticket, request.bytes, request.size, request.export_count, &index);
        if (answer == rex86::WasmInstall::kInstalled)
        {
            deferred.cpu.CompleteWasmModule(request.ticket, &index, 1);
        }
        else
        {
            deferred.cpu.FailWasmModule(request.ticket);
        }
    }
    deferring.waiting.clear();
    deferred.cpu.Run(1000000);
    REX86_CHECK(context, deferred.cpu.translation_stats().block_runs > 0);
    REX86_CHECK(context, SameMachine(interpreted, deferred));

    // A failed installation leaves the block to the interpreter.
    Rig failed(kLoop);
    REX86_CHECK(context, failed.cpu.SetTranslation(options));
    failed.cpu.Run(200);
    for (const DeferringServices::Request& request : deferring.waiting)
    {
        failed.cpu.FailWasmModule(request.ticket);
    }
    deferring.waiting.clear();
    failed.cpu.Run(1000000);
    REX86_CHECK(context, SameMachine(interpreted, failed));
}

// mov ecx, 30; L: 40 blocks of (add eax, imm8; jmp short +1; int3); dec ecx;
// jnz L (rel32); hlt
std::vector<std::uint8_t> ManyBlocks()
{
    std::vector<std::uint8_t> code = {0xB9, 0x1E, 0x00, 0x00, 0x00};
    for (std::uint8_t i = 0; i < 40; ++i)
    {
        code.insert(code.end(), {0x83, 0xC0, static_cast<std::uint8_t>(i * 3 + 1), 0xEB, 0x01, 0xCC});
    }
    code.push_back(0x49);
    const auto after = static_cast<std::int32_t>(code.size() + 6);
    const std::uint32_t rel = static_cast<std::uint32_t>(5 - after);
    code.insert(code.end(), {0x0F, 0x85, static_cast<std::uint8_t>(rel), static_cast<std::uint8_t>(rel >> 8),
                             static_cast<std::uint8_t>(rel >> 16), static_cast<std::uint8_t>(rel >> 24)});
    code.push_back(0xF4);
    return code;
}

void Load(Rig* rig, const std::vector<std::uint8_t>& code)
{
    std::copy(code.begin(), code.end(), rig->buffer.begin() + kCode);
}

// Batched modules (design #49): several blocks per module, a module budget
// small enough to evict, and asynchronous batches answered later. Results
// match the interpreter throughout.
void WasmBatchTests(rex86::test::Context& context)
{
    const std::vector<std::uint8_t> code = ManyBlocks();
    Rig interpreted(std::initializer_list<std::uint8_t>{});
    Load(&interpreted, code);
    const rex86::Event a = interpreted.cpu.Run(1000000);
    REX86_CHECK(context, a.reason == rex86::StopReason::kHalted);

    rex86::host::web::WebWasmModuleServices services;
    rex86::TranslationOptions options;
    options.mode = rex86::TranslationMode::kWasm;
    options.threshold = 2;
    options.wasm = &services;
    options.blocks_per_module = 4;
    options.batch_wait = 64;
    options.max_modules = 3;
    Rig translated(std::initializer_list<std::uint8_t>{});
    Load(&translated, code);
    REX86_CHECK(context, translated.cpu.SetTranslation(options));
    const rex86::Event b = translated.cpu.Run(1000000);
    REX86_CHECK(context, b.reason == a.reason);
    REX86_CHECK_EQ(context, b.steps, a.steps);
    REX86_CHECK(context, SameMachine(interpreted, translated));
    const rex86::TranslationStats stats = translated.cpu.translation_stats();
    REX86_CHECK(context, stats.translated_steps > 0);
    REX86_CHECK(context, stats.modules_submitted >= 10);
    REX86_CHECK(context, stats.modules_evicted >= 1);
    REX86_CHECK(context, services.installed_functions() > services.installed());

    // Asynchronous batches: each request carries several blocks.
    DeferringServices deferring;
    options.wasm = &deferring;
    options.max_modules = 0;
    Rig deferred(std::initializer_list<std::uint8_t>{});
    Load(&deferred, code);
    REX86_CHECK(context, deferred.cpu.SetTranslation(options));
    std::uint32_t most_exports = 0;
    rex86::Event c;
    do
    {
        c = deferred.cpu.Run(500);
        for (const DeferringServices::Request& request : deferring.waiting)
        {
            most_exports = std::max(most_exports, request.export_count);
            std::vector<std::uint32_t> indices(request.export_count);
            if (deferring.real.Install(request.ticket, request.bytes, request.size, request.export_count,
                                       indices.data()) == rex86::WasmInstall::kInstalled)
            {
                deferred.cpu.CompleteWasmModule(request.ticket, indices.data(), request.export_count);
            }
            else
            {
                deferred.cpu.FailWasmModule(request.ticket);
            }
        }
        deferring.waiting.clear();
    } while (c.reason == rex86::StopReason::kBudgetExhausted);
    REX86_CHECK(context, c.reason == a.reason);
    REX86_CHECK(context, SameMachine(interpreted, deferred));
    REX86_CHECK_EQ(context, most_exports, std::uint32_t{4});
    REX86_CHECK(context, deferred.cpu.translation_stats().block_runs > 0);
}

// Generated memory checks (design #65, decision 3): inline where they can
// vouch for an access, the helper otherwise. Either way a translated run
// faults, exits or completes exactly where the interpreter does.
void WasmCheckTests(rex86::test::Context& context)
{
    struct Case
    {
        const char* name;
        std::uint32_t address;
        bool write;
        void (*setup)(Rig*);
    };
    const Case cases[] = {
        {"read", 0x8000, false, [](Rig*) {}},
        {"write", 0x8000, true, [](Rig*) {}},
        {"read-only page", 0x8000, true,
         [](Rig* rig) { rig->memory.pages().Set(0x8000, 0x1000, rex86::kPageReadExecute); }},
        {"unmapped next page", 0x8FFE, true,
         [](Rig* rig) { rig->memory.pages().Set(0x9000, 0x1000, rex86::PageFlag::kNone); }},
        {"unmapped read", 0x9000, false,
         [](Rig* rig) { rig->memory.pages().Set(0x9000, 0x1000, rex86::PageFlag::kNone); }},
        {"past the end", 0xFFFE, false, [](Rig*) {}},
        {"code page", kCode + 0x100, true, [](Rig*) {}},
        {"non-flat DS", 0x8000, true,
         [](Rig* rig) {
             rex86::SegmentRegister& ds = rig->cpu.state().Seg(rex86::Segment::kDs);
             ds.base = 0x100;
             ds.limit = 0xFFFF;
         }},
        {"read-only DS", 0x8000, true,
         [](Rig* rig) { rig->cpu.state().Seg(rex86::Segment::kDs).writable = false; }},
    };
    for (const Case& test : cases)
    {
        // mov eax, 0x11223344; then mov [address], eax or mov eax, [address];
        // then inc ecx and hlt, so the block goes on past the access.
        const std::uint8_t a0 = static_cast<std::uint8_t>(test.address);
        const std::uint8_t a1 = static_cast<std::uint8_t>(test.address >> 8);
        const std::vector<std::uint8_t> code = {0xB8, 0x44, 0x33, 0x22, 0x11,
                                                static_cast<std::uint8_t>(test.write ? 0x89 : 0x8B),
                                                0x05, a0, a1, 0x00, 0x00, 0x41, 0xF4};
        Rig interpreted(std::initializer_list<std::uint8_t>{});
        Rig translated(std::initializer_list<std::uint8_t>{});
        Load(&interpreted, code);
        Load(&translated, code);
        test.setup(&interpreted);
        test.setup(&translated);
        rex86::host::web::WebWasmModuleServices services;
        rex86::TranslationOptions options;
        options.mode = rex86::TranslationMode::kWasm;
        options.threshold = 0;
        options.wasm = &services;
        options.blocks_per_module = 1;
        REX86_CHECK(context, translated.cpu.SetTranslation(options));
        const rex86::Event a = interpreted.cpu.Run(100);
        const rex86::Event b = translated.cpu.Run(100);
        const bool same = a.reason == b.reason && a.steps == b.steps && a.fault_kind == b.fault_kind &&
                          a.fault_address == b.fault_address && SameMachine(interpreted, translated);
        if (!same)
        {
            context.Fail(std::string("inline check: ") + test.name, __FILE__, __LINE__);
        }
        REX86_CHECK(context, translated.cpu.translation_stats().block_runs > 0);
    }
}

#endif

}  // namespace

void RunWasmBackendTests(rex86::test::Context& context)
{
    ModuleFormatTests(context);
#if defined(REX86_HOST_WEB)
    WasmRunTests(context);
    WasmBatchTests(context);
    WasmCheckTests(context);
#endif
}
