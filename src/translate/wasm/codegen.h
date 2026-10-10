// IR to wasm (design #45, decision 2): a block becomes one function
// (state, memory, base) -> exit code, where state is a CpuState*, memory a
// GuestMemory* and base the linear address of guest address 0. The exit code
// is (steps << 1) | kind, with EIP already written into the state. Each
// operation reproduces the evaluator's definition (translate/ir/evaluator).

#ifndef REX86_TRANSLATE_WASM_CODEGEN_H_
#define REX86_TRANSLATE_WASM_CODEGEN_H_

#include <cstdint>

#include "translate/ir/evaluator.h"
#include "translate/ir/ir.h"
#include "translate/wasm/module_writer.h"

namespace rex86::translate::wasm
{

// The function body of one block. check_helper is the table index of the
// function judging Check operations, of type (state, memory, segment, offset,
// bytes | write << 8) -> 1 when the access would succeed.
ModuleSpec::Function CompileBlock(const ir::Block& block, std::uint32_t check_helper);

// A module exporting one function per block, as b0, b1, ...
Bytes CompileModule(const ir::Block* const* blocks, std::uint32_t count,
                    std::uint32_t check_helper);

inline std::uint32_t EncodeExit(const ir::ExitKind kind, const std::uint32_t steps)
{
    return (steps << 1) | (kind == ir::ExitKind::kInterpret ? 1u : 0u);
}

inline ir::ExitResult DecodeExit(const std::uint32_t code, const std::uint32_t eip)
{
    return ir::ExitResult{(code & 1u) != 0 ? ir::ExitKind::kInterpret : ir::ExitKind::kContinue, eip,
                          code >> 1};
}

}  // namespace rex86::translate::wasm

#endif  // REX86_TRANSLATE_WASM_CODEGEN_H_
