#include "translate/wasm/codegen.h"

#include <cstddef>
#include <type_traits>

#include "rex86/cpu_state.h"
#include "rex86/guest_memory.h"

namespace rex86::translate::wasm
{

namespace
{

static_assert(std::is_standard_layout_v<CpuState>, "generated code addresses CpuState fields");
static_assert(std::is_standard_layout_v<SegmentRegister>, "generated code addresses segment fields");

constexpr std::uint32_t kStateParam = 0;
constexpr std::uint32_t kMemoryParam = 1;
constexpr std::uint32_t kBaseParam = 2;
constexpr std::uint32_t kFirstValueLocal = 3;

constexpr std::uint32_t kGprOffset = offsetof(CpuState, gpr);
constexpr std::uint32_t kEipOffset = offsetof(CpuState, eip);
constexpr std::uint32_t kEflagsOffset = offsetof(CpuState, eflags);

std::uint32_t SegmentOffset(const std::uint8_t segment)
{
    return static_cast<std::uint32_t>(offsetof(CpuState, segments) + segment * sizeof(SegmentRegister));
}

std::uint32_t SegmentBaseOffset(const std::uint8_t segment)
{
    return SegmentOffset(segment) + static_cast<std::uint32_t>(offsetof(SegmentRegister, base));
}

// The run context's fields (RunContext in codegen.h).
constexpr std::uint32_t kContextFlags = 0;
constexpr std::uint32_t kContextSize = 4;
constexpr std::uint32_t kContextMaxSteps = 8;
constexpr std::uint32_t kContextAttention = 12;
constexpr std::uint32_t kContextLoops = 16;
static_assert(offsetof(RunContext, flags) == kContextFlags && offsetof(RunContext, size) == kContextSize &&
                  offsetof(RunContext, max_steps) == kContextMaxSteps &&
                  offsetof(RunContext, attention) == kContextAttention &&
                  offsetof(RunContext, loops) == kContextLoops,
              "generated code reads the run context by these offsets");

class Generator
{
public:
    Generator(const ir::Block& block, const std::uint32_t check_helper, const std::uint32_t check_context)
        : block_(block), check_helper_(check_helper), check_context_(check_context),
          loops_(check_context != 0 && LoopsToStart(block)), current_eip_(block.start_eip)
    {
    }

    ModuleSpec::Function Run()
    {
        // A block that exits to its own start runs its passes in a wasm loop
        // (design #67).
        if (loops_)
        {
            code_.Loop();
        }
        for (std::size_t i = 0; i < block_.insts.size(); ++i)
        {
            Emit(static_cast<ir::Value>(i), block_.insts[i]);
        }
        if (loops_)
        {
            code_.Op(op::kEnd);
        }
        // The evaluator's fallback for a block without a final exit.
        ExitConst(ir::ExitKind::kInterpret, current_eip_, current_index_);
        ModuleSpec::Function function;
        // The values, then the temporaries.
        function.locals = static_cast<std::uint32_t>(block_.insts.size()) + kTemporaries;
        function.code = code_.bytes();
        return function;
    }

private:
    // The inline check's three (design #65), then the steps of the passes
    // already run in place (design #67).
    static constexpr std::uint32_t kTemporaries = 4;

    // An exit to the block's own start, which a loop can take in place.
    static bool IsLoopBack(const ir::Block& block, const ir::Inst& inst)
    {
        return (inst.op == ir::Op::kExitIf || (inst.op == ir::Op::kExit && !inst.dynamic)) &&
               inst.kind == ir::ExitKind::kContinue && inst.imm == block.start_eip;
    }

    static bool LoopsToStart(const ir::Block& block)
    {
        for (const ir::Inst& inst : block.insts)
        {
            if (IsLoopBack(block, inst))
            {
                return true;
            }
        }
        return false;
    }

    static std::uint32_t Local(const ir::Value value) { return kFirstValueLocal + value; }

    // The inline check's temporaries, after the values.
    [[nodiscard]] std::uint32_t Temporary(const std::uint32_t i) const
    {
        return kFirstValueLocal + static_cast<std::uint32_t>(block_.insts.size()) + i;
    }

    void Get(const ir::Value value) { code_.LocalGet(Local(value)); }

    // The linear address of segment:offset in the wasm memory.
    void Address(const std::uint8_t segment, const ir::Value offset)
    {
        code_.LocalGet(kBaseParam);
        code_.LocalGet(kStateParam);
        code_.Memory(op::kI32Load, SegmentBaseOffset(segment));
        code_.Op(op::kI32Add);
        Get(offset);
        code_.Op(op::kI32Add);
    }

