// The SIMD host-CPU comparison fuzz (design #29, decision 7): one random
// MMX or SSE instruction runs on the host CPU and on the core from the same
// FXRSTOR image, flags, EAX and memory, and the results are compared. The
// same machine code runs on both sides:
//
//   push edx; popf; fxrstor [edi]; <instruction>; fxsave [esi];
//   pushf; pop edx; ret        (HLT instead of ret on the core)
//
// which means the same on i386 and x86-64 (no REX). Memory operands are
// [ebx] (16-byte aligned) and [ebx+8] (misaligned for 16-byte operands);
// the only general register operand allowed is EAX, and MASKMOVQ stores
// at [edi], the input image, which is compared too. A fault on the host
// arrives as a signal and is compared with the core's fault kind.
//
// Usage: rex86_simd_fuzz [iterations] [seed] [--verbose] [--record FILE]
//                        [--only MNEMONIC[,MNEMONIC...]]
// --record writes every matching case as a trace (design #22, decision 5),
// replayed on the spot as a check of the recording itself.

#if !defined(__linux__) || !(defined(__x86_64__) || defined(__i386__))
#error "the SIMD host comparison runs on x86 or x86-64 Linux only"
#endif

#include <setjmp.h>
#include <signal.h>
#include <sys/mman.h>
#include <ucontext.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "decode/decoder.h"
#include "rex86/cpu.h"
#include "rex86/environment.h"
#include "rex86/guest_memory.h"
#include "trace/replay.h"
#include "trace/trace_format.h"

