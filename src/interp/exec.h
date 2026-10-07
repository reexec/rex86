// Shared between the interpreter's dispatch (interpreter.cpp) and the
// instruction-group files that later increments add. The dispatch tries
// its own switch first and hands everything else to ExecuteExtended.

#ifndef REX86_INTERP_EXEC_H_
#define REX86_INTERP_EXEC_H_

#include <cstdint>

#include "interp/access.h"
#include "rex86/environment.h"

namespace rex86::interp
{

// What one executed instruction asks of the loop.
enum class ExecStatus : std::uint8_t
{
    kContinue,
    kStop,           // retired, event filled (HLT, INT n, port I/O)
    kFault,          // not retired, ctx.fault filled
    kUnimplemented,  // decoded but not in this increment
};

// The sixteen Jcc/SETcc condition codes, by their opcode low nibble.
bool ConditionCodeHolds(unsigned condition_code, const CpuState& state);

// Increment 2 (#13): shifts/rotates, SHLD/SHRD, MUL/IMUL/DIV/IDIV, the
// BT family, BSF/BSR, SETcc, ENTER/LEAVE, XLAT.
ExecStatus ExecuteArith2(Ctx* ctx, Event* stop_event);

// Increment 2 (#13): MOVS/STOS/LODS/SCAS/CMPS with REP/REPE/REPNE.
ExecStatus ExecuteStrings(Ctx* ctx);

// The tail of the dispatch: every group file in order, then
// kUnimplemented.
ExecStatus ExecuteExtended(Ctx* ctx, Event* stop_event);

}  // namespace rex86::interp

#endif  // REX86_INTERP_EXEC_H_
