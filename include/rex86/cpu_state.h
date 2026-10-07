#ifndef REX86_CPU_STATE_H_
#define REX86_CPU_STATE_H_

#include <array>
#include <cstdint>

// The guest's architectural state, owned by the core. Every field is a plain
// fixed-width value so that a consumer's adapter (rePIU's GuestCpuContext,
// re2DJ's CONTEXT for guest SEH) converts field by field without knowing how
// an engine stores it internally.
//
// Reset values follow the Intel SDM: EFLAGS bit 1 is always set, FNINIT
// leaves the x87 control word at 0x037F and the tag word at 0xFFFF (every
// register empty), and the segment registers start flat (base 0, 4 GiB
// limit, 32-bit default operand size), which is what a 32-bit guest expects
// of every segment except FS and GS. The host supplies the rest through
// Environment::LoadDescriptor when the guest loads a selector.
namespace rex86
{

enum class Gpr : std::uint8_t
{
    kEax = 0,
    kEcx = 1,
    kEdx = 2,
    kEbx = 3,
    kEsp = 4,
    kEbp = 5,
    kEsi = 6,
    kEdi = 7,
};

enum class Segment : std::uint8_t
{
    kEs = 0,
    kCs = 1,
    kSs = 2,
    kDs = 3,
    kFs = 4,
    kGs = 5,
};

inline constexpr std::uint32_t kEflagsReserved1 = 0x00000002u;
inline constexpr std::uint32_t kEflagsCarry = 0x00000001u;
inline constexpr std::uint32_t kEflagsParity = 0x00000004u;
inline constexpr std::uint32_t kEflagsAdjust = 0x00000010u;
inline constexpr std::uint32_t kEflagsZero = 0x00000040u;
inline constexpr std::uint32_t kEflagsSign = 0x00000080u;
inline constexpr std::uint32_t kEflagsTrap = 0x00000100u;
inline constexpr std::uint32_t kEflagsInterrupt = 0x00000200u;
inline constexpr std::uint32_t kEflagsDirection = 0x00000400u;
inline constexpr std::uint32_t kEflagsOverflow = 0x00000800u;

// What the core caches about a segment: the descriptor the host supplied for
// its selector. Address formation adds base and checks limit, with a fast
// path when the segment is flat.
struct SegmentRegister
{
    std::uint16_t selector = 0;
    std::uint32_t base = 0;
    std::uint32_t limit = 0xFFFFFFFFu;
    bool present = true;
    bool executable = false;
    bool writable = true;
    // The D/B bit: 32-bit default operand and address size when true, 16-bit
    // when false.
    bool default_32bit = true;

    [[nodiscard]] bool IsFlat() const
    {
        return base == 0 && limit == 0xFFFFFFFFu;
    }
};

// The x87 state, kept in the 80-bit memory format the guest can observe
// through FSTP m80, FNSAVE and FNSTENV. Registers are indexed by physical
// slot; the stack top lives in status_word bits 11 to 13.
struct X87State
{
    static constexpr std::uint16_t kControlWordReset = 0x037Fu;
    static constexpr std::uint16_t kTagWordAllEmpty = 0xFFFFu;

    std::array<std::array<std::uint8_t, 10>, 8> registers = {};
    std::uint16_t control_word = kControlWordReset;
    std::uint16_t status_word = 0;
    std::uint16_t tag_word = kTagWordAllEmpty;
    std::uint16_t last_opcode = 0;
    std::uint32_t last_instruction_pointer = 0;
    std::uint16_t last_instruction_selector = 0;
    std::uint32_t last_operand_pointer = 0;
    std::uint16_t last_operand_selector = 0;
};

struct CpuState
{
    std::array<std::uint32_t, 8> gpr = {};
    std::uint32_t eip = 0;
    std::uint32_t eflags = kEflagsReserved1;
    std::array<SegmentRegister, 6> segments = {};
    X87State x87;

    [[nodiscard]] std::uint32_t Get(Gpr reg) const
    {
        return gpr[static_cast<std::size_t>(reg)];
    }

    void Set(Gpr reg, std::uint32_t value)
    {
        gpr[static_cast<std::size_t>(reg)] = value;
    }

    [[nodiscard]] const SegmentRegister& Seg(Segment segment) const
    {
        return segments[static_cast<std::size_t>(segment)];
    }

    [[nodiscard]] SegmentRegister& Seg(Segment segment)
    {
        return segments[static_cast<std::size_t>(segment)];
    }

    // Returns the state to its reset values. Code segments are made
    // executable; every other segment starts flat and writable.
    void Reset();
};

}  // namespace rex86

#endif  // REX86_CPU_STATE_H_