namespace
{

constexpr unsigned kImageSize = 512;
// FXSAVE's state bytes; the rest is not written by the core (and holds
// XMM8-15 on an x86-64 host).
constexpr unsigned kStateSize = 288;
constexpr unsigned kDataSize = 64;
// Offset of the instruction in the stub: push edx; popf; fxrstor [edi].
constexpr unsigned kInstructionOffset = 5;

// --- the instruction table ---------------------------------------------

struct Form
{
    std::vector<std::uint8_t> bytes;
    std::string mnemonic;
    int immediate = -1;  // offset of the imm8, or -1
    bool mxcsr_load = false;
};

bool Wanted(const rex86::decode::DecodedInstruction& d)
{
    const ZydisISASet isa = d.instruction.meta.isa_set;
    if (isa != ZYDIS_ISA_SET_PENTIUMMMX && isa != ZYDIS_ISA_SET_SSE &&
        isa != ZYDIS_ISA_SET_SSEMXCSR && isa != ZYDIS_ISA_SET_SSE_PREFETCH)
    {
        return false;
    }
    for (ZyanU8 i = 0; i < d.instruction.operand_count_visible; ++i)
    {
        const ZydisDecodedOperand& op = d.operands[i];
        if (op.type == ZYDIS_OPERAND_TYPE_REGISTER &&
            ZydisRegisterGetClass(op.reg.value) == ZYDIS_REGCLASS_GPR32 &&
            op.reg.value != ZYDIS_REGISTER_EAX)
        {
            return false;
        }
    }
    return true;
}

std::vector<Form> BuildForms()
{
    const rex86::decode::Decoder decoder(rex86::decode::Decoder::Mode::kLegacy32);
    std::vector<Form> forms;
    // A form is at most 6 bytes (prefix, 0F, opcode, ModRM, disp8, imm8),
    // so its bytes are their own key.
    std::set<std::uint64_t> seen;
    const auto key = [](const std::vector<std::uint8_t>& bytes) {
        std::uint64_t k = bytes.size();
        for (const std::uint8_t b : bytes) k = (k << 8) | b;
        return k;
    };
    // allow_immediate: the bytes end in a ModRM (and displacement), so a
    // failed decode means an imm8 follows. A bare opcode never grows one,
    // or the added byte would be read as a ModRM.
    const auto add = [&](std::vector<std::uint8_t> bytes, const bool allow_immediate) {
        for (int extra = 0; extra < (allow_immediate ? 2 : 1); ++extra)
        {
            std::uint8_t padded[16] = {};
            std::memcpy(padded, bytes.data(), bytes.size());
            rex86::decode::DecodedInstruction d;
            if (!decoder.Decode(padded, bytes.size(), 0, &d))
            {
                bytes.push_back(0);  // an imm8 follows
                continue;
            }
            if (d.Length() != bytes.size() || !Wanted(d) || !seen.insert(key(bytes)).second)
            {
                return;
            }
            Form form;
            form.bytes = bytes;
            form.mnemonic = d.MnemonicName();
            form.immediate = extra == 1 ? static_cast<int>(bytes.size()) - 1 : -1;
            form.mxcsr_load = d.instruction.mnemonic == ZYDIS_MNEMONIC_LDMXCSR;
            for (ZyanU8 i = 0; i < d.instruction.operand_count_visible; ++i)
            {
                if (d.operands[i].type == ZYDIS_OPERAND_TYPE_MEMORY)
                {
                    form.mnemonic += " m" + std::to_string(d.operands[i].size);
                }
            }
            forms.push_back(form);
            return;
        }
    };
    for (const int prefix : {-1, 0xF3})
    {
        for (unsigned opcode = 0x00; opcode <= 0xFF; ++opcode)
        {
            std::vector<std::uint8_t> head;
            if (prefix >= 0) head.push_back(static_cast<std::uint8_t>(prefix));
            head.push_back(0x0F);
            head.push_back(static_cast<std::uint8_t>(opcode));
            for (unsigned modrm = 0xC0; modrm <= 0xFF; ++modrm)
            {
                std::vector<std::uint8_t> b = head;
                b.push_back(static_cast<std::uint8_t>(modrm));
                add(b, true);
            }
            for (unsigned reg = 0; reg < 8; ++reg)
            {
                std::vector<std::uint8_t> b = head;
                b.push_back(static_cast<std::uint8_t>((reg << 3) | 3u));  // [ebx]
                add(b, true);
                std::vector<std::uint8_t> c = head;
                c.push_back(static_cast<std::uint8_t>(0x40 | (reg << 3) | 3u));  // [ebx+8]
                c.push_back(8);
                add(c, true);
            }
            std::vector<std::uint8_t> bare = head;  // EMMS, ...
            add(bare, false);
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

    // A 64-bit value weighted toward lane edges: zero, all ones, the signed
    // extremes per lane width, small shift counts.
    std::uint64_t Value64()
    {
        switch (Bits(8) % 10)
        {
            case 0: return 0;
            case 1: return ~0ull;
            case 2: return Bits(7);  // a shift count around the widths
            case 3:
            {
                const unsigned w = 8u << (Bits(2) % 3);
                std::uint64_t v = 0;
                for (unsigned i = 0; i < 64 / w; ++i)
                {
                    const std::uint64_t edge[4] = {0, (1ull << (w - 1)), (1ull << (w - 1)) - 1,
                                                   (w == 64 ? ~0ull : (1ull << w) - 1)};
                    v |= edge[Bits(2)] << (i * w);
                }
                return v;
            }
            case 4:
            {
                // binary32 lanes of interesting classes.
                std::uint64_t v = 0;
                for (unsigned i = 0; i < 2; ++i) v |= static_cast<std::uint64_t>(Float32()) << (32 * i);
                return v;
            }
            default:
                return Bits(64);
        }
    }

    std::uint32_t Float32()
    {
        std::uint32_t exponent = static_cast<std::uint32_t>(Bits(8));
        std::uint32_t fraction = static_cast<std::uint32_t>(Bits(23));
        switch (Bits(8) % 10)
        {
            case 0: exponent = 0; fraction = 0; break;
            case 1: exponent = 0; break;                                   // denormal
            case 2: exponent = 0xFF; fraction = 0; break;                  // infinity
            case 3: exponent = 0xFF; fraction |= 1u << 22; break;          // QNaN
            case 4: exponent = 0xFF; fraction = (fraction & ~(1u << 22)) | 1u; break;  // SNaN
            case 5: exponent = 1 + static_cast<std::uint32_t>(Bits(3)); break;          // tiny
            case 6: exponent = 0xFE - static_cast<std::uint32_t>(Bits(3)); break;       // huge
            case 7: exponent = 127 + static_cast<std::uint32_t>(Bits(5)); fraction &= 0x7FFF00u; break;
            default: exponent = 100 + static_cast<std::uint32_t>(Bits(6)); break;
        }
        return (static_cast<std::uint32_t>(Bits(1)) << 31) | (exponent << 23) | fraction;
    }

    // An FXSAVE image: x87 control, status and tags, registers holding x87
    // or MMX values, MXCSR without DAZ, XMM values.
    void Image(std::uint8_t* image)
    {
        std::memset(image, 0, kImageSize);
        // FCW: usually all masked; sometimes an unmasked exception.
        std::uint16_t cw = static_cast<std::uint16_t>(0x0040u | (Bits(2) << 8) | (Bits(2) << 10) | 0x3Fu);
        if (Chance(15)) cw = static_cast<std::uint16_t>(cw & ~(1u << Bits(3) % 6));
        // TOP, C0-C2 and C3.
        std::uint16_t sw = static_cast<std::uint16_t>((Bits(3) << 11) | (Bits(3) << 8) | (Bits(1) << 14));
        if (Chance(20)) sw = static_cast<std::uint16_t>(sw | (Bits(6) & 0x3F));
        // ES and B follow the flags and the masks, as the x87 keeps them.
        if ((sw & ~cw & 0x3Fu) != 0) sw = static_cast<std::uint16_t>(sw | 0x8080u);
        image[0] = static_cast<std::uint8_t>(cw);
        image[1] = static_cast<std::uint8_t>(cw >> 8);
        image[2] = static_cast<std::uint8_t>(sw);
        image[3] = static_cast<std::uint8_t>(sw >> 8);
        image[4] = static_cast<std::uint8_t>(Chance(30) ? 0xFF : Bits(8));
        for (unsigned st = 0; st < 8; ++st)
        {
            const std::uint64_t value = Value64();
            for (unsigned i = 0; i < 8; ++i) image[32 + 16 * st + i] = static_cast<std::uint8_t>(value >> (8 * i));
            const std::uint16_t se = static_cast<std::uint16_t>(Chance(40) ? 0xFFFFu : Bits(16));
            image[32 + 16 * st + 8] = static_cast<std::uint8_t>(se);
            image[32 + 16 * st + 9] = static_cast<std::uint8_t>(se >> 8);
        }
        const std::uint32_t mxcsr = Chance(50) ? 0x1F80u
                                               : static_cast<std::uint32_t>(Bits(16) & 0xFFBFu);
        for (unsigned i = 0; i < 4; ++i) image[24 + i] = static_cast<std::uint8_t>(mxcsr >> (8 * i));
        for (unsigned n = 0; n < 8; ++n)
        {
            for (unsigned half = 0; half < 2; ++half)
            {
                const std::uint64_t value = Value64();
                for (unsigned i = 0; i < 8; ++i)
                {
                    image[160 + 16 * n + 8 * half + i] = static_cast<std::uint8_t>(value >> (8 * i));
                }
            }
        }
    }

    void Data(std::uint8_t* data, const bool mxcsr_load)
    {
        for (unsigned at = 0; at < kDataSize; at += 8)
        {
            const std::uint64_t value = Value64();
            for (unsigned i = 0; i < 8; ++i) data[at + i] = static_cast<std::uint8_t>(value >> (8 * i));
        }
        if (mxcsr_load)
        {
            // DAZ exists on the host but not on the Pentium III: never set
            // it. Reserved bits 16 and up fault on both.
            for (const unsigned at : {0u, 8u})
            {
                std::uint32_t v = static_cast<std::uint32_t>(Bits(16)) & 0xFFBFu;
                if (Chance(10)) v |= static_cast<std::uint32_t>(Bits(16)) << 16;
                for (unsigned i = 0; i < 4; ++i) data[at + i] = static_cast<std::uint8_t>(v >> (8 * i));
            }
        }
    }

private:
    std::mt19937_64 rng_;
};

// --- the two runners -----------------------------------------------------

struct Input
{
    std::vector<std::uint8_t> code;
    alignas(16) std::uint8_t image[kImageSize] = {};
    alignas(16) std::uint8_t data[kDataSize] = {};
    std::uint32_t flags = 0;
    std::uint32_t eax = 0;
};

struct Outcome
{
    // The fault kind, kNone when the stub ran to its end.
    rex86::FaultKind fault = rex86::FaultKind::kNone;
    alignas(16) std::uint8_t out[kImageSize] = {};
    alignas(16) std::uint8_t in[kImageSize] = {};
    alignas(16) std::uint8_t data[kDataSize] = {};
    std::uint32_t flags = 0;
    std::uint32_t eax = 0;
};

std::vector<std::uint8_t> Stub(const std::vector<std::uint8_t>& instruction, const std::uint8_t last)
{
    std::vector<std::uint8_t> code = {0x52, 0x9D, 0x0F, 0xAE, 0x0F};  // push edx; popf; fxrstor [edi]
    code.insert(code.end(), instruction.begin(), instruction.end());
    code.insert(code.end(), {0x0F, 0xAE, 0x06, 0x9C, 0x5A, last});    // fxsave [esi]; pushf; pop edx
    return code;
}

sigjmp_buf g_jump;
volatile sig_atomic_t g_armed = 0;
volatile sig_atomic_t g_signal = 0;
volatile sig_atomic_t g_trapno = 0;
volatile std::uintptr_t g_fault_ip = 0;

void Handler(const int signal, siginfo_t*, void* context)
{
    auto* uc = static_cast<ucontext_t*>(context);
    if (g_armed == 0)
    {
        struct sigaction action = {};
        action.sa_handler = SIG_DFL;
        sigaction(signal, &action, nullptr);
        return;
    }
    g_signal = signal;
#if defined(__x86_64__)
    g_trapno = static_cast<int>(uc->uc_mcontext.gregs[REG_TRAPNO]);
    g_fault_ip = static_cast<std::uintptr_t>(uc->uc_mcontext.gregs[REG_RIP]);
#else
    g_trapno = static_cast<int>(uc->uc_mcontext.gregs[REG_TRAPNO]);
    g_fault_ip = static_cast<std::uintptr_t>(uc->uc_mcontext.gregs[REG_EIP]);
#endif
    siglongjmp(g_jump, 1);
}

rex86::FaultKind KindOf(const int signal, const int trapno)
{
    if (signal == SIGILL) return rex86::FaultKind::kIllegalInstruction;
    if (signal == SIGSEGV && trapno == 13) return rex86::FaultKind::kGeneralProtection;
    if (signal == SIGSEGV && trapno == 14) return rex86::FaultKind::kAccessViolation;
    if (signal == SIGFPE && trapno == 16) return rex86::FaultKind::kFloatingPoint;
    if (signal == SIGFPE && trapno == 19) return rex86::FaultKind::kSimdFloatingPoint;
    return rex86::FaultKind::kOther;
}

class HostRunner
{
public:
    HostRunner()
    {
        page_ = static_cast<std::uint8_t*>(mmap(nullptr, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                                                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        if (page_ == MAP_FAILED)
        {
            std::perror("mmap");
            std::exit(2);
        }
        // The signal stack lives as long as the process: a static buffer,
        // which LeakSanitizer does not count as a leak.
        alignas(16) static std::uint8_t alternate_stack[1u << 16];
        stack_t alternate = {};
        alternate.ss_size = sizeof alternate_stack;
        alternate.ss_sp = alternate_stack;
        sigaltstack(&alternate, nullptr);
        struct sigaction action = {};
        action.sa_sigaction = Handler;
        action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_NODEFER;
        sigemptyset(&action.sa_mask);
        for (const int signal : {SIGSEGV, SIGBUS, SIGFPE, SIGILL})
        {
            sigaction(signal, &action, nullptr);
        }
    }

    Outcome Run(const Input& input)
    {
        const std::vector<std::uint8_t> code = Stub(input.code, 0xC3);
        std::memcpy(page_, code.data(), code.size());
        Outcome out;
        std::memcpy(out.in, input.image, kImageSize);
        std::memcpy(out.data, input.data, kDataSize);
        g_signal = 0;
        g_armed = 1;
        if (sigsetjmp(g_jump, 1) == 0)
        {
            Call(&out, input);
        }
        g_armed = 0;
        // Whatever the guest left in the x87 and MXCSR, give compiled code a
        // clean state back.
        const std::uint32_t mxcsr = 0x1F80u;
        asm volatile("fninit\n\tldmxcsr %0" : : "m"(mxcsr));
        if (g_signal != 0)
        {
            out.fault = KindOf(g_signal, g_trapno);
            if (g_fault_ip != reinterpret_cast<std::uintptr_t>(page_) + kInstructionOffset)
            {
                out.fault = rex86::FaultKind::kOther;  // not at the instruction
            }
        }
        return out;
    }

private:
    __attribute__((noinline)) void Call(Outcome* out, const Input& input)
    {
        std::uintptr_t flags = input.flags;
        std::uintptr_t eax = input.eax;
        void* fn = page_;
        std::uint8_t* data = out->data;
        std::uint8_t* in = out->in;
        std::uint8_t* image = out->out;
#if defined(__x86_64__)
        register std::uint8_t* data_register asm("r9") = data;
        asm volatile(
            "sub $128, %%rsp\n\t"
            "push %%rbx\n\t"
            "mov %%r9, %%rbx\n\t"
            "call *%%rcx\n\t"
            "pop %%rbx\n\t"
            "add $128, %%rsp"
            : "+d"(flags), "+a"(eax), "+c"(fn), "+r"(data_register)
            : "D"(in), "S"(image)
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
            : "D"(in), "S"(image), [data] "m"(data)
            : "memory", "cc");
#endif
        out->flags = static_cast<std::uint32_t>(flags);
        out->eax = static_cast<std::uint32_t>(eax);
    }

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
        : buffer_(0x10000, 0), memory_(buffer_.data(), static_cast<std::uint32_t>(buffer_.size()))
    {
        memory_.pages().Set(0, static_cast<std::uint32_t>(buffer_.size()), rex86::kPageReadWriteExecute);
    }

    Outcome Run(const Input& input)
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
        const rex86::Event event = cpu.Run(16);
        Outcome out;
        if (event.reason == rex86::StopReason::kFault)
        {
            out.fault = s.eip == kCode + kInstructionOffset ? event.fault_kind : rex86::FaultKind::kOther;
        }
        else if (event.reason != rex86::StopReason::kHalted)
        {
            out.fault = rex86::FaultKind::kOther;
        }
        std::memcpy(out.out, buffer_.data() + kOut, kImageSize);
        std::memcpy(out.in, buffer_.data() + kIn, kImageSize);
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

// FOP/FIP/FCS/FDP/FDS (bytes 6-23) and MXCSR_MASK (28-31) differ by host
// and are not compared (design #29, measured first).
bool Compared(const unsigned at)
{
    return at < kStateSize && !(at >= 6 && at < 24) && !(at >= 28 && at < 32);
}

std::string Difference(const Outcome& host, const Outcome& core)
{
    char text[200];
    if (host.fault != core.fault)
    {
        std::snprintf(text, sizeof text, "fault host=%d core=%d", static_cast<int>(host.fault),
                      static_cast<int>(core.fault));
        return text;
    }
    if (host.fault != rex86::FaultKind::kNone)
    {
        return {};  // both faulted at the instruction with the same kind
    }
    for (unsigned i = 0; i < kImageSize; ++i)
    {
        if (Compared(i) && host.out[i] != core.out[i])
        {
            std::snprintf(text, sizeof text, "image[%u] host=%02X core=%02X", i, host.out[i], core.out[i]);
            return text;
        }
    }
    for (unsigned i = 0; i < kImageSize; ++i)
    {
        if (host.in[i] != core.in[i])
        {
            std::snprintf(text, sizeof text, "in[%u] host=%02X core=%02X", i, host.in[i], core.in[i]);
            return text;
        }
    }
    for (unsigned i = 0; i < kDataSize; ++i)
    {
        if (host.data[i] != core.data[i])
        {
            std::snprintf(text, sizeof text, "data[%u] host=%02X core=%02X", i, host.data[i], core.data[i]);
            return text;
        }
    }
    if (((host.flags ^ core.flags) & 0x8D5u) != 0)
    {
        std::snprintf(text, sizeof text, "flags host=%08X core=%08X", host.flags, core.flags);
        return text;
    }
    if (host.eax != core.eax)
    {
        std::snprintf(text, sizeof text, "eax host=%08X core=%08X", host.eax, core.eax);
        return text;
    }
    return {};
}

// RCPPS/RSQRTPS and their scalar forms are approximations the SDM bounds
// at a relative error of 1.5 * 2^-12; the core's model is closer, so a
// lane is tolerated when the two finite results differ by at most 2^-11
// relatively, or, for RCP, when the input lies in the SDM's
// implementation-defined band between "never tiny" and "always tiny" and
// one side flushed to zero (design #29, decision 6). Everything else must
// match exactly.
struct Approximation
{
    bool rsqrt = false;
    bool scalar = false;
    unsigned destination = 0;
    std::uint32_t source[4] = {};
};

bool ApproximationOf(const Input& input, Approximation* a)
{
    const std::vector<std::uint8_t>& code = input.code;
    std::size_t at = 0;
    a->scalar = code[0] == 0xF3;
    if (a->scalar) ++at;
    if (code[at] != 0x0F || (code[at + 1] != 0x52 && code[at + 1] != 0x53)) return false;
    a->rsqrt = code[at + 1] == 0x52;
    const std::uint8_t modrm = code[at + 2];
    a->destination = (modrm >> 3) & 7u;
    const std::uint8_t* from = nullptr;
    if ((modrm >> 6) == 3)
    {
        from = input.image + 160 + 16 * (modrm & 7u);
    }
    else
    {
        from = input.data + ((modrm >> 6) == 1 ? code[at + 3] : 0);
    }
    for (unsigned lane = 0; lane < 4; ++lane)
    {
        std::memcpy(&a->source[lane], from + 4 * lane, 4);
    }
    return true;
}

bool LaneTolerated(const Approximation& a, const std::uint32_t x, const std::uint32_t h,
                   const std::uint32_t c)
{
    if (h == c) return true;
    const auto finite_nonzero = [](const std::uint32_t v) {
        return (v & 0x7F800000u) != 0x7F800000u && (v & 0x7FFFFFFFu) != 0;
    };
    if (!a.rsqrt && ((h & 0x7FFFFFFFu) == 0 || (c & 0x7FFFFFFFu) == 0))
    {
        // 1.11111111110100000000000b * 2^125 .. 1.00000000000110000000001b * 2^126
        const std::uint32_t m = x & 0x7FFFFFFFu;
        return m >= 0x7E7FE800u && m <= 0x7E800C01u && (h >> 31) == (c >> 31);
    }
    if (!finite_nonzero(h) || !finite_nonzero(c) || (h >> 31) != (c >> 31)) return false;
    float hf = 0;
    float cf = 0;
    std::memcpy(&hf, &h, 4);
    std::memcpy(&cf, &c, 4);
    const double diff = static_cast<double>(hf) - static_cast<double>(cf);
    const double bound = static_cast<double>(cf) / 2048.0;
    return (diff < 0 ? -diff : diff) <= (bound < 0 ? -bound : bound);
}

// True when the only differences are tolerated RCP/RSQRT lanes.
bool WithinTolerance(const Input& input, const Outcome& host, const Outcome& core)
{
    Approximation a;
    if (host.fault != core.fault || host.fault != rex86::FaultKind::kNone ||
        !ApproximationOf(input, &a))
    {
        return false;
    }
    Outcome patched = core;
    const unsigned base = 160 + 16 * a.destination;
    for (unsigned lane = 0; lane < (a.scalar ? 1u : 4u); ++lane)
    {
        std::uint32_t h = 0;
        std::uint32_t c = 0;
        std::memcpy(&h, host.out + base + 4 * lane, 4);
        std::memcpy(&c, core.out + base + 4 * lane, 4);
        if (!LaneTolerated(a, a.source[lane], h, c)) return false;
        std::memcpy(patched.out + base + 4 * lane, &h, 4);
    }
    return Difference(host, patched).empty();
}

// The trace case for a matching run: the core's own stub and regions, the
// host's results as the expectation, the uncompared bytes ignored.
rex86::trace::Case BuildCase(const Input& input, const Outcome& host)
{
    namespace trace = rex86::trace;
    trace::Case c;
    c.kind = trace::CaseKind::kSimd;
    c.mode = trace::RunMode::kUntilHalt;
    c.budget = 16;
    c.features = trace::kFeatureX87 | trace::kFeatureCmov | trace::kFeatureMmx |
        trace::kFeatureSse | trace::kFeatureFxsr;
    c.regions = {{CoreRunner::kCode, CoreRunner::kStack - CoreRunner::kCode,
                  static_cast<std::uint8_t>(rex86::kPageReadWriteExecute)}};
    c.fill_seed = input.eax ^ input.flags;
    c.patches = {{CoreRunner::kCode, Stub(input.code, 0xF4)},
                 {CoreRunner::kIn, std::vector<std::uint8_t>(input.image, input.image + kImageSize)},
                 {CoreRunner::kData, std::vector<std::uint8_t>(input.data, input.data + kDataSize)}};
    c.input.gpr = {input.eax, 0, input.flags, CoreRunner::kData,
                   CoreRunner::kStack, 0, CoreRunner::kOut, CoreRunner::kIn};
    c.input.eip = CoreRunner::kCode;
    c.input.eflags = rex86::CpuState{}.eflags;
    c.expected.gpr = c.input.gpr;
    c.gpr_mask = 0xFFu;
    c.eflags_mask = 0;
    if (host.fault != rex86::FaultKind::kNone)
    {
        c.reason = static_cast<std::uint8_t>(rex86::StopReason::kFault);
        c.fault_kind = static_cast<std::uint8_t>(host.fault);
        c.flags = trace::kCompareEip;
        c.expected.eip = CoreRunner::kCode + kInstructionOffset;
        // The PUSH before the fault left its slot written.
        c.ignores = {{CoreRunner::kStack - 4, 4}};
        return c;
    }
    c.reason = static_cast<std::uint8_t>(rex86::StopReason::kHalted);
    c.expected.gpr[0] = host.eax;
    c.expected.gpr[2] = (host.flags & 0x8D5u) | (input.flags & ~0x8D5u);
    std::vector<std::uint8_t> out(kStateSize);
    std::memcpy(out.data(), host.out, kStateSize);
    c.diffs = {{CoreRunner::kOut, out}};
    for (unsigned i = 0; i < kImageSize;)
    {
        if (host.in[i] == input.image[i])
        {
            ++i;
            continue;
        }
        unsigned end = i;
        while (end < kImageSize && host.in[end] != input.image[end]) ++end;
        c.diffs.push_back({CoreRunner::kIn + i, std::vector<std::uint8_t>(host.in + i, host.in + end)});
        i = end;
    }
    for (unsigned i = 0; i < kDataSize;)
    {
        if (host.data[i] == input.data[i])
        {
            ++i;
            continue;
        }
        unsigned end = i;
        while (end < kDataSize && host.data[end] != input.data[end]) ++end;
        c.diffs.push_back({CoreRunner::kData + i, std::vector<std::uint8_t>(host.data + i, host.data + end)});
        i = end;
    }
    c.ignores = {{CoreRunner::kOut + 6, 18}, {CoreRunner::kOut + 28, 4},
                 {CoreRunner::kOut + kStateSize, kImageSize - kStateSize},
                 {CoreRunner::kStack - 4, 4}};
    return c;
}

}  // namespace

int main(int argc, char** argv)
{
    std::uint64_t iterations = 20000;
    std::uint64_t seed = 1;
    bool verbose = false;
    std::string record_path;
    std::string only;
    int positional = 0;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--verbose") == 0)
        {
            verbose = true;
        }
        else if (std::strcmp(argv[i], "--record") == 0 && i + 1 < argc)
        {
            record_path = argv[++i];
        }
        else if (std::strcmp(argv[i], "--only") == 0 && i + 1 < argc)
        {
            only = argv[++i];
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

    std::vector<Form> forms = BuildForms();
    if (!only.empty())
    {
        const std::string list = "," + only + ",";
        forms.erase(std::remove_if(forms.begin(), forms.end(),
                                   [&](const Form& f) {
                                       const std::string name =
                                           "," + f.mnemonic.substr(0, f.mnemonic.find(' ')) + ",";
                                       return list.find(name) == std::string::npos;
                                   }),
                    forms.end());
        if (forms.empty())
        {
            std::fprintf(stderr, "no form named %s\n", only.c_str());
            return 2;
        }
    }
    Generator gen(seed);
    HostRunner host;
    CoreRunner core;
    rex86::trace::Writer record;
    std::string error;
    if (!record_path.empty() && !record.Open(record_path, &error))
    {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 2;
    }
    std::map<std::string, std::uint64_t> failures;
    std::map<int, std::uint64_t> faults;
    std::uint64_t mismatches = 0;
    std::uint64_t within_tolerance = 0;
    for (std::uint64_t n = 0; n < iterations; ++n)
    {
        const Form& form = forms[gen.Bits(32) % forms.size()];
        Input input;
        input.code = form.bytes;
        if (form.immediate >= 0)
        {
            input.code[static_cast<std::size_t>(form.immediate)] = static_cast<std::uint8_t>(gen.Bits(8));
        }
        gen.Image(input.image);
        gen.Data(input.data, form.mxcsr_load);
        input.flags = static_cast<std::uint32_t>(0x202u | (gen.Bits(12) & 0x8D5u));
        input.eax = static_cast<std::uint32_t>(gen.Bits(32));

        const Outcome expected = host.Run(input);
        const Outcome actual = core.Run(input);
        std::string difference = Difference(expected, actual);
        bool tolerated = false;
        if (!difference.empty() && WithinTolerance(input, expected, actual))
        {
            ++within_tolerance;
            difference.clear();
            tolerated = true;  // a trace holds one expectation: not recorded
        }
        if (difference.empty())
        {
            ++faults[static_cast<int>(expected.fault)];
        }
        if (difference.empty() && !tolerated && !record_path.empty())
        {
            const rex86::trace::Case c = BuildCase(input, expected);
            const rex86::trace::ReplayResult replayed = rex86::trace::Replay(c);
            if (!replayed.matched)
            {
                difference = "recorded case does not replay: " + replayed.difference;
            }
            record.Add(c);
        }
        if (!difference.empty())
        {
            ++mismatches;
            if (failures[form.mnemonic]++ < 3 || verbose)
            {
                std::printf("MISMATCH %s: %s\n  code=%s data=%s eax=%08X flags=%08X\n  image=%s\n",
                            form.mnemonic.c_str(), difference.c_str(),
                            Hex(input.code.data(), static_cast<unsigned>(input.code.size())).c_str(),
                            Hex(input.data, kDataSize).c_str(), input.eax, input.flags,
                            Hex(input.image, kStateSize).c_str());
            }
        }
    }
    if (!record_path.empty())
    {
        record.Close();
    }
    std::printf("forms=%zu iterations=%llu seed=%llu mismatches=%llu within_tolerance=%llu\n",
                forms.size(), static_cast<unsigned long long>(iterations),
                static_cast<unsigned long long>(seed), static_cast<unsigned long long>(mismatches),
                static_cast<unsigned long long>(within_tolerance));
    std::printf("faults:");
    for (const auto& [kind, count] : faults)
    {
        std::printf(" %d=%llu", kind, static_cast<unsigned long long>(count));
    }
    std::printf("\n");
    for (const auto& [mnemonic, count] : failures)
    {
        std::printf("  %-24s %llu\n", mnemonic.c_str(), static_cast<unsigned long long>(count));
    }
    return mismatches == 0 ? 0 : 1;
}
