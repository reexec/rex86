// IR optimization (design #43, decision 6): dead state writes and dead
// values. The state must be precise wherever the block can exit, so a
// write survives whenever a check or an exit lies between it and the next
// write of the same register or flag.

#ifndef REX86_TRANSLATE_IR_OPTIMIZE_H_
#define REX86_TRANSLATE_IR_OPTIMIZE_H_

#include "translate/ir/ir.h"

namespace rex86::translate::ir
{

// Removes dead writes, then the pure operations nobody uses, renumbering
// the values that remain.
void Optimize(Block* block);

}  // namespace rex86::translate::ir

#endif  // REX86_TRANSLATE_IR_OPTIMIZE_H_
