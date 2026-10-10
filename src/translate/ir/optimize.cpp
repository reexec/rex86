#include "translate/ir/optimize.h"

#include <cstdint>
#include <vector>

namespace rex86::translate::ir
{

namespace
{

// Marks writes overwritten before anything reads them or the block can
// exit. Scans backward, remembering which targets a later write covers.
void MarkDeadWrites(const Block& block, std::vector<bool>* dead)
{
    std::uint32_t covered_registers = 0;
    std::uint32_t covered_flags = 0;
    for (std::size_t i = block.insts.size(); i-- > 0;)
    {
        const Inst& inst = block.insts[i];
        switch (inst.op)
        {
            case Op::kCheck:
            case Op::kExitIf:
            case Op::kExit:
                covered_registers = 0;
                covered_flags = 0;
                break;
            case Op::kGetReg:
                covered_registers &= ~(1u << inst.slot);
                break;
            case Op::kGetFlag:
                covered_flags &= ~inst.imm;
                break;
            case Op::kSetReg:
                if ((covered_registers & (1u << inst.slot)) != 0)
                {
                    (*dead)[i] = true;
                }
                covered_registers |= 1u << inst.slot;
                break;
            case Op::kSetFlag:
                if ((covered_flags & inst.imm) == inst.imm)
                {
                    (*dead)[i] = true;
                }
                covered_flags |= inst.imm;
                break;
            default:
                break;
        }
    }
}

}  // namespace

void Optimize(Block* block)
{
    const std::size_t count = block->insts.size();
    std::vector<bool> dead(count, false);
    MarkDeadWrites(*block, &dead);

    // Values used by a surviving operation, found backward so that a pure
    // operation is kept only when something kept uses it.
    std::vector<bool> used(count, false);
    for (std::size_t i = count; i-- > 0;)
    {
        const Inst& inst = block->insts[i];
        if (dead[i])
        {
            continue;
        }
        if (IsPure(inst.op) && !used[i])
        {
            dead[i] = true;
            continue;
        }
        for (const Value operand : {inst.a, inst.b, inst.c})
        {
            if (operand != kNoValue)
            {
                used[operand] = true;
            }
        }
    }

    std::vector<Value> renumbered(count, kNoValue);
    std::vector<Inst> kept;
    kept.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        if (dead[i])
        {
            continue;
        }
        Inst inst = block->insts[i];
        for (Value* operand : {&inst.a, &inst.b, &inst.c})
        {
            if (*operand != kNoValue)
            {
                *operand = renumbered[*operand];
            }
        }
        renumbered[i] = static_cast<Value>(kept.size());
        kept.push_back(inst);
    }
    block->insts = std::move(kept);
}

}  // namespace rex86::translate::ir
