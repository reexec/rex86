// The 32-bit integer host-CPU comparison fuzz (design #22, decisions 1 to
// 4): one random integer instruction runs on the host CPU under the trap
// flag and on the core, from the same registers, flags and memory at the
// same guest addresses, and the outcomes are compared.
//
// Host side: an i386 process maps 0x10000-0x15FFF (code page, 8 KiB work
// area, 8 KiB PROT_NONE guard, a stub page outside the trace) and runs
//
//   0x15000: mov eax..edi (not esp), imm32; mov esp, guest_esp - 4;
//            jmp 0x10028
//   0x10028: popfd
//   0x10029: <instruction>; int3
//
// where the POPFD sets TF, so a single-step trap (or the instruction's own
// fault) arrives right after the instruction. The signal handler runs on
// an alternate stack, records the context and longjmps back.
//
// Core side: the host's input and outcome become one trace case
// (src/trace/), and trace::Replay runs it on the core, so the fuzz and the
// committed corpus compare through the same code.
//
// Usage: rex86_int_fuzz [iterations] [seed] [--verbose] [--stats]
//                       [--only MNEMONIC] [--record FILE] [--failures FILE]
// --record writes every case (the corpus), --failures the mismatches only;
// rex86_trace --dump N FILE spells a recorded case out.

#if !defined(__linux__) || !defined(__i386__)
#error "the integer host comparison runs in i386 Linux processes only"
#endif

#include <setjmp.h>
#include <signal.h>
#include <sys/mman.h>
#include <ucontext.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "decode/decoder.h"
#include "rex86/environment.h"
#include "rex86/guest_memory.h"
#include "trace/replay.h"
#include "trace/trace_format.h"

