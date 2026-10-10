#if !defined(__EMSCRIPTEN__)
#error "src/host/web/ is the web host's adapter: build it with Emscripten"
#endif

#include "host/web/async_wasm_module_services.h"

// Implemented by rex86_wasm_host.js.
// owner names the adapter whose queue receives the result.
extern "C" void rex86_wasm_install_async(const void* owner, std::uint32_t ticket,
                                         const std::uint8_t* bytes, std::uint32_t size,
                                         std::uint32_t export_count);
extern "C" int rex86_wasm_take(const void* owner, std::uint32_t* ticket,
                               std::uint32_t* table_indices, std::uint32_t capacity);
extern "C" void rex86_wasm_forget(const void* owner);
extern "C" void rex86_wasm_release(const std::uint32_t* table_indices, std::uint32_t count);

namespace rex86::host::web
{

namespace
{
constexpr std::uint32_t kMaxExports = 64;
}

AsyncWebWasmModuleServices::~AsyncWebWasmModuleServices()
{
    rex86_wasm_forget(this);
}

WasmInstall AsyncWebWasmModuleServices::Install(const std::uint32_t ticket, const std::uint8_t* bytes,
                                                const std::size_t size,
                                                const std::uint32_t export_count, std::uint32_t*)
{
    if (export_count > kMaxExports)
    {
        return WasmInstall::kFailed;
    }
    rex86_wasm_install_async(this, ticket, bytes, static_cast<std::uint32_t>(size), export_count);
    ++pending_;
    return WasmInstall::kPending;
}

void AsyncWebWasmModuleServices::Release(const std::uint32_t* table_indices, const std::uint32_t count)
{
    rex86_wasm_release(table_indices, count);
}

std::uint32_t AsyncWebWasmModuleServices::Poll(Cpu& cpu)
{
    std::uint32_t delivered = 0;
    std::uint32_t indices[kMaxExports];
    while (true)
    {
        std::uint32_t ticket = 0;
        const int count = rex86_wasm_take(this, &ticket, indices, kMaxExports);
        if (count < 0)
        {
            return delivered;
        }
        if (pending_ != 0)
        {
            --pending_;
        }
        if (count == 0)
        {
            ++failed_;
            cpu.FailWasmModule(ticket);
        }
        else
        {
            ++installed_;
            cpu.CompleteWasmModule(ticket, indices, static_cast<std::uint32_t>(count));
        }
        ++delivered;
    }
}

}  // namespace rex86::host::web
