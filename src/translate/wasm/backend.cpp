#include "translate/wasm/backend.h"

#include <cstdint>
#include <utility>

#include "translate/ir/evaluator.h"
#include "translate/wasm/codegen.h"

namespace rex86::translate::wasm
{

namespace
{

// Called from generated code for every Check: the evaluator's own judgment,
// so both backends agree by construction. In wasm32 its address is an index
// into the table the modules import.
std::uint32_t CheckHelper(const CpuState* state, const GuestMemory* memory,
                          const std::uint32_t segment, const std::uint32_t offset,
                          const std::uint32_t bytes_and_write)
{
    return ir::AccessWouldSucceed(*state, *memory, static_cast<Segment>(segment), offset,
                                  bytes_and_write & 0xFFu, (bytes_and_write & 0x100u) != 0)
        ? 1u
        : 0u;
}

using BlockFunction = std::uint32_t (*)(CpuState*, GuestMemory*, std::uint32_t);

}  // namespace

bool Runnable()
{
#if defined(__EMSCRIPTEN__)
    return true;
#else
    return false;
#endif
}

std::uint32_t CheckHelperIndex()
{
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&CheckHelper));
}

CompileStatus WasmBackend::Compile(Translation* translation)
{
    const ir::Block* blocks[] = {&translation->block};
    Bytes bytes = CompileModule(blocks, 1, CheckHelperIndex());
    std::uint32_t index = 0;
    const WasmInstall answer =
        services_->Install(translation->ticket, bytes.data(), bytes.size(), 1, &index);
    switch (answer)
    {
        case WasmInstall::kInstalled:
            translation->handle = index;
            return CompileStatus::kReady;
        case WasmInstall::kPending:
            pending_bytes_[translation->ticket] = std::move(bytes);
            return CompileStatus::kPending;
        case WasmInstall::kFailed:
        default:
            return CompileStatus::kFailed;
    }
}

ir::ExitResult WasmBackend::Run(const Translation& translation, CpuState& state, GuestMemory& memory)
{
#if defined(__EMSCRIPTEN__)
    const auto function =
        reinterpret_cast<BlockFunction>(static_cast<std::uintptr_t>(translation.handle));
    const std::uint32_t code = function(&state, &memory,
                                        static_cast<std::uint32_t>(
                                            reinterpret_cast<std::uintptr_t>(memory.base())));
    return DecodeExit(code, state.eip);
#else
    // SetTranslation never turns kWasm on outside wasm32; should a host
    // installer answer anyway, the block is left to the interpreter.
    static_cast<void>(translation);
    static_cast<void>(memory);
    return ir::ExitResult{ir::ExitKind::kInterpret, state.eip, 0};
#endif
}

void WasmBackend::Drop(const Translation& translation)
{
    services_->Release(&translation.handle, 1);
}

void WasmBackend::Finished(const std::uint32_t ticket)
{
    pending_bytes_.erase(ticket);
}

void WasmBackend::Discard(const std::uint32_t* handles, const std::uint32_t count)
{
    if (count != 0)
    {
        services_->Release(handles, count);
    }
}

std::size_t WasmBackend::FootprintBytes() const
{
    std::size_t bytes = 0;
    for (const auto& item : pending_bytes_)
    {
        bytes += item.second.capacity();
    }
    return bytes;
}

}  // namespace rex86::translate::wasm