namespace
{

namespace trace = rex86::trace;

constexpr std::uint32_t kBase = 0x10000;
constexpr std::uint32_t kCodeSize = 0x1000;
constexpr std::uint32_t kWorkBase = 0x11000;
constexpr std::uint32_t kWorkSize = 0x2000;
constexpr std::uint32_t kGuardBase = 0x13000;
constexpr std::uint32_t kGuardSize = 0x2000;
constexpr std::uint32_t kRegionSize = kCodeSize + kWorkSize;
// The register loads live on their own page, past the guard, so a trace
// carries only the POPFD and the instruction (design #22, decision 5).
constexpr std::uint32_t kStubPage = kGuardBase + kGuardSize;
constexpr std::uint32_t kMappedEnd = kStubPage + 0x1000;
constexpr std::uint32_t kInstruction = kBase + 41;
// Memory operands must start below this: everything under it is the
// region, its guard or the never-mapped low 64 KiB, so a stray access
// faults on the host exactly as it does on the core.
constexpr std::uint32_t kAccessLimit = kGuardBase + kGuardSize / 2;

constexpr std::uint32_t kEflagsCarry = 1u << 0;
constexpr std::uint32_t kEflagsArithmetic = 0x8D5u;  // OF SF ZF AF PF CF
constexpr std::uint32_t kEflagsTrap = 1u << 8;
constexpr std::uint32_t kEflagsInterrupt = 1u << 9;
constexpr std::uint32_t kEflagsDirection = 1u << 10;
constexpr std::uint32_t kEflagsOverflow = 1u << 11;
constexpr std::uint32_t kEflagsIopl = 3u << 12;
constexpr std::uint32_t kEflagsResume = 1u << 16;
constexpr std::uint32_t kEflagsId = 1u << 21;

// --- the instruction table ---------------------------------------------

struct Form
{
    std::vector<std::uint8_t> opcode;
    int reg = -1;  // modrm.reg, or -1 when the opcode has no ModRM
    std::string mnemonic;
};

bool AllowedIsa(const ZydisISASet isa)
{
    switch (isa)
    {
        case ZYDIS_ISA_SET_I86: case ZYDIS_ISA_SET_I186:
        case ZYDIS_ISA_SET_I386: case ZYDIS_ISA_SET_I486:
        case ZYDIS_ISA_SET_I486REAL: case ZYDIS_ISA_SET_PENTIUMREAL:
        case ZYDIS_ISA_SET_PPRO: case ZYDIS_ISA_SET_CMOV:
        case ZYDIS_ISA_SET_LAHF: case ZYDIS_ISA_SET_FAT_NOP:
        case ZYDIS_ISA_SET_PAUSE:
            return true;
        default:
            return false;
    }
}

// Bound to the environment or to privilege (design #22, decision 3).
bool ExcludedMnemonic(const ZydisMnemonic mnemonic)
{
    switch (mnemonic)
    {
        case ZYDIS_MNEMONIC_CLI: case ZYDIS_MNEMONIC_STI: case ZYDIS_MNEMONIC_HLT:
        case ZYDIS_MNEMONIC_IN: case ZYDIS_MNEMONIC_OUT:
        case ZYDIS_MNEMONIC_INSB: case ZYDIS_MNEMONIC_INSW: case ZYDIS_MNEMONIC_INSD:
        case ZYDIS_MNEMONIC_OUTSB: case ZYDIS_MNEMONIC_OUTSW: case ZYDIS_MNEMONIC_OUTSD:
        case ZYDIS_MNEMONIC_INT: case ZYDIS_MNEMONIC_INT1: case ZYDIS_MNEMONIC_INT3:
        case ZYDIS_MNEMONIC_INTO: case ZYDIS_MNEMONIC_IRET: case ZYDIS_MNEMONIC_IRETD:
        case ZYDIS_MNEMONIC_LDS: case ZYDIS_MNEMONIC_LES: case ZYDIS_MNEMONIC_LFS:
        case ZYDIS_MNEMONIC_LGS: case ZYDIS_MNEMONIC_LSS:
        case ZYDIS_MNEMONIC_ARPL: case ZYDIS_MNEMONIC_LAR: case ZYDIS_MNEMONIC_LSL:
        case ZYDIS_MNEMONIC_VERR: case ZYDIS_MNEMONIC_VERW:
        case ZYDIS_MNEMONIC_SLDT: case ZYDIS_MNEMONIC_STR: case ZYDIS_MNEMONIC_LLDT:
        case ZYDIS_MNEMONIC_LTR: case ZYDIS_MNEMONIC_SGDT: case ZYDIS_MNEMONIC_SIDT:
        case ZYDIS_MNEMONIC_LGDT: case ZYDIS_MNEMONIC_LIDT: case ZYDIS_MNEMONIC_SMSW:
        case ZYDIS_MNEMONIC_LMSW: case ZYDIS_MNEMONIC_CLTS:
        case ZYDIS_MNEMONIC_CPUID: case ZYDIS_MNEMONIC_RDTSC: case ZYDIS_MNEMONIC_RDMSR:
        case ZYDIS_MNEMONIC_WRMSR: case ZYDIS_MNEMONIC_RDPMC:
        case ZYDIS_MNEMONIC_INVD: case ZYDIS_MNEMONIC_WBINVD: case ZYDIS_MNEMONIC_INVLPG:
        case ZYDIS_MNEMONIC_RSM: case ZYDIS_MNEMONIC_SYSENTER: case ZYDIS_MNEMONIC_SYSEXIT:
            return true;
        default:
            return false;
    }
}

bool Allowed(const rex86::decode::DecodedInstruction& d)
{
    const ZydisDecodedInstruction& in = d.instruction;
    if (!AllowedIsa(in.meta.isa_set) || ExcludedMnemonic(in.mnemonic) ||
        in.meta.branch_type == ZYDIS_BRANCH_TYPE_FAR)
    {
        return false;
    }
    for (ZyanU8 i = 0; i < in.operand_count; ++i)
    {
        const ZydisDecodedOperand& op = d.operands[i];
        if (op.type != ZYDIS_OPERAND_TYPE_REGISTER)
        {
            continue;
        }
        const ZydisRegisterClass cls = ZydisRegisterGetClass(op.reg.value);
        // Segment-register loads (MOV sreg, POP sreg) and control or
        // debug registers.
        if ((cls == ZYDIS_REGCLASS_SEGMENT && (op.actions & ZYDIS_OPERAND_ACTION_MASK_WRITE) != 0) ||
            cls == ZYDIS_REGCLASS_CONTROL || cls == ZYDIS_REGCLASS_DEBUG ||
            cls == ZYDIS_REGCLASS_TEST)
        {
            return false;
        }
    }
    return true;
}

bool IsPrefix(const unsigned byte)
{
    switch (byte)
    {
        case 0x26: case 0x2E: case 0x36: case 0x3E: case 0x64: case 0x65:
        case 0x66: case 0x67: case 0xF0: case 0xF2: case 0xF3:
            return true;
        default:
            return false;
    }
}

std::vector<Form> BuildForms(const rex86::decode::Decoder& decoder)
{
    std::vector<Form> forms;
    const auto add = [&](const std::vector<std::uint8_t>& opcode) {
        std::map<std::pair<int, std::string>, bool> seen;
        for (int reg = 0; reg < 8; ++reg)
        {
            for (const unsigned mod : {0u, 3u})
            {
                std::uint8_t bytes[15] = {};
                std::memcpy(bytes, opcode.data(), opcode.size());
                bytes[opcode.size()] = static_cast<std::uint8_t>((mod << 6) | (static_cast<unsigned>(reg) << 3));
                rex86::decode::DecodedInstruction d;
                if (!decoder.Decode(bytes, sizeof bytes, kInstruction, &d) || !Allowed(d))
                {
                    continue;
                }
                const bool has_modrm = (d.instruction.attributes & ZYDIS_ATTRIB_HAS_MODRM) != 0;
                Form form;
                form.opcode = opcode;
                form.reg = has_modrm ? reg : -1;
                // UD0 and UD1 are one invalid opcode whatever the ModRM.
                const ZydisMnemonic m = d.instruction.mnemonic;
                if ((m == ZYDIS_MNEMONIC_UD0 || m == ZYDIS_MNEMONIC_UD1) && reg != 0)
                {
                    continue;
                }
                form.mnemonic = d.MnemonicName();
                if (!seen.emplace(std::make_pair(form.reg, form.mnemonic), true).second)
                {
                    continue;
                }
                forms.push_back(form);
            }
        }
    };
    for (unsigned op = 0; op < 0x100; ++op)
    {
        if (IsPrefix(op) || op == 0x0F)
        {
            continue;
        }
        add({static_cast<std::uint8_t>(op)});
    }
    for (unsigned op = 0; op < 0x100; ++op)
    {
        add({0x0F, static_cast<std::uint8_t>(op)});
    }
    return forms;
}

// --- random inputs -------------------------------------------------------

class Random
{
public:
    explicit Random(const std::uint64_t seed) : rng_(seed)
    {
    }

    std::uint32_t Bits(const unsigned count)
    {
        const auto value = static_cast<std::uint32_t>(rng_());
        return count >= 32 ? value : value & ((1u << count) - 1u);
    }

    std::uint32_t Below(const std::uint32_t limit)
    {
        return static_cast<std::uint32_t>(rng_() % limit);
    }

    bool Chance(const unsigned percent)
    {
        return Below(100) < percent;
    }

