// The x87 host-CPU comparison fuzz (design #19, decision 6): one random
// x87 instruction runs on the host CPU and on the core from the same
// FRSTOR image, flags, EAX and memory operand, and the results are
// compared. The same machine code runs on both sides:
//
//   push edx; popf; frstor [edi]; <instruction>; fnsave [esi];
//   pushf; pop edx; ret        (HLT instead of ret on the core)
//
// which means the same on i386 and x86-64 (no REX: [edi]/[esi]/[ebx] are
// [rdi]/[rsi]/[rbx] in 64-bit mode). Memory operands are [ebx].
//
// Usage: rex86_x87_fuzz [iterations] [seed] [--verbose]
//        rex86_x87_fuzz --replay <code hex> <image hex> <data hex> <flags hex>
// The replay form runs one case (as a MISMATCH line prints it) and dumps
// both sides.

#if !defined(__linux__) || !(defined(__x86_64__) || defined(__i386__))
#error "the x87 host comparison runs on x86 or x86-64 Linux only"
#endif

#include <sys/mman.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "decode/decoder.h"
#include "rex86/cpu.h"
#include "rex86/environment.h"
#include "rex86/guest_memory.h"

namespace
{

constexpr unsigned kImageSize = 108;
constexpr unsigned kDataSize = 128;

// --- the instruction table ---------------------------------------------

struct Form
{
    std::vector<std::uint8_t> bytes;
    std::string mnemonic;
    unsigned memory_size = 0;  // bytes, 0 for register forms
};

bool Excluded(const ZydisMnemonic mnemonic)
{
    switch (mnemonic)
    {
        // Increment 2: the transcendentals.
        case ZYDIS_MNEMONIC_FSIN: case ZYDIS_MNEMONIC_FCOS:
        case ZYDIS_MNEMONIC_FSINCOS: case ZYDIS_MNEMONIC_FPTAN:
        case ZYDIS_MNEMONIC_FPATAN: case ZYDIS_MNEMONIC_F2XM1:
        case ZYDIS_MNEMONIC_FYL2X: case ZYDIS_MNEMONIC_FYL2XP1:
        // Out of scope (SSE3) and the 8087/287 no-ops (no state to compare).
        case ZYDIS_MNEMONIC_FISTTP:
            return true;
        default:
            return false;
    }
}

std::vector<Form> BuildForms()
{
    const rex86::decode::Decoder decoder(
        rex86::decode::Decoder::Mode::kLegacy32);
    std::vector<Form> forms;
    const auto add = [&](const std::vector<std::uint8_t>& bytes) {
        std::uint8_t padded[16] = {};
        std::memcpy(padded, bytes.data(), bytes.size());
        rex86::decode::DecodedInstruction d;
        if (!decoder.Decode(padded, bytes.size(), 0, &d) ||
            d.Length() != bytes.size())
        {
            return;
        }
        const ZydisISASet isa = d.instruction.meta.isa_set;
        if ((isa != ZYDIS_ISA_SET_X87 && isa != ZYDIS_ISA_SET_FCMOV &&
             isa != ZYDIS_ISA_SET_FCOMI) ||
            Excluded(d.instruction.mnemonic))
        {
            return;
        }
        Form form;
        form.bytes = bytes;
        form.mnemonic = d.MnemonicName();
        for (ZyanU8 i = 0; i < d.instruction.operand_count; ++i)
        {
            if (d.operands[i].type == ZYDIS_OPERAND_TYPE_MEMORY)
            {
                form.memory_size = d.operands[i].size / 8u;
                form.mnemonic += " m" + std::to_string(d.operands[i].size);
            }
        }
        if (bytes[0] == 0x66)
        {
            form.mnemonic = "o16 " + form.mnemonic;
        }
        forms.push_back(form);
    };
    for (unsigned opcode = 0xD8; opcode <= 0xDF; ++opcode)
    {
        for (unsigned modrm = 0xC0; modrm <= 0xFF; ++modrm)
        {
            add({static_cast<std::uint8_t>(opcode),
                 static_cast<std::uint8_t>(modrm)});
        }
        for (unsigned reg = 0; reg < 8; ++reg)
        {
            const auto modrm = static_cast<std::uint8_t>((reg << 3) | 3u);  // [ebx]
            add({static_cast<std::uint8_t>(opcode), modrm});
            // The 16-bit environment layouts.
            if ((opcode == 0xD9 && (reg == 4 || reg == 6)) ||
                (opcode == 0xDD && (reg == 4 || reg == 6)))
            {
                add({0x66, static_cast<std::uint8_t>(opcode), modrm});
            }
        }
    }
    return forms;
}

// --- random inputs -------------------------------------------------------

class Generator
{
public:
    explicit Generator(const std::uint64_t seed) : rng_(seed)
    {
    }

