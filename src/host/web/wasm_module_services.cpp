#if !defined(__EMSCRIPTEN__)
#error "src/host/web/ is the web host's adapter: build it with Emscripten"
#endif

#include "host/web/wasm_module_services.h"

#include <chrono>

// Implemented by rex86_wasm_host.js.
extern "C" int rex86_wasm_install(const std::uint8_t* bytes, std::uint32_t size,
                                  std::uint32_t export_count, std::uint32_t* table_indices);
extern "C" void rex86_wasm_release(const std::uint32_t* table_indices, std::uint32_t count);

namespace rex86::host::web
{

WasmInstall WebWasmModuleServices::Install(std::uint32_t, const std::uint8_t* bytes,
                                           const std::size_t size, const std::uint32_t export_count,
                                           std::uint32_t* table_indices)
{
    const auto start = std::chrono::steady_clock::now();
    const int ok = rex86_wasm_install(bytes, static_cast<std::uint32_t>(size), export_count,
                                      table_indices);
    install_ms_ +=
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    if (ok == 0)
    {
        return WasmInstall::kFailed;
    }
    ++installed_;
    return WasmInstall::kInstalled;
}

void WebWasmModuleServices::Release(const std::uint32_t* table_indices, const std::uint32_t count)
{
    rex86_wasm_release(table_indices, count);
}

}  // namespace rex86::host::web