    std::uint32_t Value()
    {
        static const std::uint32_t kEdges[] = {
            0u, 1u, 2u, 0x7Fu, 0x80u, 0xFFu, 0x100u, 0x7FFFu, 0x8000u, 0xFFFFu,
            0x10000u, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu, 0xFFFFFFFEu, 0xFFFFFF80u};
        switch (Below(10))
        {
            case 0: case 1: case 2:
                return kWorkBase + Below(kWorkSize);
            case 3: case 4:
                return Below(0x40) - 0x20u;
            case 5:
                return kEdges[Below(sizeof kEdges / sizeof kEdges[0])];
            default:
                return Bits(32);
        }
    }

private:
    std::mt19937_64 rng_;
};

struct Input
{
    std::vector<std::uint8_t> code;  // the instruction alone
    std::array<std::uint32_t, 8> gpr = {};
    std::uint32_t eflags = 0;
    std::uint32_t fill_seed = 0;
    rex86::decode::DecodedInstruction decoded;
};

// The value of a register by Zydis id, from a register file.
std::uint32_t RegisterValue(const std::array<std::uint32_t, 8>& gpr, const ZydisRegister reg)
{
    if (reg >= ZYDIS_REGISTER_EAX && reg <= ZYDIS_REGISTER_EDI)
    {
        return gpr[reg - ZYDIS_REGISTER_EAX];
    }
    if (reg >= ZYDIS_REGISTER_AX && reg <= ZYDIS_REGISTER_DI)
    {
        return gpr[reg - ZYDIS_REGISTER_AX] & 0xFFFFu;
    }
    if (reg >= ZYDIS_REGISTER_AL && reg <= ZYDIS_REGISTER_BL)
    {
        return gpr[reg - ZYDIS_REGISTER_AL] & 0xFFu;
    }
    if (reg >= ZYDIS_REGISTER_AH && reg <= ZYDIS_REGISTER_BH)
    {
        return (gpr[reg - ZYDIS_REGISTER_AH] >> 8) & 0xFFu;
    }
    return 0;
}

// The 32-bit slot of a register id, or -1.
int Slot(const ZydisRegister reg)
{
    if (reg >= ZYDIS_REGISTER_EAX && reg <= ZYDIS_REGISTER_EDI)
    {
        return reg - ZYDIS_REGISTER_EAX;
    }
    if (reg >= ZYDIS_REGISTER_AX && reg <= ZYDIS_REGISTER_DI)
    {
        return reg - ZYDIS_REGISTER_AX;
    }
    if (reg >= ZYDIS_REGISTER_AL && reg <= ZYDIS_REGISTER_BL)
    {
        return reg - ZYDIS_REGISTER_AL;
    }
    if (reg >= ZYDIS_REGISTER_AH && reg <= ZYDIS_REGISTER_BH)
    {
        return reg - ZYDIS_REGISTER_AH;
    }
    return -1;
}

bool IsBitTest(const ZydisMnemonic m)
{
    return m == ZYDIS_MNEMONIC_BT || m == ZYDIS_MNEMONIC_BTS ||
           m == ZYDIS_MNEMONIC_BTR || m == ZYDIS_MNEMONIC_BTC;
}

bool IsMemory(const ZydisDecodedOperand& op)
{
    return op.type == ZYDIS_OPERAND_TYPE_MEMORY && op.mem.type != ZYDIS_MEMOP_TYPE_AGEN;
}

// The effective address of a memory operand, wrapped at the address width.
std::uint32_t Address(const Input& input, const ZydisDecodedOperand& op)
{
    std::uint32_t address = static_cast<std::uint32_t>(op.mem.disp.value);
    if (op.mem.base != ZYDIS_REGISTER_NONE)
    {
        address += RegisterValue(input.gpr, op.mem.base);
    }
    if (op.mem.index != ZYDIS_REGISTER_NONE)
    {
        address += RegisterValue(input.gpr, op.mem.index) * op.mem.scale;
    }
    return input.decoded.instruction.address_width == 16 ? address & 0xFFFFu : address;
}

// The byte offset a BT-family register bit offset adds to a memory operand.
std::int32_t BitTestOffset(const Input& input)
{
    const auto& d = input.decoded;
    if (!IsBitTest(d.instruction.mnemonic) || !IsMemory(d.operands[0]) ||
        d.operands[1].type != ZYDIS_OPERAND_TYPE_REGISTER)
    {
        return 0;
    }
    const std::uint32_t offset = RegisterValue(input.gpr, d.operands[1].reg.value);
    if (d.instruction.operand_width == 16)
    {
        return (static_cast<std::int16_t>(offset) >> 4) * 2;
    }
    return (static_cast<std::int32_t>(offset) >> 5) * 4;
}

class Generator
{
public:
    Generator(const std::uint64_t seed, const rex86::decode::Decoder& decoder,
              const std::vector<Form>& forms)
        : random_(seed), decoder_(decoder), forms_(forms)
    {
    }

    Random& random()
    {
        return random_;
    }

    std::uint64_t rejected() const
    {
        return rejected_;
    }

    void Next(Input* input)
    {
        while (!Try(input))
        {
            ++rejected_;
        }
    }

private:
    bool Decode(Input* input)
    {
        std::uint8_t bytes[15] = {};
        std::memcpy(bytes, input->code.data(), std::min<std::size_t>(input->code.size(), 15));
        return decoder_.Decode(bytes, std::min<std::size_t>(input->code.size(), 15), kInstruction,
                               &input->decoded) &&
               Allowed(input->decoded);
    }

