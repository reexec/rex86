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
    DeferringServices deferring;
    options.wasm = &deferring;
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

#endif

}  // namespace

void RunWasmBackendTests(rex86::test::Context& context)
{
    ModuleFormatTests(context);
#if defined(REX86_HOST_WEB)
    WasmRunTests(context);
#endif
}
