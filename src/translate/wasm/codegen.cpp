#include "translate/wasm/codegen.h"

#include <cstddef>
#include <type_traits>

#include "rex86/cpu_state.h"

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

std::uint32_t SegmentBaseOffset(const std::uint8_t segment)
{
    return static_cast<std::uint32_t>(offsetof(CpuState, segments) +
                                      segment * sizeof(SegmentRegister) +
                                      offsetof(SegmentRegister, base));
}

class Generator
{
public:
    Generator(const ir::Block& block, const std::uint32_t check_helper)
        : block_(block), check_helper_(check_helper), current_eip_(block.start_eip)
    {
    }

    ModuleSpec::Function Run()
    {
        for (std::size_t i = 0; i < block_.insts.size(); ++i)
        {
            Emit(static_cast<ir::Value>(i), block_.insts[i]);
        }
        // The evaluator's fallback for a block without a final exit.
        ExitConst(ir::ExitKind::kInterpret, current_eip_, current_index_);
        ModuleSpec::Function function;
        function.locals = static_cast<std::uint32_t>(block_.insts.size());
        function.code = code_.bytes();
        return function;
    }

private:
    static std::uint32_t Local(const ir::Value value) { return kFirstValueLocal + value; }

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

    void StoreEip(const std::uint32_t eip)
    {
        code_.LocalGet(kStateParam);
        code_.I32Const(eip);
        code_.Memory(op::kI32Store, kEipOffset);
    }

    void ExitConst(const ir::ExitKind kind, const std::uint32_t eip, const std::uint32_t steps)
    {
        StoreEip(eip);
        code_.I32Const(EncodeExit(kind, steps));
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
                return;
            case ir::Op::kBegin:
                current_index_ = inst.index;
                current_eip_ = inst.imm;
                return;
            case ir::Op::kExitIf:
                Get(inst.a);
                code_.If();
                ExitConst(inst.kind, inst.imm, inst.index);
                code_.Op(op::kEnd);
                return;
            case ir::Op::kExit:
                if (inst.dynamic)
                {
                    code_.LocalGet(kStateParam);
                    Get(inst.a);
                    code_.Memory(op::kI32Store, kEipOffset);
                    code_.I32Const(EncodeExit(inst.kind, inst.index));
                    code_.Op(op::kReturn);
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
    Code code_;
    std::uint32_t current_index_ = 0;
    std::uint32_t current_eip_;
};

}  // namespace

ModuleSpec::Function CompileBlock(const ir::Block& block, const std::uint32_t check_helper)
{
    return Generator(block, check_helper).Run();
}

Bytes CompileModule(const ir::Block* const* blocks, const std::uint32_t count,
                    const std::uint32_t check_helper)
{
    ModuleSpec spec;
    for (std::uint32_t i = 0; i < count; ++i)
    {
        spec.functions.push_back(CompileBlock(*blocks[i], check_helper));
    }
    return WriteModule(spec);
}

}  // namespace rex86::translate::wasm