    bool Try(Input* input)
    {
        const Form& form = forms_[random_.Below(static_cast<std::uint32_t>(forms_.size()))];
        std::vector<std::uint8_t> prefixes;
        if (random_.Chance(25)) prefixes.push_back(0x66);
        if (random_.Chance(8)) prefixes.push_back(0x67);
        if (random_.Chance(15))
        {
            static const std::uint8_t kSegments[] = {0x26, 0x2E, 0x36, 0x3E, 0x64};
            prefixes.push_back(kSegments[random_.Below(5)]);
        }
        if (random_.Chance(12)) prefixes.push_back(random_.Chance(70) ? 0xF3 : 0xF2);
        if (random_.Chance(6)) prefixes.push_back(0xF0);
        for (std::size_t i = prefixes.size(); i > 1; --i)
        {
            std::swap(prefixes[i - 1], prefixes[random_.Below(static_cast<std::uint32_t>(i))]);
        }
        input->code = prefixes;
        input->code.insert(input->code.end(), form.opcode.begin(), form.opcode.end());
        if (form.reg >= 0)
        {
            std::uint8_t modrm = static_cast<std::uint8_t>(random_.Bits(8));
            if (random_.Chance(30)) modrm |= 0xC0u;
            input->code.push_back(static_cast<std::uint8_t>((modrm & 0xC7u) | (form.reg << 3)));
        }
        while (input->code.size() < 15)
        {
            input->code.push_back(static_cast<std::uint8_t>(random_.Bits(8)));
        }
        if (!Decode(input))
        {
            return false;
        }
        input->code.resize(input->decoded.Length());

        for (std::uint32_t& value : input->gpr)
        {
            value = random_.Value();
        }
        input->gpr[4] = kWorkBase + kWorkSize / 2 + random_.Below(0x400) - 0x200u;
        if (random_.Chance(80)) input->gpr[4] &= ~3u;
        const ZydisDecodedInstruction& in = input->decoded.instruction;
        if (in.mnemonic == ZYDIS_MNEMONIC_ENTER || in.mnemonic == ZYDIS_MNEMONIC_LEAVE)
        {
            input->gpr[5] = kWorkBase + 0x400 + random_.Below(kWorkSize - 0x800);
        }
        if (in.mnemonic == ZYDIS_MNEMONIC_ENTER && random_.Chance(80))
        {
            // Mostly a frame that fits the work area; the rest probe the
            // final stack pointer's fault.
            const std::uint32_t size = random_.Below(0x200);
            const unsigned at = in.raw.imm[0].offset;
            input->code[at] = static_cast<std::uint8_t>(size);
            input->code[at + 1] = static_cast<std::uint8_t>(size >> 8);
            if (!Decode(input))
            {
                return false;
            }
        }
        if ((in.attributes & (ZYDIS_ATTRIB_HAS_REP | ZYDIS_ATTRIB_HAS_REPE |
                              ZYDIS_ATTRIB_HAS_REPNE)) != 0)
        {
            const std::uint32_t count = random_.Below(9);
            input->gpr[1] = in.address_width == 16 ? (input->gpr[1] & 0xFFFF0000u) | count : count;
        }
        if (IsBitTest(in.mnemonic) && IsMemory(input->decoded.operands[0]) &&
            input->decoded.operands[1].type == ZYDIS_OPERAND_TYPE_REGISTER)
        {
            // A small bit offset keeps the access near the operand; ESP
            // stays the stack.
            const int slot = Slot(input->decoded.operands[1].reg.value);
            if (slot != 4)
            {
                input->gpr[slot] = random_.Below(0x400) - 0x200u;
            }
        }
        input->eflags = 0x2u | kEflagsTrap | kEflagsInterrupt |
                        (random_.Bits(12) & (kEflagsArithmetic | kEflagsDirection)) |
                        (random_.Chance(50) ? kEflagsId : 0u);
        input->fill_seed = random_.Bits(32);
        return FixAddresses(input);
    }

    // Moves every memory operand into the work area (design #22, decision
    // 3), then rejects the case if any still starts past kAccessLimit.
    bool FixAddresses(Input* input)
    {
        for (int pass = 0; pass < 4; ++pass)
        {
            bool changed = false;
            const auto& d = input->decoded;
            for (ZyanU8 i = 0; i < d.instruction.operand_count; ++i)
            {
                const ZydisDecodedOperand& op = d.operands[i];
                if (!IsMemory(op))
                {
                    continue;
                }
                const std::uint32_t address = Address(*input, op);
                if (address >= kWorkBase && address < kGuardBase)
                {
                    continue;
                }
                if (address < kAccessLimit && random_.Chance(5))
                {
                    continue;  // left to fault, or to touch the code page
                }
                std::uint32_t target = kWorkBase + 0x100 + random_.Below(kWorkSize - 0x200);
                if (random_.Chance(4))
                {
                    target = kGuardBase - random_.Below(16);  // straddle the guard
                }
                if (d.instruction.address_width == 16)
                {
                    break;  // a 16-bit address never reaches the region: it faults
                }
                const std::uint32_t rest = address -
                    (op.mem.base != ZYDIS_REGISTER_NONE ? RegisterValue(input->gpr, op.mem.base) : 0u);
                const int base = Slot(op.mem.base);
                const int index = Slot(op.mem.index);
                if (base >= 0 && base != 4)
                {
                    input->gpr[base] = target - rest;
                    changed = true;
                }
                else if (index >= 0 && index != 4)
                {
                    const std::uint32_t without = address - RegisterValue(input->gpr, op.mem.index) * op.mem.scale;
                    std::uint32_t wanted = target - without;
                    wanted &= ~(static_cast<std::uint32_t>(op.mem.scale) - 1u);
                    input->gpr[index] = wanted / op.mem.scale;
                    changed = true;
                }
                else if (op.mem.base == ZYDIS_REGISTER_NONE && op.mem.index == ZYDIS_REGISTER_NONE &&
                         d.instruction.raw.disp.size == 32)
                {
                    const unsigned at = d.instruction.raw.disp.offset;
                    for (unsigned b = 0; b < 4; ++b)
                    {
                        input->code[at + b] = static_cast<std::uint8_t>(target >> (8 * b));
                    }
                    if (!Decode(input))
                    {
                        return false;
                    }
                    changed = true;
                    break;  // the operands were decoded anew
                }
            }
            if (!changed)
            {
                break;
            }
        }
        const auto& d = input->decoded;
        for (ZyanU8 i = 0; i < d.instruction.operand_count; ++i)
        {
            const ZydisDecodedOperand& op = d.operands[i];
            if (!IsMemory(op))
            {
                continue;
            }
            const std::uint32_t address =
                Address(*input, op) + static_cast<std::uint32_t>(i == 0 ? BitTestOffset(*input) : 0);
            if (address >= kAccessLimit)
            {
                return false;
            }
        }
        return true;
    }

