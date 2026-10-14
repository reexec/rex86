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

// What inline checks read (design #65, decision 3), at an address the
// generated code embeds: the guest memory's page flags (a PageFlag*) and its
// size, which the backend refreshes before each run.
struct CheckContext
{
    std::uint32_t flags = 0;
    std::uint32_t size = 0;
};

// The function body of one block. check_helper is the table index of the
// function judging Check operations, of type (state, memory, segment, offset,
// bytes | write << 8) -> 1 when the access would succeed. check_context, when
// not 0, is the wasm address of a CheckContext: a Check then calls the
// helper only when an inline test cannot vouch for the access.
ModuleSpec::Function CompileBlock(const ir::Block& block, std::uint32_t check_helper,
                                  std::uint32_t check_context = 0);

// A module exporting one function per block, as b0, b1, ...
Bytes CompileModule(const ir::Block* const* blocks, std::uint32_t count,
                    std::uint32_t check_helper, std::uint32_t check_context = 0);

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
