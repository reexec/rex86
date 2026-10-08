// The x87 register stack and status word as the interpreter sees them:
// TOP, the physical registers behind ST(i), the 2-bit tags, the condition
// codes and the exception summary. Pure state helpers over X87State; the
// numeric work lives in src/fpu/. See design #19, decision 2.

#ifndef REX86_INTERP_X87_STACK_H_
#define REX86_INTERP_X87_STACK_H_

#include <cstdint>

#include "fpu/float80.h"
#include "fpu/x87_math.h"
#include "rex86/cpu_state.h"

namespace rex86::interp::x87
{

inline constexpr std::uint16_t kC0 = 0x0100;
inline constexpr std::uint16_t kC1 = 0x0200;
inline constexpr std::uint16_t kC2 = 0x0400;
inline constexpr std::uint16_t kC3 = 0x4000;
inline constexpr std::uint16_t kConditionMask = kC0 | kC1 | kC2 | kC3;
inline constexpr std::uint16_t kErrorSummary = 0x0080;
inline constexpr std::uint16_t kBusy = 0x8000;

// The FNSTENV tags.
inline constexpr unsigned kTagValid = 0;
inline constexpr unsigned kTagZero = 1;
inline constexpr unsigned kTagSpecial = 2;
inline constexpr unsigned kTagEmpty = 3;

unsigned Top(const X87State& x87);
void SetTop(X87State* x87, unsigned top);

// ST(i) -> physical register number.
unsigned Physical(const X87State& x87, unsigned st);

unsigned Tag(const X87State& x87, unsigned physical);
void SetTag(X87State* x87, unsigned physical, unsigned tag);
unsigned TagFor(const fpu::Float80& value);

bool IsEmpty(const X87State& x87, unsigned st);
fpu::Float80 Read(const X87State& x87, unsigned st);
// Writes ST(i) and recomputes its tag.
void Write(X87State* x87, unsigned st, const fpu::Float80& value);
void Push(X87State* x87, const fpu::Float80& value);
void Pop(X87State* x87);

// Merges raised exception flags into SW and recomputes ES and B (set when
// any flagged exception is unmasked in CW).
void Raise(X87State* x87, std::uint16_t flags);
void UpdateErrorSummary(X87State* x87);

void SetConditions(X87State* x87, std::uint16_t mask, std::uint16_t value);

// The x87's stack-fault response (SDM 8.5.1.1): #IS with C1 = 1 for an
// overflow, 0 for an underflow. Returns true when the fault is masked and
// the instruction goes on with the indefinite.
bool StackFault(X87State* x87, bool overflow);

// Folds one operation's status into SW. C1 reports the rounding
// direction of a written inexact result and is cleared otherwise.
void Commit(X87State* x87, const fpu::Status& status, bool written);

// A control word as FLDCW, FLDENV and FRSTOR load it: the reserved bits
// 7 and 13-15 read as zero and bit 6 as one (measured, #19).
std::uint16_t CanonicalControlWord(std::uint16_t value);

}  // namespace rex86::interp::x87

#endif  // REX86_INTERP_X87_STACK_H_