    std::uint64_t Bits(const unsigned count)
    {
        const std::uint64_t value = rng_();
        return count >= 64 ? value : value & ((std::uint64_t{1} << count) - 1u);
    }

    bool Chance(const unsigned percent)
    {
        return Bits(16) % 100 < percent;
    }

    // An 80-bit value weighted toward the x87's interesting encodings.
    void Real80(std::uint8_t out[10])
    {
        std::uint64_t significand = Bits(64);
        std::uint32_t exponent = 0;
        switch (Bits(8) % 16)
        {
            case 0: significand = 0; exponent = 0; break;                     // zero
            case 1: significand &= ~(1ull << 63); exponent = 0; break;        // denormal
            case 2: significand |= 1ull << 63; exponent = 0; break;          // pseudo-denormal
            case 3: significand = 1ull << 63; exponent = 0x7FFF; break;       // infinity
            case 4: significand |= 3ull << 62; exponent = 0x7FFF; break;      // QNaN
            case 5:                                                            // SNaN
                significand = (significand & ~(3ull << 62)) | (1ull << 63) | 1u;
                exponent = 0x7FFF;
                break;
            case 6: significand &= ~(1ull << 63); exponent = 1 + Bits(15) % 0x7FFE; break;  // unnormal
            case 7: significand &= ~(1ull << 63); exponent = 0x7FFF; break;  // pseudo-NaN/inf
            case 8: significand |= 1ull << 63; exponent = 1 + Bits(6); break;  // tiny
            case 9: significand |= 1ull << 63; exponent = 0x7FFE - Bits(6); break;  // huge
            case 10:                                                           // small integer
                significand = (Bits(16) | 1u) << 48;
                while ((significand >> 63) == 0) significand <<= 1;
                exponent = 0x3FFF + Bits(5);
                break;
            case 11: significand = ~0ull; exponent = 0x3FFF + Bits(7) - 64; break;  // all ones
            case 12: significand = (1ull << 63) | Bits(3); exponent = 0x3FFF + Bits(7) - 64; break;
            default: significand |= 1ull << 63; exponent = 0x3FFF + Bits(8) - 128; break;
        }
        if (Chance(50)) exponent |= 0x8000u;
        for (int i = 0; i < 8; ++i) out[i] = static_cast<std::uint8_t>(significand >> (8 * i));
        out[8] = static_cast<std::uint8_t>(exponent);
        out[9] = static_cast<std::uint8_t>(exponent >> 8);
    }

    // A memory operand of `size` bytes in an interesting encoding.
    void Operand(std::uint8_t* out, const unsigned size)
    {
        for (unsigned i = 0; i < kDataSize; ++i) out[i] = static_cast<std::uint8_t>(Bits(8));
        if (Chance(20)) return;
        if (size == 10)
        {
            if (Chance(30))
            {
                // Packed BCD with valid digits.
                for (int i = 0; i < 9; ++i)
                {
                    out[i] = static_cast<std::uint8_t>(((Bits(8) % 10) << 4) | (Bits(8) % 10));
                }
                out[9] = Chance(50) ? 0x80u : 0x00u;
                return;
            }
            Real80(out);
            return;
        }
        if (size == 4 || size == 8)
        {
            const unsigned bits = size * 8;
            const unsigned exp_bits = size == 4 ? 8 : 11;
            const unsigned frac_bits = bits - 1 - exp_bits;
            std::uint64_t exponent = Bits(exp_bits);
            std::uint64_t fraction = Bits(frac_bits);
            switch (Bits(8) % 8)
            {
                case 0: exponent = 0; fraction = 0; break;
                case 1: exponent = 0; break;
                case 2: exponent = (1u << exp_bits) - 1; fraction = 0; break;
                case 3: exponent = (1u << exp_bits) - 1; fraction |= 1ull << (frac_bits - 1); break;
                case 4: exponent = (1u << exp_bits) - 1; fraction = (fraction & ~(1ull << (frac_bits - 1))) | 1u; break;
                case 5: exponent = (1u << (exp_bits - 1)) - 1 + Bits(5); break;
                default: break;
            }
            const std::uint64_t value = (Bits(1) << (bits - 1)) | (exponent << frac_bits) | fraction;
            for (unsigned i = 0; i < size; ++i) out[i] = static_cast<std::uint8_t>(value >> (8 * i));
            return;
        }
        if (size == 2 && Chance(50))
        {
            // A control word or a small integer.
            const std::uint16_t v = static_cast<std::uint16_t>(
                Chance(50) ? (0x0040u | (Bits(4) << 8) | (Chance(70) ? 0x3Fu : Bits(6)))
                           : Bits(16));
            out[0] = static_cast<std::uint8_t>(v);
            out[1] = static_cast<std::uint8_t>(v >> 8);
        }
    }

