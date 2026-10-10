#include "translate/ir/evaluator.h"

#include <cstdint>
#include <vector>

namespace rex86::translate::ir
{

bool AccessWouldSucceed(const CpuState& state, const GuestMemory& memory, const Segment segment,
                        const std::uint32_t offset, const unsigned bytes, const bool write)
{
    const SegmentRegister& seg = state.Seg(segment);
    if (write && !seg.writable)
    {
        return false;
    }
    if (!seg.IsFlat() &&
        (!seg.present || offset > seg.limit || seg.limit - offset < bytes - 1u))
    {
        return false;
    }
    const std::uint32_t linear = seg.base + offset;
    if (!memory.Contains(linear, bytes))
    {
        return false;
    }
    const PageFlag wanted = PageFlag::kMapped | (write ? PageFlag::kWrite : PageFlag::kRead);
    if (!memory.pages().AllHave(linear, bytes, wanted))
    {
        return false;
    }
    if (write)
    {
        const std::uint32_t first = linear & ~(kGuestPageSize - 1u);
        const std::uint32_t last = (linear + bytes - 1u) & ~(kGuestPageSize - 1u);
        if (Has(memory.pages().Get(first), PageFlag::kTranslated) ||
            Has(memory.pages().Get(last), PageFlag::kTranslated))
        {
            return false;
        }
    }
    return true;
}

namespace
{

std::uint32_t ReadMemory(const GuestMemory& memory, const std::uint32_t linear,
                         const unsigned bytes)
{
    if (bytes == 1)
    {
        std::uint8_t value = 0;
        memory.Read8(linear, &value);
        return value;
    }
    if (bytes == 2)
    {
        std::uint16_t value = 0;
        memory.Read16(linear, &value);
        return value;
    }
    std::uint32_t value = 0;
    memory.Read32(linear, &value);
    return value;
}

void WriteMemory(GuestMemory& memory, const std::uint32_t linear, const unsigned bytes,
                 const std::uint32_t value)
{
    if (bytes == 1)
    {
        memory.Write8(linear, static_cast<std::uint8_t>(value));
    }
    else if (bytes == 2)
    {
        memory.Write16(linear, static_cast<std::uint16_t>(value));
    }
    else
    {
        memory.Write32(linear, value);
    }
}

bool ParityEven(std::uint32_t value)
{
    std::uint8_t byte = static_cast<std::uint8_t>(value);
    byte ^= static_cast<std::uint8_t>(byte >> 4);
    byte ^= static_cast<std::uint8_t>(byte >> 2);
    byte ^= static_cast<std::uint8_t>(byte >> 1);
    return (byte & 1u) == 0;
}

}  // namespace

ExitResult Evaluate(const Block& block, CpuState& state, GuestMemory& memory)
{
    std::vector<std::uint32_t> values;
    return Evaluate(block, state, memory, &values);
}

ExitResult Evaluate(const Block& block, CpuState& state, GuestMemory& memory,
                    std::vector<std::uint32_t>* buffer)
{
    if (buffer->size() < block.insts.size())
    {
        buffer->resize(block.insts.size());
    }
    std::vector<std::uint32_t>& values = *buffer;
    std::uint32_t current_index = 0;
    std::uint32_t current_eip = block.start_eip;
    const auto exit = [&](const ExitKind kind, const std::uint32_t eip,
                          const std::uint32_t steps) {
        state.eip = eip;
        return ExitResult{kind, eip, steps};
    };

    for (std::size_t i = 0; i < block.insts.size(); ++i)
    {
        const Inst& inst = block.insts[i];
        const auto v = [&](const Value value) { return values[value]; };
        std::uint32_t& out = values[i];
        switch (inst.op)
        {
            case Op::kConst: out = inst.imm; break;
            case Op::kGetReg: out = state.gpr[inst.slot]; break;
            case Op::kGetFlag: out = (state.eflags & inst.imm) != 0 ? 1u : 0u; break;
            case Op::kAdd: out = v(inst.a) + v(inst.b); break;
            case Op::kSub: out = v(inst.a) - v(inst.b); break;
            case Op::kAnd: out = v(inst.a) & v(inst.b); break;
            case Op::kOr: out = v(inst.a) | v(inst.b); break;
            case Op::kXor: out = v(inst.a) ^ v(inst.b); break;
            case Op::kNot: out = ~v(inst.a); break;
            case Op::kMul: out = v(inst.a) * v(inst.b); break;
            case Op::kMulHiS:
                out = static_cast<std::uint32_t>(
                    static_cast<std::uint64_t>(
                        static_cast<std::int64_t>(static_cast<std::int32_t>(v(inst.a))) *
                        static_cast<std::int64_t>(static_cast<std::int32_t>(v(inst.b)))) >>
                    32);
                break;
            case Op::kMulHiU:
                out = static_cast<std::uint32_t>(
                    (static_cast<std::uint64_t>(v(inst.a)) * v(inst.b)) >> 32);
                break;
            case Op::kShl: out = v(inst.a) << (v(inst.b) & 31u); break;
            case Op::kShr: out = v(inst.a) >> (v(inst.b) & 31u); break;
            case Op::kSar:
                out = static_cast<std::uint32_t>(static_cast<std::int32_t>(v(inst.a)) >>
                                                 (v(inst.b) & 31u));
                break;
            case Op::kEq: out = v(inst.a) == v(inst.b) ? 1u : 0u; break;
            case Op::kNe: out = v(inst.a) != v(inst.b) ? 1u : 0u; break;
            case Op::kLtU: out = v(inst.a) < v(inst.b) ? 1u : 0u; break;
            case Op::kLtS:
                out = static_cast<std::int32_t>(v(inst.a)) < static_cast<std::int32_t>(v(inst.b))
                    ? 1u
                    : 0u;
                break;
            case Op::kSelect: out = v(inst.c) != 0 ? v(inst.a) : v(inst.b); break;
            case Op::kParity: out = ParityEven(v(inst.a)) ? 1u : 0u; break;
            case Op::kLoad:
            {
                const SegmentRegister& seg = state.Seg(static_cast<Segment>(inst.slot));
                out = ReadMemory(memory, seg.base + v(inst.a), inst.bytes);
                break;
            }
            case Op::kSetReg: state.gpr[inst.slot] = v(inst.a); break;
            case Op::kSetFlag:
                state.eflags = v(inst.a) != 0 ? (state.eflags | inst.imm) : (state.eflags & ~inst.imm);
                break;
            case Op::kStore:
            {
                const SegmentRegister& seg = state.Seg(static_cast<Segment>(inst.slot));
                WriteMemory(memory, seg.base + v(inst.a), inst.bytes, v(inst.b));
                break;
            }
            case Op::kCheck:
                if (!AccessWouldSucceed(state, memory, static_cast<Segment>(inst.slot), v(inst.a),
                                        inst.bytes, inst.write))
                {
                    return exit(ExitKind::kInterpret, current_eip, current_index);
                }
                break;
            case Op::kBegin:
                current_index = inst.index;
                current_eip = inst.imm;
                break;
            case Op::kExitIf:
                if (v(inst.a) != 0)
                {
                    return exit(inst.kind, inst.imm, inst.index);
                }
                break;
            case Op::kExit:
                return exit(inst.kind, inst.dynamic ? v(inst.a) : inst.imm, inst.index);
        }
    }
    // Every block ends in an exit; reaching here means the frontend did not
    // close it, and the interpreter takes over where the block began.
    return exit(ExitKind::kInterpret, current_eip, current_index);
}

}  // namespace rex86::translate::ir