    // Sets temporary 0 when the access surely succeeds (design #65, decision
    // 3): a flat segment, writable when written, the range inside memory, and
    // its first and last pages mapped and readable or writable, and when
    // written not translated. That is AccessWouldSucceed restated for its
    // common case, never more permissive; anything else leaves it clear and
    // the helper judges. The pages are read only once the range is known to
    // be inside memory, so the flags array is never read past its end.
    void InlineCheck(const ir::Inst& inst)
    {
        const std::uint32_t segment = SegmentOffset(inst.slot);
        const std::uint32_t ok = Temporary(0);
        const std::uint32_t first = Temporary(1);
        const std::uint32_t last = Temporary(2);
        code_.LocalGet(kStateParam);
        code_.Memory(op::kI32Load, segment + static_cast<std::uint32_t>(offsetof(SegmentRegister, base)));
        code_.Op(op::kI32Eqz);
        code_.LocalGet(kStateParam);
        code_.Memory(op::kI32Load, segment + static_cast<std::uint32_t>(offsetof(SegmentRegister, limit)));
        code_.I32Const(0xFFFFFFFFu);
        code_.Op(op::kI32Eq);
        code_.Op(op::kI32And);
        if (inst.write)
        {
            code_.LocalGet(kStateParam);
            code_.Memory(op::kI32Load8U, segment + static_cast<std::uint32_t>(offsetof(SegmentRegister, writable)));
            code_.Op(op::kI32And);
        }
        // offset < size, and size - offset >= bytes.
        Get(inst.a);
        code_.I32Const(check_context_);
        code_.Memory(op::kI32Load, kContextSize);
        code_.Op(op::kI32LtU);
        code_.Op(op::kI32And);
        code_.I32Const(check_context_);
        code_.Memory(op::kI32Load, kContextSize);
        Get(inst.a);
        code_.Op(op::kI32Sub);
        code_.I32Const(inst.bytes);
        code_.Op(op::kI32LtU);
        code_.Op(op::kI32Eqz);
        code_.Op(op::kI32And);
        code_.LocalSet(ok);
        code_.LocalGet(ok);
        code_.If();
        PageFlags(inst.a, 0, first);
        PageFlags(inst.a, inst.bytes - 1u, last);
        const auto wanted = static_cast<std::uint32_t>(PageFlag::kMapped | (inst.write ? PageFlag::kWrite
                                                                                       : PageFlag::kRead));
        code_.LocalGet(first);
        code_.LocalGet(last);
        code_.Op(op::kI32And);
        code_.I32Const(wanted);
        code_.Op(op::kI32And);
        code_.I32Const(wanted);
        code_.Op(op::kI32Eq);
        if (inst.write)
        {
            code_.LocalGet(first);
            code_.LocalGet(last);
            code_.Op(op::kI32Or);
            code_.I32Const(static_cast<std::uint32_t>(PageFlag::kTranslated));
            code_.Op(op::kI32And);
            code_.Op(op::kI32Eqz);
            code_.Op(op::kI32And);
        }
        code_.LocalSet(ok);
        code_.Op(op::kEnd);
    }

    // into = the flags byte of the page holding offset + delta.
    void PageFlags(const ir::Value offset, const std::uint32_t delta, const std::uint32_t into)
    {
        code_.I32Const(check_context_);
        code_.Memory(op::kI32Load, kContextFlags);
        Get(offset);
        if (delta != 0)
        {
            code_.I32Const(delta);
            code_.Op(op::kI32Add);
        }
        code_.I32Const(kGuestPageShift);
        code_.Op(op::kI32ShrU);
        code_.Op(op::kI32Add);
        code_.Memory(op::kI32Load8U, 0);
        code_.LocalSet(into);
    }

    void StoreEip(const std::uint32_t eip)
    {
        code_.LocalGet(kStateParam);
        code_.I32Const(eip);
        code_.Memory(op::kI32Store, kEipOffset);
    }

    void ExitConst(const ir::ExitKind kind, const std::uint32_t eip, const std::uint32_t steps)
    {
        StoreEip(eip);
        ExitCode(kind, steps);
        code_.Op(op::kReturn);
    }

    // Pushes the exit code for an exit after steps of this pass: in a loop,
    // the passes already run in place count too.
    void ExitCode(const ir::ExitKind kind, const std::uint32_t steps)
    {
        if (!loops_)
        {
            code_.I32Const(EncodeExit(kind, steps));
            return;
        }
        code_.LocalGet(Temporary(3));
        code_.I32Const(steps);
        code_.Op(op::kI32Add);
        code_.I32Const(1);
        code_.Op(op::kI32Shl);
        if (kind == ir::ExitKind::kInterpret)
        {
            code_.I32Const(1);
            code_.Op(op::kI32Or);
        }
    }

