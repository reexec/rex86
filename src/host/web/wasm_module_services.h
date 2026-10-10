// The reference WasmModuleServices for a web host (design #45, decision 4):
// installs each module synchronously with new WebAssembly.Module and
// new WebAssembly.Instance against the core's own memory and table. That
// suits Node and a Worker; a main-thread host with large modules needs an
// asynchronous installer answering through Cpu::CompleteWasmModule.
//
// Link the target rex86_host_web, which carries the JavaScript library
// (rex86_wasm_host.js) and -sALLOW_TABLE_GROWTH.

#ifndef REX86_HOST_WEB_WASM_MODULE_SERVICES_H_
#define REX86_HOST_WEB_WASM_MODULE_SERVICES_H_

#include <cstddef>
#include <cstdint>

#include "rex86/environment.h"

namespace rex86::host::web
{

class WebWasmModuleServices final : public WasmModuleServices
{
public:
    WasmInstall Install(std::uint32_t ticket, const std::uint8_t* bytes, std::size_t size,
                        std::uint32_t export_count, std::uint32_t* table_indices) override;
    void Release(const std::uint32_t* table_indices, std::uint32_t count) override;

    // Modules installed so far and the time they took, for the benchmark.
    [[nodiscard]] std::uint64_t installed() const { return installed_; }
    [[nodiscard]] double install_ms() const { return install_ms_; }

private:
    std::uint64_t installed_ = 0;
    double install_ms_ = 0.0;
};

}  // namespace rex86::host::web

#endif  // REX86_HOST_WEB_WASM_MODULE_SERVICES_H_
