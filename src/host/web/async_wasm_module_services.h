// The asynchronous reference WasmModuleServices for a web host (design
// #48, decision 2): Install starts WebAssembly.compile and instantiate and
// answers kPending; the host calls Poll between frames, outside Run, which
// hands every finished installation to the Cpu. Promises settle only while
// the event loop runs, so the host must yield between frames, as a web
// page's frame loop does. It suits the main thread, where synchronous
// compilation is limited.
//
// Tickets are numbered per Cpu, so one adapter serves one Cpu, and outlives
// it. Destroying the adapter gives back the table slots of installations not
// yet delivered. Link the target rex86_host_web.

#ifndef REX86_HOST_WEB_ASYNC_WASM_MODULE_SERVICES_H_
#define REX86_HOST_WEB_ASYNC_WASM_MODULE_SERVICES_H_

#include <cstddef>
#include <cstdint>

#include "rex86/cpu.h"
#include "rex86/environment.h"

namespace rex86::host::web
{

class AsyncWebWasmModuleServices final : public WasmModuleServices
{
public:
    AsyncWebWasmModuleServices() = default;
    AsyncWebWasmModuleServices(const AsyncWebWasmModuleServices&) = delete;
    AsyncWebWasmModuleServices& operator=(const AsyncWebWasmModuleServices&) = delete;
    ~AsyncWebWasmModuleServices() override;

    WasmInstall Install(std::uint32_t ticket, const std::uint8_t* bytes, std::size_t size,
                        std::uint32_t export_count, std::uint32_t* table_indices) override;
    void Release(const std::uint32_t* table_indices, std::uint32_t count) override;

    // Delivers every installation that finished since the last call; returns
    // how many were delivered.
    std::uint32_t Poll(Cpu& cpu);

    // Installations started and not yet delivered.
    [[nodiscard]] std::uint32_t pending() const { return pending_; }
    [[nodiscard]] std::uint64_t installed() const { return installed_; }
    [[nodiscard]] std::uint64_t failed() const { return failed_; }

private:
    std::uint32_t pending_ = 0;
    std::uint64_t installed_ = 0;
    std::uint64_t failed_ = 0;
};

}  // namespace rex86::host::web

#endif  // REX86_HOST_WEB_ASYNC_WASM_MODULE_SERVICES_H_
