// The wasm backend (design #45, decision 3): compiles each translated block
// into a module, hands it to the host's WasmModuleServices, and calls the
// installed function through the indirect function table. Module bytes can
// be made on any host; running them needs a wasm32 build.

#ifndef REX86_TRANSLATE_WASM_BACKEND_H_
#define REX86_TRANSLATE_WASM_BACKEND_H_

#include <cstdint>
#include <unordered_map>

#include "rex86/environment.h"
#include "translate/runtime.h"
#include "translate/wasm/module_writer.h"

namespace rex86::translate::wasm
{

// True in a build whose generated code can run (wasm32).
bool Runnable();

// The table index of the check helper generated code calls; meaningful in
// wasm32 builds only.
std::uint32_t CheckHelperIndex();

class WasmBackend final : public Backend
{
public:
    explicit WasmBackend(WasmModuleServices* services) : services_(services) {}

    CompileStatus Compile(Translation* translation) override;
    ir::ExitResult Run(const Translation& translation, CpuState& state, GuestMemory& memory) override;
    void Drop(const Translation& translation) override;
    void Finished(std::uint32_t ticket) override;
    void Discard(const std::uint32_t* handles, std::uint32_t count) override;
    [[nodiscard]] std::size_t FootprintBytes() const override;

private:
    WasmModuleServices* services_;
    // Module bytes a pending installation may still read.
    std::unordered_map<std::uint32_t, Bytes> pending_bytes_;
};

}  // namespace rex86::translate::wasm

#endif  // REX86_TRANSLATE_WASM_BACKEND_H_