    Random random_;
    const rex86::decode::Decoder& decoder_;
    const std::vector<Form>& forms_;
    std::uint64_t rejected_ = 0;
};

// --- the host side -------------------------------------------------------

struct HostOutcome
{
    int signal = 0;
    std::uint32_t trapno = 0;
    std::array<std::uint32_t, 8> gpr = {};
    std::uint32_t eip = 0;
    std::uint32_t eflags = 0;
    std::uint32_t cr2 = 0;
};

sigjmp_buf g_jump;
volatile sig_atomic_t g_armed = 0;
volatile sig_atomic_t g_rep_string = 0;
HostOutcome g_outcome;

void Handler(const int signal, siginfo_t*, void* context)
{
    auto* uc = static_cast<ucontext_t*>(context);
    const greg_t* g = uc->uc_mcontext.gregs;
    if (g_armed == 0)
    {
        // A fault in the fuzz itself: say where, then die with the default
        // action when the handler returns and the instruction re-executes.
        char text[128];
        const int length = std::snprintf(text, sizeof text,
                                         "signal %d trapno %d at eip %08X outside the guest\n", signal,
                                         static_cast<int>(g[REG_TRAPNO]),
                                         static_cast<unsigned>(g[REG_EIP]));
        if (write(2, text, static_cast<std::size_t>(length)) < 0) {}
        struct sigaction action = {};
        action.sa_handler = SIG_DFL;
        sigaction(signal, &action, nullptr);
        return;
    }
    // A REP string traps after every iteration; let it run to the end.
    if (signal == SIGTRAP && g[REG_TRAPNO] == 1 && g_rep_string != 0 &&
        static_cast<std::uint32_t>(g[REG_EIP]) == kInstruction)
    {
        return;
    }
    g_outcome.signal = signal;
    g_outcome.trapno = static_cast<std::uint32_t>(g[REG_TRAPNO]);
    static const int kSlots[8] = {REG_EAX, REG_ECX, REG_EDX, REG_EBX,
                                  REG_ESP, REG_EBP, REG_ESI, REG_EDI};
    for (int i = 0; i < 8; ++i)
    {
        g_outcome.gpr[i] = static_cast<std::uint32_t>(g[kSlots[i]]);
    }
    g_outcome.eip = static_cast<std::uint32_t>(g[REG_EIP]);
    g_outcome.eflags = static_cast<std::uint32_t>(g[REG_EFL]);
    g_outcome.cr2 = static_cast<std::uint32_t>(uc->uc_mcontext.cr2);
    siglongjmp(g_jump, 1);
}

class Host
{
public:
    Host()
    {
        void* p = mmap(reinterpret_cast<void*>(kBase), kMappedEnd - kBase, PROT_NONE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        if (p != reinterpret_cast<void*>(kBase))
        {
            std::perror("mmap 0x10000");
            std::exit(2);
        }
        region_ = static_cast<std::uint8_t*>(p);
        if (mprotect(region_, kCodeSize, PROT_READ | PROT_WRITE | PROT_EXEC) != 0 ||
            mprotect(region_ + kCodeSize, kWorkSize, PROT_READ | PROT_WRITE) != 0 ||
            mprotect(region_ + (kStubPage - kBase), 0x1000, PROT_READ | PROT_WRITE | PROT_EXEC) != 0)
        {
            std::perror("mprotect");
            std::exit(2);
        }
        CheckLowMemoryUnmapped();

        stack_t alternate = {};
        alternate.ss_size = 1u << 16;
        alternate.ss_sp = std::malloc(alternate.ss_size);
        sigaltstack(&alternate, nullptr);
        struct sigaction action = {};
        action.sa_sigaction = Handler;
        action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_NODEFER;
        sigemptyset(&action.sa_mask);
        for (const int signal : {SIGTRAP, SIGSEGV, SIGBUS, SIGFPE, SIGILL})
        {
            sigaction(signal, &action, nullptr);
        }

        std::uint16_t s[6] = {};
        asm volatile("mov %%es, %0" : "=r"(s[0]));
        asm volatile("mov %%cs, %0" : "=r"(s[1]));
        asm volatile("mov %%ss, %0" : "=r"(s[2]));
        asm volatile("mov %%ds, %0" : "=r"(s[3]));
        asm volatile("mov %%fs, %0" : "=r"(s[4]));
        asm volatile("mov %%gs, %0" : "=r"(s[5]));
        for (int i = 0; i < 6; ++i)
        {
            selectors_[i] = s[i];
        }
    }

    const std::array<std::uint16_t, 6>& selectors() const
    {
        return selectors_;
    }

    std::uint8_t* region()
    {
        return region_;
    }

    // Lays out the region for input; the bytes written over the fill are
    // returned as trace patches.
    std::vector<trace::Bytes> Prepare(const Input& input)
    {
        trace::FillRegion(input.fill_seed, kBase, region_, kCodeSize);
        trace::FillRegion(input.fill_seed, kWorkBase, region_ + kCodeSize, kWorkSize);
        std::vector<std::uint8_t> stub;
        const auto emit32 = [&](const std::uint32_t v) {
            for (int i = 0; i < 4; ++i) stub.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
        };
        for (int r = 0; r < 8; ++r)
        {
            if (r == 4) continue;
            stub.push_back(static_cast<std::uint8_t>(0xB8 + r));
            emit32(input.gpr[r]);
        }
        stub.push_back(0xBC);
        emit32(input.gpr[4] - 4u);
        stub.push_back(0xE9);  // jmp rel32 to the POPFD
        emit32(kInstruction - 1u - (kStubPage + static_cast<std::uint32_t>(stub.size()) + 4u));
        std::memcpy(region_ + (kStubPage - kBase), stub.data(), stub.size());
        std::vector<std::uint8_t> code = {0x9D};
        code.insert(code.end(), input.code.begin(), input.code.end());
        code.push_back(0xCC);
        std::memcpy(region_ + (kInstruction - 1u - kBase), code.data(), code.size());
        std::uint8_t flags[4];
        for (int i = 0; i < 4; ++i) flags[i] = static_cast<std::uint8_t>(input.eflags >> (8 * i));
        std::memcpy(region_ + (input.gpr[4] - 4u - kBase), flags, 4);
        return {trace::Bytes{kInstruction - 1u, code},
                trace::Bytes{input.gpr[4] - 4u, std::vector<std::uint8_t>(flags, flags + 4)}};
    }

    HostOutcome Run(const bool rep_string)
    {
        g_rep_string = rep_string ? 1 : 0;
        g_outcome = HostOutcome{};
        g_armed = 1;
        if (sigsetjmp(g_jump, 1) == 0)
        {
            Enter();
        }
        g_armed = 0;
        // The signal frame cleared TF and DF; AC and NT may still be what
        // the guest left. Clear them all before compiled code runs again.
        asm volatile("pushfl\n\tandl $0xFFFBBAFF, (%%esp)\n\tpopfl" ::: "cc", "memory");
        return g_outcome;
    }

private:
    [[noreturn]] static void Enter()
    {
        asm volatile("jmp *%0" : : "r"(kStubPage) : "memory");
        __builtin_unreachable();
    }

    static void CheckLowMemoryUnmapped()
    {
        std::FILE* maps = std::fopen("/proc/self/maps", "r");
        if (maps == nullptr)
        {
            return;
        }
        char line[512];
        while (std::fgets(line, sizeof line, maps) != nullptr)
        {
            unsigned long start = 0;
            unsigned long end = 0;
            if (std::sscanf(line, "%lx-%lx", &start, &end) == 2 && start < kMappedEnd &&
                (start < kBase || end > kMappedEnd))
            {
                std::fprintf(stderr, "something else is mapped below 0x%X: %s",
                             kMappedEnd, line);
                std::exit(2);
            }
        }
        std::fclose(maps);
    }

    std::uint8_t* region_ = nullptr;
    std::array<std::uint16_t, 6> selectors_ = {};
};

// --- the trace case ------------------------------------------------------

bool IsShiftOrRotate(const ZydisMnemonic m)
{
    switch (m)
    {
        case ZYDIS_MNEMONIC_SHL: case ZYDIS_MNEMONIC_SHR:
        case ZYDIS_MNEMONIC_SAR: case ZYDIS_MNEMONIC_ROL: case ZYDIS_MNEMONIC_ROR:
        case ZYDIS_MNEMONIC_RCL: case ZYDIS_MNEMONIC_RCR:
            return true;
        default:
            return false;
    }
}

std::uint32_t OperandValue(const Input& input, const ZydisDecodedOperand& op)
{
    if (op.type == ZYDIS_OPERAND_TYPE_REGISTER)
    {
        return RegisterValue(input.gpr, op.reg.value);
    }
    if (op.type == ZYDIS_OPERAND_TYPE_IMMEDIATE)
    {
        return static_cast<std::uint32_t>(op.imm.value.u);
    }
    return 0;
}

// Marks the destination of an instruction as undefined: its register, or
// its memory bytes.
void MaskDestination(const Input& input, trace::Case* c)
{
    const ZydisDecodedOperand& op = input.decoded.operands[0];
    if (op.type == ZYDIS_OPERAND_TYPE_REGISTER)
    {
        const int slot = Slot(op.reg.value);
        if (slot >= 0) c->gpr_mask = static_cast<std::uint8_t>(c->gpr_mask & ~(1u << slot));
    }
    else if (IsMemory(op))
    {
        c->ignores.push_back({Address(input, op), static_cast<std::uint16_t>(op.size / 8)});
    }
}

// What the SDM leaves undefined (design #22, decision 4), for a retired
// instruction.
void MaskUndefined(const Input& input, const HostOutcome& host, trace::Case* c)
{
    const ZydisDecodedInstruction& in = input.decoded.instruction;
    const ZydisDecodedOperand* ops = input.decoded.operands;
    std::uint32_t undefined = in.cpu_flags != nullptr ? in.cpu_flags->undefined : 0u;
    const unsigned width = in.operand_width;
    if (IsShiftOrRotate(in.mnemonic))
    {
        const std::uint32_t count = OperandValue(input, ops[1]) & 0x1Fu;
        if (count == 0)
        {
            undefined = 0;  // nothing changes
        }
        else
        {
            if (count != 1) undefined |= kEflagsOverflow;
            const bool shift = in.mnemonic == ZYDIS_MNEMONIC_SHL ||
                               in.mnemonic == ZYDIS_MNEMONIC_SHR || in.mnemonic == ZYDIS_MNEMONIC_SAR;
            if (shift && count >= ops[0].size) undefined |= kEflagsCarry;
        }
    }
    if (in.mnemonic == ZYDIS_MNEMONIC_SHLD || in.mnemonic == ZYDIS_MNEMONIC_SHRD)
    {
        const std::uint32_t count = OperandValue(input, ops[2]) & 0x1Fu;
        if (count == 0)
        {
            undefined = 0;
        }
        else if (count > width)
        {
            undefined |= kEflagsArithmetic;
            MaskDestination(input, c);
        }
        else if (count != 1)
        {
            undefined |= kEflagsOverflow;
        }
    }
    if ((in.mnemonic == ZYDIS_MNEMONIC_BSF || in.mnemonic == ZYDIS_MNEMONIC_BSR) &&
        (host.eflags & (1u << 6)) != 0)
    {
        MaskDestination(input, c);  // zero source
    }
    if (in.mnemonic == ZYDIS_MNEMONIC_BSWAP && width == 16)
    {
        MaskDestination(input, c);
    }
    if (in.mnemonic == ZYDIS_MNEMONIC_PUSH && ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER &&
        ZydisRegisterGetClass(ops[0].reg.value) == ZYDIS_REGCLASS_SEGMENT && width == 32)
    {
        c->ignores.push_back({host.gpr[4] + 2u, 2});
    }
    if (in.mnemonic == ZYDIS_MNEMONIC_POPF || in.mnemonic == ZYDIS_MNEMONIC_POPFD)
    {
        undefined |= kEflagsInterrupt | kEflagsIopl;  // CPL 3 cannot change them
    }
    c->eflags_mask &= ~undefined;
}

rex86::FaultKind FaultOf(const HostOutcome& host)
{
    switch (host.trapno)
    {
        case 0: return rex86::FaultKind::kDivide;
        case 4: return rex86::FaultKind::kOverflow;
        case 5: return rex86::FaultKind::kBound;
        case 6: return rex86::FaultKind::kIllegalInstruction;
        case 12: return rex86::FaultKind::kStackFault;
        case 13: return rex86::FaultKind::kGeneralProtection;
        case 14: return rex86::FaultKind::kAccessViolation;
        default: return rex86::FaultKind::kOther;
    }
}

std::vector<trace::Bytes> Differences(const std::uint8_t* before, const std::uint8_t* after,
                                      const std::uint32_t base, const std::uint32_t size)
{
    std::vector<trace::Bytes> diffs;
    std::uint32_t i = 0;
    while (i < size)
    {
        if (before[i] == after[i])
        {
            ++i;
            continue;
        }
        std::uint32_t end = i + 1;
        // Short equal gaps join one run.
        while (end < size)
        {
            std::uint32_t next = end;
            while (next < size && next - end < 4 && before[next] == after[next]) ++next;
            if (next >= size || before[next] == after[next]) break;
            end = next + 1;
        }
        diffs.push_back({base + i, std::vector<std::uint8_t>(after + i, after + end)});
        i = end;
    }
    return diffs;
}

trace::Case BuildCase(const Input& input, const std::array<std::uint16_t, 6>& selectors,
                      std::vector<trace::Bytes> patches, const std::uint8_t* before,
                      const std::uint8_t* after, const HostOutcome& host)
{
    trace::Case c;
    c.kind = trace::CaseKind::kInteger32;
    c.mode = trace::RunMode::kSingleStep;
    c.flags = trace::kCompareEip;
    c.features = trace::kFeatureX87 | trace::kFeatureCmov;
    c.regions = {{kBase, kCodeSize, static_cast<std::uint8_t>(rex86::kPageReadWriteExecute)},
                 {kWorkBase, kWorkSize, static_cast<std::uint8_t>(rex86::kPageReadWrite)}};
    c.fill_seed = input.fill_seed;
    c.patches = std::move(patches);
    c.selectors = selectors;
    c.segment_present = 0;
    for (int i = 0; i < 6; ++i)
    {
        if (selectors[i] != 0) c.segment_present = static_cast<std::uint8_t>(c.segment_present | (1u << i));
    }
    c.input.gpr = input.gpr;
    c.input.eip = kInstruction;
    c.input.eflags = input.eflags;

    c.expected.gpr = host.gpr;
    c.expected.eflags = host.eflags;
    c.eflags_mask = ~kEflagsResume;
    const bool single_step = host.signal == SIGTRAP && host.trapno == 1;
    const bool breakpoint = host.signal == SIGTRAP && host.trapno == 3;  // POPF cleared TF
    if (single_step || breakpoint)
    {
        c.reason = static_cast<std::uint8_t>(rex86::StopReason::kBudgetExhausted);
        c.expected.eip = breakpoint ? host.eip - 1u : host.eip;
        MaskUndefined(input, host, &c);
    }
    else
    {
        c.reason = static_cast<std::uint8_t>(rex86::StopReason::kFault);
        const rex86::FaultKind kind = FaultOf(host);
        c.fault_kind = static_cast<std::uint8_t>(kind);
        c.expected.eip = host.eip;
        if (kind == rex86::FaultKind::kAccessViolation)
        {
            c.flags |= trace::kCompareFaultAddress;
            c.fault_address = host.cr2;
        }
        // A REP CMPS/SCAS that faults after completing iterations: the SDM
        // does not say which flags the fault reports. The P6 host shows
        // the flags the instruction started with, the 386EX (SST) those of
        // the last completed iteration, which the core follows.
        const ZydisMnemonic m = input.decoded.instruction.mnemonic;
        const bool compare_string = m == ZYDIS_MNEMONIC_CMPSB || m == ZYDIS_MNEMONIC_CMPSW ||
                                    m == ZYDIS_MNEMONIC_CMPSD || m == ZYDIS_MNEMONIC_SCASB ||
                                    m == ZYDIS_MNEMONIC_SCASW || m == ZYDIS_MNEMONIC_SCASD;
        if (compare_string && host.gpr[1] != input.gpr[1])
        {
            c.eflags_mask &= ~kEflagsArithmetic;
        }
    }
    c.diffs = Differences(before, after, kBase, kRegionSize);
    return c;
}

std::string Hex(const std::vector<std::uint8_t>& bytes)
{
    std::string text;
    char piece[4];
    for (const std::uint8_t b : bytes)
    {
        std::snprintf(piece, sizeof piece, "%02X", b);
        text += piece;
    }
    return text;
}

}  // namespace

int main(int argc, char** argv)
{
    std::uint64_t iterations = 20000;
    std::uint64_t seed = 1;
    bool verbose = false;
    bool stats = false;
    std::string only;
    std::string record_path;
    std::string failures_path;
    int positional = 0;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--verbose") == 0)
        {
            verbose = true;
        }
        else if (std::strcmp(argv[i], "--stats") == 0)
        {
            stats = true;
        }
        else if (std::strcmp(argv[i], "--only") == 0 && i + 1 < argc)
        {
            only = argv[++i];
        }
        else if (std::strcmp(argv[i], "--record") == 0 && i + 1 < argc)
        {
            record_path = argv[++i];
        }
        else if (std::strcmp(argv[i], "--failures") == 0 && i + 1 < argc)
        {
            failures_path = argv[++i];
        }
        else if (positional++ == 0)
        {
            iterations = std::strtoull(argv[i], nullptr, 0);
        }
        else
        {
            seed = std::strtoull(argv[i], nullptr, 0);
        }
    }

