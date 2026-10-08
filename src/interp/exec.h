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
    kStopNoRetire,   // not retired, event filled (a declined INS/OUTS)
    kFault,          // not retired, ctx.fault filled
    kUnimplemented,  // decoded but not in this increment
};

// The sixteen Jcc/SETcc condition codes, by their opcode low nibble.
bool ConditionCodeHolds(unsigned condition_code, const CpuState& state);

// Increment 2 (#13): shifts/rotates, SHLD/SHRD, MUL/IMUL/DIV/IDIV, the
// BT family, BSF/BSR, SETcc, ENTER/LEAVE, XLAT.
ExecStatus ExecuteArith2(Ctx* ctx, Event* stop_event);

// Increments 2-3 (#13, #15): MOVS/STOS/LODS/SCAS/CMPS and INS/OUTS with
// REP/REPE/REPNE.
ExecStatus ExecuteStrings(Ctx* ctx, Event* stop_event);

// Increment 3 (#15): segment-register loads, far JMP/CALL/RETF, IRET,
// and the privileged instructions. next_eip is the fallthrough and
// is overwritten by a taken far branch.
ExecStatus ExecuteSegments(Ctx* ctx, std::uint32_t* next_eip);

// x87 increment 1 (#19): every x87 instruction but the transcendentals,
// FWAIT's #MF included. kUnimplemented for anything that is not x87.
ExecStatus ExecuteX87(Ctx* ctx);

// The x87 control and environment instructions, called by ExecuteX87.
ExecStatus ExecuteX87Control(Ctx* ctx);

// Increment 3 (#15): AAA/AAS/DAA/DAS/AAM/AAD, BOUND, SALC.
ExecStatus ExecuteBcd(Ctx* ctx);

// The integer instructions after the 386 (#22): BSWAP, XADD, CMPXCHG,
// CMPXCHG8B, CMOVcc and UD0/UD1/UD2.
ExecStatus ExecutePost386(Ctx* ctx);

// The tail of the dispatch: every group file in order, then
// kUnimplemented.
ExecStatus ExecuteExtended(Ctx* ctx, std::uint32_t* next_eip,
                           Event* stop_event);

}  // namespace rex86::interp

#endif  // REX86_INTERP_EXEC_H_