    // A FRSTOR image (32-bit protected-mode layout).
    void Image(std::uint8_t image[kImageSize])
    {
        std::memset(image, 0, kImageSize);
        const std::uint16_t masks = static_cast<std::uint16_t>(Chance(70) ? 0x3Fu : Bits(6));
        const std::uint16_t cw = static_cast<std::uint16_t>(
            0x0040u | masks | (Bits(2) << 8) | (Bits(2) << 10) | (Bits(1) << 12));
        // Only masked exception flags may be pending, or the host would
        // raise #MF at the tested instruction.
        const std::uint16_t flags = static_cast<std::uint16_t>(Bits(7) & (masks | 0x40u));
        const std::uint16_t sw = static_cast<std::uint16_t>(
            (Bits(3) << 11) | (Bits(1) << 8) | (Bits(1) << 9) | (Bits(1) << 10) |
            (Bits(1) << 14) | (flags & 0x7Fu));
        std::uint16_t tw = 0;
        for (unsigned r = 0; r < 8; ++r)
        {
            if (Chance(25)) tw = static_cast<std::uint16_t>(tw | (3u << (2 * r)));
        }
        image[0] = static_cast<std::uint8_t>(cw);
        image[1] = static_cast<std::uint8_t>(cw >> 8);
        image[4] = static_cast<std::uint8_t>(sw);
        image[5] = static_cast<std::uint8_t>(sw >> 8);
        image[8] = static_cast<std::uint8_t>(tw);
        image[9] = static_cast<std::uint8_t>(tw >> 8);
        for (unsigned st = 0; st < 8; ++st)
        {
            Real80(image + 28 + 10 * st);
        }
    }

private:
    std::mt19937_64 rng_;
};

// --- the two runners -----------------------------------------------------

struct Outcome
{
    std::uint8_t image[kImageSize] = {};
    std::uint8_t data[kDataSize] = {};
    std::uint32_t flags = 0;
    std::uint32_t eax = 0;
};

struct Input
{
    std::vector<std::uint8_t> code;  // the instruction alone
    std::uint8_t image[kImageSize] = {};
    std::uint8_t data[kDataSize] = {};
    std::uint32_t flags = 0;
    std::uint32_t eax = 0;
};

std::vector<std::uint8_t> Stub(const std::vector<std::uint8_t>& instruction,
                               const std::uint8_t last)
{
    std::vector<std::uint8_t> code = {0x52, 0x9D, 0xDD, 0x27};  // push edx; popf; frstor [edi]
    code.insert(code.end(), instruction.begin(), instruction.end());
    code.insert(code.end(), {0xDD, 0x36, 0x9C, 0x5A, last});    // fnsave [esi]; pushf; pop edx
    return code;
}

class HostRunner
{
public:
    HostRunner()
    {
        page_ = static_cast<std::uint8_t*>(mmap(nullptr, 4096,
                                                PROT_READ | PROT_WRITE | PROT_EXEC,
                                                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        if (page_ == MAP_FAILED)
        {
            std::perror("mmap");
            std::exit(2);
        }
    }

    Outcome Run(const Input& input)
    {
        const std::vector<std::uint8_t> code = Stub(input.code, 0xC3);
        std::memcpy(page_, code.data(), code.size());
        Outcome out;
        std::uint8_t image[kImageSize];
        std::memcpy(image, input.image, kImageSize);
        std::memcpy(out.data, input.data, kDataSize);
        std::uintptr_t flags = input.flags;
        std::uintptr_t eax = input.eax;
        void* fn = page_;
        std::uint8_t* data = out.data;
        // The stub uses EBX for the memory operand, so EBX is saved by hand
        // and every input sits in a register the stub leaves alone (the
        // compiler may otherwise choose EBX or an ESP-relative slot).
#if defined(__x86_64__)
        register std::uint8_t* data_register asm("r9") = data;
        // Step over the red zone before the call pushes anything.
        asm volatile(
            "sub $128, %%rsp\n\t"
            "push %%rbx\n\t"
            "mov %%r9, %%rbx\n\t"
            "call *%%rcx\n\t"
            "pop %%rbx\n\t"
            "add $128, %%rsp"
            : "+d"(flags), "+a"(eax), "+c"(fn), "+r"(data_register)
            : "D"(image), "S"(out.image)
            : "memory", "cc", "r8", "r10", "r11");
#else
        asm volatile(
            "push %[data]\n\t"
            "push %%ebx\n\t"
            "mov 4(%%esp), %%ebx\n\t"
            "call *%%ecx\n\t"
            "pop %%ebx\n\t"
            "add $4, %%esp"
            : "+d"(flags), "+a"(eax), "+c"(fn)
            : "D"(image), "S"(out.image), [data] "m"(data)
            : "memory", "cc");
#endif
        out.flags = static_cast<std::uint32_t>(flags);
        out.eax = static_cast<std::uint32_t>(eax);
        return out;
    }

private:
    std::uint8_t* page_ = nullptr;
};

class NullEnvironment final : public rex86::Environment
{
public:
    bool LoadDescriptor(std::uint16_t, rex86::Descriptor* descriptor) override
    {
        *descriptor = rex86::Descriptor{};
        return true;
    }
    bool PortRead(std::uint16_t, std::uint8_t, std::uint32_t*) override { return false; }
    bool PortWrite(std::uint16_t, std::uint8_t, std::uint32_t) override { return false; }
    bool InterruptTarget(std::uint8_t, std::uint16_t*, std::uint32_t*) override { return false; }
    std::uint64_t ReadTimeStampCounter() override { return 0; }
    void Cpuid(std::uint32_t, std::uint32_t, std::uint32_t r[4]) override
    {
        r[0] = r[1] = r[2] = r[3] = 0;
    }
};

class CoreRunner
{
public:
    static constexpr std::uint32_t kCode = 0x1000;
    static constexpr std::uint32_t kIn = 0x2000;
    static constexpr std::uint32_t kOut = 0x3000;
    static constexpr std::uint32_t kData = 0x4000;
    static constexpr std::uint32_t kStack = 0x8000;

    CoreRunner()
        : buffer_(0x10000, 0),
          memory_(buffer_.data(), static_cast<std::uint32_t>(buffer_.size()))
    {
        memory_.pages().Set(0, static_cast<std::uint32_t>(buffer_.size()),
                            rex86::kPageReadWriteExecute);
    }

    Outcome Run(const Input& input, rex86::Event* event)
    {
        const std::vector<std::uint8_t> code = Stub(input.code, 0xF4);
        std::memcpy(buffer_.data() + kCode, code.data(), code.size());
        std::memcpy(buffer_.data() + kIn, input.image, kImageSize);
        std::memset(buffer_.data() + kOut, 0, kImageSize);
        std::memcpy(buffer_.data() + kData, input.data, kDataSize);
        rex86::Cpu cpu(&memory_, &environment_, rex86::Features{});
        rex86::CpuState& s = cpu.state();
        s.eip = kCode;
        s.Set(rex86::Gpr::kEdi, kIn);
        s.Set(rex86::Gpr::kEsi, kOut);
        s.Set(rex86::Gpr::kEbx, kData);
        s.Set(rex86::Gpr::kEdx, input.flags);
        s.Set(rex86::Gpr::kEax, input.eax);
        s.Set(rex86::Gpr::kEsp, kStack);
        *event = cpu.Run(16);
        Outcome out;
        std::memcpy(out.image, buffer_.data() + kOut, kImageSize);
        std::memcpy(out.data, buffer_.data() + kData, kDataSize);
        out.flags = s.Get(rex86::Gpr::kEdx);
        out.eax = s.Get(rex86::Gpr::kEax);
        return out;
    }

private:
    std::vector<std::uint8_t> buffer_;
    rex86::GuestMemory memory_;
    NullEnvironment environment_;
};

// --- comparison ----------------------------------------------------------

std::string Hex(const std::uint8_t* bytes, const unsigned count)
{
    std::string text;
    char piece[4];
    for (unsigned i = 0; i < count; ++i)
    {
        std::snprintf(piece, sizeof piece, "%02X", bytes[i]);
        text += piece;
    }
    return text;
}

// The first difference, or empty. FIP/FCS/FOP/FDP/FDS (image bytes 12-27)
// hold host addresses and are not compared.
std::string Difference(const Outcome& host, const Outcome& core)
{
    char text[160];
    for (unsigned i = 0; i < kImageSize; ++i)
    {
        if (i >= 12 && i < 28) continue;
        if (host.image[i] != core.image[i])
        {
            const char* field = i < 4 ? "cw" : i < 8 ? "sw" : i < 12 ? "tw" : "st";
            if (i >= 28)
            {
                std::snprintf(text, sizeof text, "st(%u) host=%s core=%s", (i - 28) / 10,
                              Hex(host.image + 28 + 10 * ((i - 28) / 10), 10).c_str(),
                              Hex(core.image + 28 + 10 * ((i - 28) / 10), 10).c_str());
            }
            else
            {
                const unsigned base = i & ~3u;
                const unsigned h = host.image[base] | (host.image[base + 1] << 8);
                const unsigned c = core.image[base] | (core.image[base + 1] << 8);
                std::snprintf(text, sizeof text, "%s host=%04X core=%04X diff=%04X",
                              field, h, c, h ^ c);
            }
            return text;
        }
    }
    for (unsigned i = 0; i < kDataSize; ++i)
    {
        if (host.data[i] != core.data[i])
        {
            std::snprintf(text, sizeof text, "mem[%u] host=%s core=%s", i,
                          Hex(host.data + (i & ~7u), 8).c_str(),
                          Hex(core.data + (i & ~7u), 8).c_str());
            return text;
        }
    }
    if (((host.flags ^ core.flags) & 0x8D5u) != 0)
    {
        std::snprintf(text, sizeof text, "eflags host=%08X core=%08X", host.flags, core.flags);
        return text;
    }
    if (host.eax != core.eax)
    {
        std::snprintf(text, sizeof text, "eax host=%08X core=%08X", host.eax, core.eax);
        return text;
    }
    return {};
}

bool IsCompare(const std::string& mnemonic)
{
    static const char* const kCompares[] = {"fcom", "fcomp", "fcompp", "fucom",
                                            "fucomp", "fucompp", "ficom", "ficomp",
                                            "fcomi", "fcomip", "fucomi", "fucomip",
                                            "ftst"};
    const std::string base = mnemonic.substr(0, mnemonic.find(' '));
    for (const char* name : kCompares)
    {
        if (base == name) return true;
    }
    return false;
}

// A known vendor deviation from the SDM, counted apart: the SDM's FCOM
// and FCOMI tables say "flags not set if unmasked #IA", while the AMD Zen 3
// host reports unordered (C3/C2/C0 = 111, or ZF/PF/CF = 111) anyway. The
// core follows the SDM; see docs/analysis/x87-host-comparison.md.
bool UnmaskedInvalidCompareFlags(const std::string& mnemonic, const Input& input,
                                 const Outcome& host, const Outcome& core)
{
    if (!IsCompare(mnemonic) || (input.image[0] & 1u) != 0)
    {
        return false;  // not a compare, or #IA masked
    }
    if ((host.image[4] & 1u) == 0)
    {
        return false;  // no #IA raised
    }
    Outcome patched = core;
    std::memcpy(patched.image + 4, host.image + 4, 2);  // SW
    patched.flags = (core.flags & ~0x8D5u) | (host.flags & 0x8D5u);
    const std::uint16_t sw_diff = static_cast<std::uint16_t>(
        (host.image[4] | (host.image[5] << 8)) ^ (core.image[4] | (core.image[5] << 8)));
    if ((sw_diff & ~0x4500u) != 0)
    {
        return false;  // something besides C3/C2/C0 differs
    }
    return Difference(host, patched).empty();
}

std::vector<std::uint8_t> FromHex(const char* text)
{
    std::vector<std::uint8_t> bytes;
    for (std::size_t i = 0; text[i] != 0 && text[i + 1] != 0; i += 2)
    {
        const char pair[3] = {text[i], text[i + 1], 0};
        bytes.push_back(static_cast<std::uint8_t>(std::strtoul(pair, nullptr, 16)));
    }
    return bytes;
}

void Dump(const char* side, const Outcome& o)
{
    const auto word = [&](const unsigned at) {
        return static_cast<unsigned>(o.image[at] | (o.image[at + 1] << 8));
    };
    std::printf("%s cw=%04X sw=%04X tw=%04X eflags=%08X eax=%08X\n", side, word(0),
                word(4), word(8), o.flags, o.eax);
    for (unsigned st = 0; st < 8; ++st)
    {
        std::printf("   st(%u) %s\n", st, Hex(o.image + 28 + 10 * st, 10).c_str());
    }
    std::printf("   mem %s\n", Hex(o.data, 32).c_str());
}

int Replay(char** argv)
{
    Input input;
    input.code = FromHex(argv[0]);
    const std::vector<std::uint8_t> image = FromHex(argv[1]);
    const std::vector<std::uint8_t> data = FromHex(argv[2]);
    std::memcpy(input.image, image.data(), std::min<std::size_t>(image.size(), kImageSize));
    std::memcpy(input.data, data.data(), std::min<std::size_t>(data.size(), kDataSize));
    input.flags = static_cast<std::uint32_t>(std::strtoul(argv[3], nullptr, 16));
    HostRunner host;
    CoreRunner core;
    Dump("input", [&] {
        Outcome o;
        std::memcpy(o.image, input.image, kImageSize);
        std::memcpy(o.data, input.data, kDataSize);
        o.flags = input.flags;
        return o;
    }());
    Dump("host ", host.Run(input));
    rex86::Event event;
    const Outcome c = core.Run(input, &event);
    Dump("core ", c);
    std::printf("core stop reason=%d fault=%d\n", static_cast<int>(event.reason),
                static_cast<int>(event.fault_kind));
    return 0;
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc == 6 && std::strcmp(argv[1], "--replay") == 0)
    {
        return Replay(argv + 2);
    }
    std::uint64_t iterations = 20000;
    std::uint64_t seed = 1;
    bool verbose = false;
    int positional = 0;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--verbose") == 0)
        {
            verbose = true;
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

    const std::vector<Form> forms = BuildForms();
    Generator gen(seed);
    HostRunner host;
    CoreRunner core;
    std::map<std::string, std::uint64_t> failures;
    std::uint64_t mismatches = 0;
    std::uint64_t core_stops = 0;
    std::uint64_t vendor_deviations = 0;
    for (std::uint64_t n = 0; n < iterations; ++n)
    {
        const Form& form = forms[gen.Bits(32) % forms.size()];
        Input input;
        input.code = form.bytes;
        gen.Image(input.image);
        gen.Operand(input.data, form.memory_size);
        input.flags = static_cast<std::uint32_t>(0x202u | (gen.Bits(12) & 0x8D5u));
        input.eax = static_cast<std::uint32_t>(gen.Bits(32));

        const Outcome expected = host.Run(input);
        rex86::Event event;
        const Outcome actual = core.Run(input, &event);
        std::string difference;
        if (event.reason != rex86::StopReason::kHalted)
        {
            ++core_stops;
            difference = "core stopped: reason=" + std::to_string(static_cast<int>(event.reason)) +
                " fault=" + std::to_string(static_cast<int>(event.fault_kind));
        }
        else
        {
            difference = Difference(expected, actual);
            if (!difference.empty() &&
                UnmaskedInvalidCompareFlags(form.mnemonic, input, expected, actual))
            {
                ++vendor_deviations;
                difference.clear();
            }
        }
        if (!difference.empty())
        {
            ++mismatches;
            if (failures[form.mnemonic]++ < 3 || verbose)
            {
                std::printf("MISMATCH %s: %s\n  --replay %s %s %s %08X\n",
                            form.mnemonic.c_str(), difference.c_str(),
                            Hex(form.bytes.data(), static_cast<unsigned>(form.bytes.size())).c_str(),
                            Hex(input.image, kImageSize).c_str(),
                            Hex(input.data, kDataSize).c_str(), input.flags);
            }
        }
    }
    std::printf("forms=%zu iterations=%llu seed=%llu mismatches=%llu core_stops=%llu "
                "vendor_deviations=%llu\n",
                forms.size(), static_cast<unsigned long long>(iterations),
                static_cast<unsigned long long>(seed),
                static_cast<unsigned long long>(mismatches),
                static_cast<unsigned long long>(core_stops),
                static_cast<unsigned long long>(vendor_deviations));
    for (const auto& [mnemonic, count] : failures)
    {
        std::printf("  %-24s %llu\n", mnemonic.c_str(), static_cast<unsigned long long>(count));
    }
    return mismatches == 0 ? 0 : 1;
}