    const rex86::decode::Decoder decoder(rex86::decode::Decoder::Mode::kLegacy32);
    std::vector<Form> forms = BuildForms(decoder);
    if (!only.empty())
    {
        forms.erase(std::remove_if(forms.begin(), forms.end(),
                                   [&](const Form& f) { return f.mnemonic != only; }),
                    forms.end());
        if (forms.empty())
        {
            std::fprintf(stderr, "no form named %s\n", only.c_str());
            return 2;
        }
    }
    trace::Writer record;
    trace::Writer failures;
    std::string error;
    if ((!record_path.empty() && !record.Open(record_path, &error)) ||
        (!failures_path.empty() && !failures.Open(failures_path, &error)))
    {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 2;
    }

    Generator generator(seed, decoder, forms);
    Host host;
    std::vector<std::uint8_t> before(kRegionSize);
    std::map<std::string, std::uint64_t> failed;
    std::map<rex86::FaultKind, std::uint64_t> faults;
    // Per mnemonic: cases retired, cases faulted.
    std::map<std::string, std::pair<std::uint64_t, std::uint64_t>> coverage;
    std::uint64_t mismatches = 0;
    std::uint64_t retired = 0;
    for (std::uint64_t n = 0; n < iterations; ++n)
    {
        Input input;
        generator.Next(&input);
        std::vector<trace::Bytes> patches = host.Prepare(input);
        std::memcpy(before.data(), host.region(), kRegionSize);
        const bool rep_string = (input.decoded.instruction.attributes &
                                 (ZYDIS_ATTRIB_HAS_REP | ZYDIS_ATTRIB_HAS_REPE | ZYDIS_ATTRIB_HAS_REPNE)) != 0;
        const HostOutcome outcome = host.Run(rep_string);
        const trace::Case c = BuildCase(input, host.selectors(), std::move(patches), before.data(),
                                        host.region(), outcome);
        auto& covered = coverage[input.decoded.MnemonicName()];
        if (c.reason == static_cast<std::uint8_t>(rex86::StopReason::kFault))
        {
            ++faults[static_cast<rex86::FaultKind>(c.fault_kind)];
            ++covered.second;
        }
        else
        {
            ++retired;
            ++covered.first;
        }
        if (!record_path.empty())
        {
            record.Add(c);
        }
        const trace::ReplayResult result = trace::Replay(c);
        if (result.matched)
        {
            continue;
        }
        ++mismatches;
        const std::string name = input.decoded.MnemonicName();
        if (!failures_path.empty())
        {
            failures.Add(c);
        }
        if (failed[name]++ < 3 || verbose)
        {
            std::printf("MISMATCH %s: %s\n  bytes=%s signal=%d trapno=%u", name.c_str(),
                        result.difference.c_str(), Hex(input.code).c_str(), outcome.signal,
                        outcome.trapno);
            if (!failures_path.empty())
            {
                std::printf(" (--failures case %u)", failures.count() - 1);
            }
            std::printf("\n");
        }
    }
    if (!record_path.empty()) record.Close();
    if (!failures_path.empty()) failures.Close();

    std::printf("forms=%zu iterations=%llu seed=%llu mismatches=%llu retired=%llu rejected=%llu\n",
                forms.size(), static_cast<unsigned long long>(iterations),
                static_cast<unsigned long long>(seed), static_cast<unsigned long long>(mismatches),
                static_cast<unsigned long long>(retired),
                static_cast<unsigned long long>(generator.rejected()));
    std::printf("faults:");
    for (const auto& [kind, count] : faults)
    {
        std::printf(" %d=%llu", static_cast<int>(kind), static_cast<unsigned long long>(count));
    }
    std::printf("\n");
    if (stats)
    {
        std::printf("mnemonics=%zu (retired / faulted)\n", coverage.size());
        for (const auto& [mnemonic, counts] : coverage)
        {
            std::printf("  %-16s %llu / %llu\n", mnemonic.c_str(),
                        static_cast<unsigned long long>(counts.first),
                        static_cast<unsigned long long>(counts.second));
        }
    }
    for (const auto& [mnemonic, count] : failed)
    {
        std::printf("  %-16s %llu\n", mnemonic.c_str(), static_cast<unsigned long long>(count));
    }
    return mismatches == 0 ? 0 : 1;
}