    // An exit to the block's own start inside the loop (design #67): count
    // the pass, then run the next one in place when another whole pass fits
    // the steps allowed and attention is clear, as the dispatch would;
    // otherwise exit to the start with every pass counted. depth is how many
    // structured blocks lie between here and the loop.
    void LoopBack(const std::uint32_t steps, const std::uint32_t depth)
    {
        const std::uint32_t done = Temporary(3);
        code_.LocalGet(done);
        code_.I32Const(steps);
        code_.Op(op::kI32Add);
        code_.LocalSet(done);
        code_.LocalGet(done);
        code_.I32Const(block_.instruction_count);
        code_.Op(op::kI32Add);
        code_.I32Const(check_context_);
        code_.Memory(op::kI32Load, kContextMaxSteps);
        code_.Op(op::kI32LeU);
        code_.I32Const(check_context_);
        code_.Memory(op::kI32Load, kContextAttention);
        code_.Memory(op::kI32Load8U, 0);
        code_.Op(op::kI32Eqz);
        code_.Op(op::kI32And);
        code_.If();
        code_.I32Const(check_context_);
        code_.I32Const(check_context_);
        code_.Memory(op::kI32Load, kContextLoops);
        code_.I32Const(1);
        code_.Op(op::kI32Add);
        code_.Memory(op::kI32Store, kContextLoops);
        code_.Br(depth + 1);
        code_.Op(op::kEnd);
        StoreEip(block_.start_eip);
        code_.LocalGet(done);
        code_.I32Const(1);
        code_.Op(op::kI32Shl);
        code_.Op(op::kReturn);
    }

    void Binary(const ir::Inst& inst, const std::uint8_t opcode)
    {
        Get(inst.a);
        Get(inst.b);
        code_.Op(opcode);
    }

    void MulHigh(const ir::Inst& inst, const bool is_signed)
    {
        const std::uint8_t extend = is_signed ? op::kI64ExtendI32S : op::kI64ExtendI32U;
        Get(inst.a);
        code_.Op(extend);
        Get(inst.b);
        code_.Op(extend);
        code_.Op(op::kI64Mul);
        code_.I64Const(32);
        code_.Op(op::kI64ShrU);
        code_.Op(op::kI32WrapI64);
    }

