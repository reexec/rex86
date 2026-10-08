#include "tools/bench/asm.h"

#include <cstdio>

namespace rex86::bench
{

namespace
{

std::uint8_t Code(const Reg reg)
{
    return static_cast<std::uint8_t>(reg);
}

bool FitsInt8(const std::int32_t value)
{
    return value >= -128 && value <= 127;
}

bool FitsInt8(const std::uint32_t value)
{
    return FitsInt8(static_cast<std::int32_t>(value));
}

std::uint8_t ScaleField(const std::uint8_t scale)
{
    switch (scale)
    {
        case 1:
            return 0;
        case 2:
            return 1;
        case 4:
            return 2;
        default:
            return 3;
    }
}

}  // namespace

Mem Mem::Abs(const std::uint32_t address)
{
    Mem mem;
    mem.disp = static_cast<std::int32_t>(address);
    return mem;
}

Mem Mem::BaseDisp(const Reg base, const std::int32_t disp)
{
    Mem mem;
    mem.has_base = true;
    mem.base = base;
    mem.disp = disp;
    return mem;
}

Mem Mem::BaseIndex(const Reg base, const Reg index, const std::uint8_t scale,
                   const std::int32_t disp)
{
    Mem mem;
    mem.has_base = true;
    mem.base = base;
    mem.has_index = true;
    mem.index = index;
    mem.scale = scale;
    mem.disp = disp;
    return mem;
}

Mem Mem::IndexDisp(const Reg index, const std::uint8_t scale,
                   const std::uint32_t address)
{
    Mem mem;
    mem.has_index = true;
    mem.index = index;
    mem.scale = scale;
    mem.disp = static_cast<std::int32_t>(address);
    return mem;
}

Assembler::Assembler(const std::uint32_t base) : base_(base)
{
}

std::uint32_t Assembler::Here() const
{
    return base_ + static_cast<std::uint32_t>(bytes_.size());
}

Label Assembler::NewLabel()
{
    Label label;
    label.index = static_cast<std::int32_t>(labels_.size());
    labels_.push_back(-1);
    return label;
}

void Assembler::Bind(const Label label)
{
    labels_[static_cast<std::size_t>(label.index)] =
        static_cast<std::int64_t>(bytes_.size());
}

std::uint32_t Assembler::AddressOf(const Label label) const
{
    const std::int64_t offset = labels_[static_cast<std::size_t>(label.index)];
    return offset < 0 ? 0u : base_ + static_cast<std::uint32_t>(offset);
}

bool Assembler::Finish(std::string* error)
{
    for (const Fixup& fixup : fixups_)
    {
        const std::int64_t target = labels_[static_cast<std::size_t>(fixup.label)];
        if (target < 0)
        {
            char text[64];
            std::snprintf(text, sizeof text, "label %d is unbound", fixup.label);
            *error = text;
            return false;
        }
        // The displacement is relative to the end of the branch.
        const std::int64_t next =
            static_cast<std::int64_t>(fixup.offset) + fixup.width;
        const std::int64_t rel = target - next;
        if (fixup.width == 1)
        {
            if (rel < -128 || rel > 127)
            {
                char text[96];
                std::snprintf(text, sizeof text,
                              "short branch at +%u does not reach (%lld)",
                              fixup.offset, static_cast<long long>(rel));
                *error = text;
                return false;
            }
            bytes_[fixup.offset] = static_cast<std::uint8_t>(rel);
        }
        else
        {
            const std::uint32_t value = static_cast<std::uint32_t>(rel);
            for (unsigned i = 0; i < 4; ++i)
            {
                bytes_[fixup.offset + i] =
                    static_cast<std::uint8_t>(value >> (8 * i));
            }
        }
    }
    fixups_.clear();
    return true;
}

void Assembler::Byte(const std::uint8_t value)
{
    bytes_.push_back(value);
}

void Assembler::Bytes(const std::initializer_list<std::uint8_t> values)
{
    bytes_.insert(bytes_.end(), values.begin(), values.end());
}

void Assembler::Dword(const std::uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
    {
        bytes_.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
    }
}

void Assembler::ModRmReg(const std::uint8_t reg_field, const Reg rm)
{
    Byte(static_cast<std::uint8_t>(0xC0 | (reg_field << 3) | Code(rm)));
}

void Assembler::ModRm(const std::uint8_t reg_field, const Mem& mem)
{
    const std::uint8_t reg_bits = static_cast<std::uint8_t>(reg_field << 3);
    if (!mem.has_base && !mem.has_index)
    {
        // [disp32]: mod 00, rm 101.
        Byte(static_cast<std::uint8_t>(0x00 | reg_bits | 0x05));
        Dword(static_cast<std::uint32_t>(mem.disp));
        return;
    }
    if (!mem.has_base)
    {
        // [index*scale + disp32]: mod 00, rm 100, SIB base 101.
        Byte(static_cast<std::uint8_t>(0x00 | reg_bits | 0x04));
        Byte(static_cast<std::uint8_t>((ScaleField(mem.scale) << 6) |
                                       (Code(mem.index) << 3) | 0x05));
        Dword(static_cast<std::uint32_t>(mem.disp));
        return;
    }
    // A base of EBP has no disp-less form (mod 00 rm 101 is [disp32]), so
    // it takes a disp8 of zero.
    std::uint8_t mod = 0;
    if (mem.disp != 0 || mem.base == Reg::kEbp)
    {
        mod = FitsInt8(mem.disp) ? 0x40 : 0x80;
    }
    const bool needs_sib = mem.has_index || mem.base == Reg::kEsp;
    if (needs_sib)
    {
        Byte(static_cast<std::uint8_t>(mod | reg_bits | 0x04));
        // Without an index the SIB index field is 100 (none).
        const std::uint8_t index_bits =
            mem.has_index ? static_cast<std::uint8_t>(Code(mem.index) << 3)
                          : static_cast<std::uint8_t>(0x20);
        const std::uint8_t scale_bits =
            mem.has_index ? static_cast<std::uint8_t>(ScaleField(mem.scale) << 6)
                          : static_cast<std::uint8_t>(0);
        Byte(static_cast<std::uint8_t>(scale_bits | index_bits | Code(mem.base)));
    }
    else
    {
        Byte(static_cast<std::uint8_t>(mod | reg_bits | Code(mem.base)));
    }
    if (mod == 0x40)
    {
        Byte(static_cast<std::uint8_t>(static_cast<std::int8_t>(mem.disp)));
    }
    else if (mod == 0x80)
    {
        Dword(static_cast<std::uint32_t>(mem.disp));
    }
}

void Assembler::MovImm(const Reg dst, const std::uint32_t imm)
{
    Byte(static_cast<std::uint8_t>(0xB8 + Code(dst)));
    Dword(imm);
}

void Assembler::MovRR(const Reg dst, const Reg src)
{
    Byte(0x89);
    ModRmReg(Code(src), dst);
}

void Assembler::MovLoad(const Reg dst, const Mem& src)
{
    Byte(0x8B);
    ModRm(Code(dst), src);
}

void Assembler::MovStore(const Mem& dst, const Reg src)
{
    Byte(0x89);
    ModRm(Code(src), dst);
}

void Assembler::MovStore8(const Mem& dst, const Reg src)
{
    Byte(0x88);
    ModRm(Code(src), dst);
}

void Assembler::MovzxLoad8(const Reg dst, const Mem& src)
{
    Bytes({0x0F, 0xB6});
    ModRm(Code(dst), src);
}

void Assembler::Lea(const Reg dst, const Mem& src)
{
    Byte(0x8D);
    ModRm(Code(dst), src);
}

void Assembler::Push(const Reg reg)
{
    Byte(static_cast<std::uint8_t>(0x50 + Code(reg)));
}

void Assembler::Pop(const Reg reg)
{
    Byte(static_cast<std::uint8_t>(0x58 + Code(reg)));
}

void Assembler::PushImm(const std::uint32_t imm)
{
    Byte(0x68);
    Dword(imm);
}

void Assembler::AluRR(const Alu op, const Reg dst, const Reg src)
{
    // The r/m32, r32 form: opcode op*8 + 1.
    Byte(static_cast<std::uint8_t>((static_cast<std::uint8_t>(op) << 3) | 0x01));
    ModRmReg(Code(src), dst);
}

void Assembler::AluRI(const Alu op, const Reg dst, const std::uint32_t imm)
{
    if (FitsInt8(imm))
    {
        Byte(0x83);
        ModRmReg(static_cast<std::uint8_t>(op), dst);
        Byte(static_cast<std::uint8_t>(imm));
        return;
    }
    Byte(0x81);
    ModRmReg(static_cast<std::uint8_t>(op), dst);
    Dword(imm);
}

void Assembler::AluRM(const Alu op, const Reg dst, const Mem& src)
{
    // The r32, r/m32 form: opcode op*8 + 3.
    Byte(static_cast<std::uint8_t>((static_cast<std::uint8_t>(op) << 3) | 0x03));
    ModRm(Code(dst), src);
}

void Assembler::AluMR(const Alu op, const Mem& dst, const Reg src)
{
    Byte(static_cast<std::uint8_t>((static_cast<std::uint8_t>(op) << 3) | 0x01));
    ModRm(Code(src), dst);
}

void Assembler::ImulRRI(const Reg dst, const Reg src, const std::uint32_t imm)
{
    if (FitsInt8(imm))
    {
        Byte(0x6B);
        ModRmReg(Code(dst), src);
        Byte(static_cast<std::uint8_t>(imm));
        return;
    }
    Byte(0x69);
    ModRmReg(Code(dst), src);
    Dword(imm);
}

void Assembler::ImulRR(const Reg dst, const Reg src)
{
    Bytes({0x0F, 0xAF});
    ModRmReg(Code(dst), src);
}

void Assembler::ShiftRI(const Shift op, const Reg reg, const std::uint8_t count)
{
    Byte(0xC1);
    ModRmReg(static_cast<std::uint8_t>(op), reg);
    Byte(count);
}

void Assembler::Inc(const Reg reg)
{
    Byte(static_cast<std::uint8_t>(0x40 + Code(reg)));
}

void Assembler::Dec(const Reg reg)
{
    Byte(static_cast<std::uint8_t>(0x48 + Code(reg)));
}

void Assembler::TestRR(const Reg a, const Reg b)
{
    Byte(0x85);
    ModRmReg(Code(b), a);
}

void Assembler::Branch(const std::initializer_list<std::uint8_t> opcode,
                       const Label target, const std::uint8_t width)
{
    Bytes(opcode);
    Fixup fixup;
    fixup.offset = static_cast<std::uint32_t>(bytes_.size());
    fixup.label = target.index;
    fixup.width = width;
    fixups_.push_back(fixup);
    for (unsigned i = 0; i < width; ++i)
    {
        Byte(0);
    }
}

void Assembler::Jmp(const Label target, const BranchWidth width)
{
    if (width == BranchWidth::kShort)
    {
        Branch({0xEB}, target, 1);
    }
    else
    {
        Branch({0xE9}, target, 4);
    }
}

void Assembler::Jcc(const Cond cond, const Label target, const BranchWidth width)
{
    if (width == BranchWidth::kShort)
    {
        Branch({static_cast<std::uint8_t>(0x70 | static_cast<std::uint8_t>(cond))},
               target, 1);
    }
    else
    {
        Branch({0x0F, static_cast<std::uint8_t>(0x80 | static_cast<std::uint8_t>(cond))},
               target, 4);
    }
}

void Assembler::Call(const Label target)
{
    Branch({0xE8}, target, 4);
}

void Assembler::Ret()
{
    Byte(0xC3);
}

void Assembler::RetImm(const std::uint16_t bytes)
{
    Byte(0xC2);
    Byte(static_cast<std::uint8_t>(bytes));
    Byte(static_cast<std::uint8_t>(bytes >> 8));
}

void Assembler::Nop()
{
    Byte(0x90);
}

void Assembler::Cld()
{
    Byte(0xFC);
}

void Assembler::RepMovsd()
{
    Bytes({0xF3, 0xA5});
}

void Assembler::RepStosd()
{
    Bytes({0xF3, 0xAB});
}

void Assembler::FldM64(const Mem& src)
{
    Byte(0xDD);
    ModRm(0, src);
}

void Assembler::FstpM64(const Mem& dst)
{
    Byte(0xDD);
    ModRm(3, dst);
}

void Assembler::FaddM64(const Mem& src)
{
    Byte(0xDC);
    ModRm(0, src);
}

void Assembler::FmulM64(const Mem& src)
{
    Byte(0xDC);
    ModRm(1, src);
}

void Assembler::FaddpSt1St0()
{
    Bytes({0xDE, 0xC1});
}

void Assembler::FxchSt1()
{
    Bytes({0xD9, 0xC9});
}

void Assembler::FistpM32(const Mem& dst)
{
    Byte(0xDB);
    ModRm(3, dst);
}

void Assembler::Fldz()
{
    Bytes({0xD9, 0xEE});
}

void Assembler::Fld1()
{
    Bytes({0xD9, 0xE8});
}

}  // namespace rex86::bench
