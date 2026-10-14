#include "translate/wasm/backend.h"

#include <algorithm>
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

WasmBackend::WasmBackend(WasmModuleServices* services, const std::uint32_t blocks_per_module)
    : services_(services), batch_(std::clamp(blocks_per_module, 1u, kMaxBatchBlocks))
{
}

CompileStatus WasmBackend::Compile(Translation* const* translations, const std::uint32_t count,
                                   const std::uint32_t ticket)
{
    if (count == 0 || count > kMaxBatchBlocks)
    {
        return CompileStatus::kFailed;
    }
    const ir::Block* blocks[kMaxBatchBlocks];
    for (std::uint32_t i = 0; i < count; ++i)
    {
        blocks[i] = &translations[i]->block;
    }
    // The context's address is a wasm address in wasm32 alone; elsewhere
    // the modules are never run and check through the helper.
    const std::uint32_t context =
        Runnable() ? static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&context_)) : 0u;
    Bytes bytes = CompileModule(blocks, count, CheckHelperIndex(), context);
    std::uint32_t indices[kMaxBatchBlocks];
    const WasmInstall answer = services_->Install(ticket, bytes.data(), bytes.size(), count, indices);
    switch (answer)
    {
        case WasmInstall::kInstalled:
            for (std::uint32_t i = 0; i < count; ++i)
            {
                translations[i]->handle = indices[i];
            }
            return CompileStatus::kReady;
        case WasmInstall::kPending:
            pending_bytes_[ticket] = std::move(bytes);
            return CompileStatus::kPending;
        case WasmInstall::kFailed:
        default:
            return CompileStatus::kFailed;
    }
}

ir::ExitResult WasmBackend::Run(const Translation& translation, CpuState& state, GuestMemory& memory,
                                const RunLimits& limits)
{
#if defined(__EMSCRIPTEN__)
    context_.flags = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(memory.pages().data()));
    context_.size = memory.size();
    context_.max_steps = limits.max_steps;
    context_.attention = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(limits.attention));
    context_.loops = 0;
    const auto function =
        reinterpret_cast<BlockFunction>(static_cast<std::uintptr_t>(translation.handle));
    const std::uint32_t code = function(&state, &memory,
                                        static_cast<std::uint32_t>(
                                            reinterpret_cast<std::uintptr_t>(memory.base())));
    ir::ExitResult exit = DecodeExit(code, state.eip);
    exit.loops = context_.loops;
    return exit;
#else
    // SetTranslation never turns kWasm on outside wasm32; should a host
    // installer answer anyway, the block is left to the interpreter.
    static_cast<void>(translation);
    static_cast<void>(memory);
    static_cast<void>(limits);
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
