#include "interp/x87_stack.h"

#include "fpu/x87_math.h"

namespace rex86::interp::x87
{

unsigned Top(const X87State& x87)
{
    return (x87.status_word >> 11) & 7u;
}

void SetTop(X87State* x87, const unsigned top)
{
    x87->status_word = static_cast<std::uint16_t>(
        (x87->status_word & ~0x3800u) | ((top & 7u) << 11));
}

unsigned Physical(const X87State& x87, const unsigned st)
{
    return (Top(x87) + st) & 7u;
}

unsigned Tag(const X87State& x87, const unsigned physical)
{
    return (x87.tag_word >> (2 * physical)) & 3u;
}

void SetTag(X87State* x87, const unsigned physical, const unsigned tag)
{
    x87->tag_word = static_cast<std::uint16_t>(
        (x87->tag_word & ~(3u << (2 * physical))) | ((tag & 3u) << (2 * physical)));
}

unsigned TagFor(const fpu::Float80& value)
{
    switch (fpu::Classify(value))
    {
        case fpu::Kind::kNormal: return kTagValid;
        case fpu::Kind::kZero: return kTagZero;
        default: return kTagSpecial;
    }
}

bool IsEmpty(const X87State& x87, const unsigned st)
{
    return Tag(x87, Physical(x87, st)) == kTagEmpty;
}

fpu::Float80 Read(const X87State& x87, const unsigned st)
{
    return fpu::FromBytes(x87.registers[Physical(x87, st)].data());
}

void Write(X87State* x87, const unsigned st, const fpu::Float80& value)
{
    const unsigned physical = Physical(*x87, st);
    fpu::ToBytes(value, x87->registers[physical].data());
    SetTag(x87, physical, TagFor(value));
}

void Push(X87State* x87, const fpu::Float80& value)
{
    SetTop(x87, Top(*x87) - 1u);
    Write(x87, 0, value);
}

void Pop(X87State* x87)
{
    SetTag(x87, Physical(*x87, 0), kTagEmpty);
    SetTop(x87, Top(*x87) + 1u);
}

void UpdateErrorSummary(X87State* x87)
{
    const std::uint16_t pending = static_cast<std::uint16_t>(
        x87->status_word & ~x87->control_word & fpu::kExceptionMask);
    if (pending != 0)
    {
        x87->status_word |= kErrorSummary | kBusy;
    }
    else
    {
        x87->status_word &= static_cast<std::uint16_t>(~(kErrorSummary | kBusy));
    }
}

void Raise(X87State* x87, const std::uint16_t flags)
{
    x87->status_word |= flags & (fpu::kExceptionMask | fpu::kStackFault);
    UpdateErrorSummary(x87);
}

void SetConditions(X87State* x87, const std::uint16_t mask,
                   const std::uint16_t value)
{
    x87->status_word = static_cast<std::uint16_t>(
        (x87->status_word & ~mask) | (value & mask));
}

std::uint16_t CanonicalControlWord(const std::uint16_t value)
{
    return static_cast<std::uint16_t>((value & 0x1F3Fu) | 0x0040u);
}

}  // namespace rex86::interp::x87