    void Emit(const ir::Value index, const ir::Inst& inst)
    {
        switch (inst.op)
        {
            case ir::Op::kConst: code_.I32Const(inst.imm); break;
            case ir::Op::kGetReg:
                code_.LocalGet(kStateParam);
                code_.Memory(op::kI32Load, kGprOffset + 4u * inst.slot);
                break;
            case ir::Op::kGetFlag:
                code_.LocalGet(kStateParam);
                code_.Memory(op::kI32Load, kEflagsOffset);
                code_.I32Const(inst.imm);
                code_.Op(op::kI32And);
                code_.I32Const(0);
                code_.Op(op::kI32Ne);
                break;
            case ir::Op::kAdd: Binary(inst, op::kI32Add); break;
            case ir::Op::kSub: Binary(inst, op::kI32Sub); break;
            case ir::Op::kAnd: Binary(inst, op::kI32And); break;
            case ir::Op::kOr: Binary(inst, op::kI32Or); break;
            case ir::Op::kXor: Binary(inst, op::kI32Xor); break;
            case ir::Op::kNot:
                Get(inst.a);
                code_.I32Const(0xFFFFFFFFu);
                code_.Op(op::kI32Xor);
                break;
            case ir::Op::kMul: Binary(inst, op::kI32Mul); break;
            case ir::Op::kMulHiS: MulHigh(inst, true); break;
            case ir::Op::kMulHiU: MulHigh(inst, false); break;
            // wasm masks shift counts to 31, as the IR defines them.
            case ir::Op::kShl: Binary(inst, op::kI32Shl); break;
            case ir::Op::kShr: Binary(inst, op::kI32ShrU); break;
            case ir::Op::kSar: Binary(inst, op::kI32ShrS); break;
            case ir::Op::kEq: Binary(inst, op::kI32Eq); break;
            case ir::Op::kNe: Binary(inst, op::kI32Ne); break;
            case ir::Op::kLtU: Binary(inst, op::kI32LtU); break;
            case ir::Op::kLtS: Binary(inst, op::kI32LtS); break;
            case ir::Op::kSelect:
                Get(inst.a);
                Get(inst.b);
                Get(inst.c);
                code_.Op(op::kSelect);
                break;
            case ir::Op::kParity:
                Get(inst.a);
                code_.I32Const(0xFF);
                code_.Op(op::kI32And);
                code_.Op(op::kI32Popcnt);
                code_.I32Const(1);
                code_.Op(op::kI32And);
                code_.Op(op::kI32Eqz);
                break;
            case ir::Op::kLoad:
                Address(inst.slot, inst.a);
                code_.Memory(inst.bytes == 1 ? op::kI32Load8U
                                             : (inst.bytes == 2 ? op::kI32Load16U : op::kI32Load),
                             0);
                break;
            case ir::Op::kSetReg:
                code_.LocalGet(kStateParam);
                Get(inst.a);
                code_.Memory(op::kI32Store, kGprOffset + 4u * inst.slot);
                return;
            case ir::Op::kSetFlag:
                // eflags = (eflags & ~flag) | (a ? flag : 0)
                code_.LocalGet(kStateParam);
                code_.LocalGet(kStateParam);
                code_.Memory(op::kI32Load, kEflagsOffset);
                code_.I32Const(~inst.imm);
                code_.Op(op::kI32And);
                code_.I32Const(inst.imm);
                code_.I32Const(0);
                Get(inst.a);
                code_.Op(op::kSelect);
                code_.Op(op::kI32Or);
                code_.Memory(op::kI32Store, kEflagsOffset);
                return;
            case ir::Op::kStore:
                Address(inst.slot, inst.a);
                Get(inst.b);
                code_.Memory(inst.bytes == 1 ? op::kI32Store8
                                             : (inst.bytes == 2 ? op::kI32Store16 : op::kI32Store),
                             0);
                return;
            case ir::Op::kCheck:
                // With a context, the helper is called only when the inline
                // check cannot vouch for the access.
                if (check_context_ != 0)
                {
                    InlineCheck(inst);
                    code_.LocalGet(Temporary(0));
                    code_.Op(op::kI32Eqz);
                    code_.If();
                }
                code_.LocalGet(kStateParam);
                code_.LocalGet(kMemoryParam);
                code_.I32Const(inst.slot);
                Get(inst.a);
                code_.I32Const(inst.bytes | (inst.write ? 0x100u : 0u));
                code_.I32Const(check_helper_);
                code_.CallIndirect(1);
                code_.Op(op::kI32Eqz);
                code_.If();
                ExitConst(ir::ExitKind::kInterpret, current_eip_, current_index_);
                code_.Op(op::kEnd);
                if (check_context_ != 0)
                {
                    code_.Op(op::kEnd);
                }
                return;
            case ir::Op::kBegin:
                current_index_ = inst.index;
                current_eip_ = inst.imm;
                return;
            case ir::Op::kExitIf:
                Get(inst.a);
                code_.If();
                if (loops_ && IsLoopBack(block_, inst))
                {
                    LoopBack(inst.index, 1);
                }
                else
                {
                    ExitConst(inst.kind, inst.imm, inst.index);
                }
                code_.Op(op::kEnd);
                return;
            case ir::Op::kExit:
                if (inst.dynamic)
                {
                    code_.LocalGet(kStateParam);
                    Get(inst.a);
                    code_.Memory(op::kI32Store, kEipOffset);
                    ExitCode(inst.kind, inst.index);
                    code_.Op(op::kReturn);
                }
                else if (loops_ && IsLoopBack(block_, inst))
                {
                    LoopBack(inst.index, 0);
                }
                else
                {
                    ExitConst(inst.kind, inst.imm, inst.index);
                }
                return;
        }
        // Every value-defining case falls through to here.
        code_.LocalSet(Local(index));
    }

    const ir::Block& block_;
    std::uint32_t check_helper_;
    std::uint32_t check_context_;
    bool loops_;
    Code code_;
    std::uint32_t current_index_ = 0;
    std::uint32_t current_eip_;
};

}  // namespace

ModuleSpec::Function CompileBlock(const ir::Block& block, const std::uint32_t check_helper,
                                  const std::uint32_t check_context)
{
    return Generator(block, check_helper, check_context).Run();
}

Bytes CompileModule(const ir::Block* const* blocks, const std::uint32_t count,
                    const std::uint32_t check_helper, const std::uint32_t check_context)
{
    ModuleSpec spec;
    for (std::uint32_t i = 0; i < count; ++i)
    {
        spec.functions.push_back(CompileBlock(*blocks[i], check_helper, check_context));
    }
    return WriteModule(spec);
}

}  // namespace rex86::translate::wasm
