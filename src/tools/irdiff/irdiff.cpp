#include "tools/irdiff/irdiff.h"

#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

#include "decode/decoder.h"
#include "interp/interpreter.h"
#include "rex86/cpu_state.h"
#include "rex86/environment.h"
#include "rex86/guest_memory.h"
#include "translate/ir/evaluator.h"
#include "translate/ir/frontend.h"
#include "translate/ir/optimize.h"

namespace rex86::irdiff
{

namespace
{

// The guest layout: a page of each kind the checks distinguish.
constexpr std::uint32_t kMemorySize = 0x10000;
constexpr std::uint32_t kCode = 0x1000;      // two pages, read-execute
constexpr std::uint32_t kCodeEnd = 0x3000;   // unmapped after the code
constexpr std::uint32_t kData = 0x4000;      // four pages, read-write
constexpr std::uint32_t kDataSize = 0x4000;
constexpr std::uint32_t kReadOnly = 0x8000;  // read-only
constexpr std::uint32_t kStack = 0xA000;     // two pages, read-write
constexpr std::uint32_t kStackTop = 0xB000;
constexpr std::uint32_t kTranslatedPage = 0xC000;  // read-write, kTranslated

class NullEnvironment final : public Environment
{
public:
    bool LoadDescriptor(std::uint16_t, Descriptor* descriptor) override
    {
        *descriptor = Descriptor{};
        return true;
    }
    bool PortRead(std::uint16_t, std::uint8_t, std::uint32_t*) override { return false; }
    bool PortWrite(std::uint16_t, std::uint8_t, std::uint32_t) override { return false; }
    bool InterruptTarget(std::uint8_t, std::uint16_t*, std::uint32_t*) override { return false; }
    std::uint64_t ReadTimeStampCounter() override { return 0; }
    void Cpuid(std::uint32_t, std::uint32_t, std::uint32_t registers[4]) override
    {
        registers[0] = registers[1] = registers[2] = registers[3] = 0;
    }
};

// A guest: memory, its page table and the CPU state, copied whole.
struct Machine
{
    std::vector<std::uint8_t> bytes;
    GuestMemory memory;
    CpuState state;

    Machine() : bytes(kMemorySize, 0), memory(bytes.data(), kMemorySize) {}
    Machine(const Machine& other)
        : bytes(other.bytes), memory(bytes.data(), kMemorySize), state(other.state)
    {
        memory.pages() = other.memory.pages();
    }
    Machine& operator=(const Machine&) = delete;
};

class Generator
{
public:
    explicit Generator(const std::uint64_t seed) : random_(seed) {}

    std::uint32_t Next() { return static_cast<std::uint32_t>(random_()); }
    std::uint32_t Below(const std::uint32_t limit) { return Next() % limit; }
    bool OneIn(const std::uint32_t n) { return Below(n) == 0; }

    // A fresh machine: random data, random registers and flags, mostly
    // flat segments with an occasional limit or absent segment.
    void FillMachine(Machine* m)
    {
        for (std::uint32_t i = 0; i < kMemorySize; i += 8)
        {
            const std::uint64_t word = random_();
            std::memcpy(&m->bytes[i], &word, 8);
        }
        PageAttributeTable& pages = m->memory.pages();
        pages.Set(kCode, kCodeEnd - kCode, kPageReadExecute);
        pages.Set(kData, kDataSize, kPageReadWrite);
        pages.Set(kReadOnly, kGuestPageSize, PageFlag::kMapped | PageFlag::kRead);
        pages.Set(kStack, kStackTop - kStack, kPageReadWrite);
        pages.Set(kTranslatedPage, kGuestPageSize, kPageReadWrite);
        pages.Add(kTranslatedPage, kGuestPageSize, PageFlag::kTranslated);

        CpuState& s = m->state;
        s = CpuState{};
        for (unsigned r = 0; r < 8; ++r)
        {
            s.gpr[r] = RegisterValue();
        }
        s.gpr[static_cast<unsigned>(Gpr::kEsp)] =
            OneIn(5) ? RegisterValue() : kStackTop - 0x100u + (Below(0x40) * 4u);
        s.eflags = kEflagsReserved1 | kEflagsInterrupt |
                   (Next() & (kEflagsCarry | kEflagsParity | kEflagsAdjust | kEflagsZero |
                              kEflagsSign | kEflagsOverflow));
        for (unsigned i = 0; i < 6; ++i)
        {
            SegmentRegister& seg = s.segments[i];
            seg = SegmentRegister{};
            seg.selector = 0x23;
        }
        SegmentRegister& cs = s.Seg(Segment::kCs);
        cs.selector = 0x1B;
        cs.executable = true;
        cs.writable = false;
        if (OneIn(8))
        {
            SegmentRegister& ds = s.Seg(Segment::kDs);
            ds.base = kData;
            ds.limit = OneIn(2) ? 0x0FFFu : 0x3FFFu;
        }
        if (OneIn(16))
        {
            SegmentRegister& fs = s.Seg(Segment::kFs);
            fs.selector = 0;
            fs.limit = 0;
            fs.present = false;
        }
        if (OneIn(16))
        {
            s.Seg(Segment::kSs).limit = kStackTop - 0x80u;  // non-flat, expand-up
        }
        if (OneIn(16))
        {
            s.Seg(Segment::kEs).writable = false;
        }
    }

    std::uint32_t RegisterValue()
    {
        switch (Below(10))
        {
            case 0: case 1: case 2: case 3: return kData + Below(kDataSize);
            case 4: return kStack + Below(kStackTop - kStack);
            case 5: return kReadOnly + Below(0x5000);  // read-only to translated
            case 6: case 7: return Below(256);
            default: return Next();
        }
    }

    // Instruction bytes: an opcode the frontend covers, prefixes sometimes,
    // a ModRM biased toward SIB addressing and a SIB biased toward an ESP
    // base (POP m and PUSH m through ESP compute their addresses around
    // the stack update); displacements are often moved into the data.
    void Instruction(std::uint8_t* bytes, const decode::Decoder& decoder,
                     const std::vector<std::vector<std::uint8_t>>& opcodes)
    {
        static constexpr std::uint8_t kSegments[] = {0x26, 0x2E, 0x36, 0x3E, 0x64, 0x65};
        unsigned n = 0;
        if (OneIn(4)) bytes[n++] = 0x66;
        if (OneIn(8)) bytes[n++] = kSegments[Below(6)];
        if (OneIn(40)) bytes[n++] = 0x67;
        if (OneIn(40)) bytes[n++] = 0xF0;
        if (OneIn(40)) bytes[n++] = OneIn(2) ? 0xF2 : 0xF3;
        if (OneIn(16))
        {
            bytes[n++] = static_cast<std::uint8_t>(Next());  // anything at all
        }
        else
        {
            const std::vector<std::uint8_t>& opcode = opcodes[Below(static_cast<std::uint32_t>(opcodes.size()))];
            for (const std::uint8_t b : opcode)
            {
                bytes[n++] = b;
            }
        }
        for (unsigned i = n; i < interp::kFetchWindow; ++i)
        {
            bytes[i] = static_cast<std::uint8_t>(Next());
        }
        if (n + 1 < interp::kFetchWindow)
        {
            std::uint8_t& modrm = bytes[n];
            if (OneIn(3))
            {
                modrm = static_cast<std::uint8_t>((modrm & 0xF8u) | 0x04u);  // a SIB follows
                std::uint8_t& sib = bytes[n + 1];
                if (OneIn(2))
                {
                    sib = static_cast<std::uint8_t>((sib & 0xF8u) | 0x04u);  // base ESP
                }
                if (OneIn(2))
                {
                    sib = static_cast<std::uint8_t>((sib & 0xC7u) | 0x20u);  // no index
                }
            }
        }
        decode::DecodedInstruction d;
        if (decoder.Decode(bytes, interp::kFetchWindow, kCode, &d) &&
            d.instruction.raw.disp.size == 32 && !OneIn(4))
        {
            const std::uint32_t disp = kData + Below(kDataSize);
            std::memcpy(bytes + d.instruction.raw.disp.offset, &disp, 4);
        }
        else if (d.instruction.raw.disp.size == 8 && OneIn(2))
        {
            bytes[d.instruction.raw.disp.offset] = static_cast<std::uint8_t>(Below(0x40) * 4u);
        }
    }

    std::mt19937_64 random_;
};

bool IsControlTransfer(const decode::DecodedInstruction& d)
{
    return d.Flow() != decode::ControlFlow::kNone;
}

std::string Hex(const std::uint8_t* bytes, const unsigned count)
{
    std::string text;
    char piece[4];
    for (unsigned i = 0; i < count; ++i)
    {
        std::snprintf(piece, sizeof piece, "%02X ", bytes[i]);
        text += piece;
    }
    return text;
}

// The first difference between two machines, or an empty string.
std::string Difference(const Machine& expected, const Machine& actual)
{
    char line[160];
    for (unsigned r = 0; r < 8; ++r)
    {
        if (expected.state.gpr[r] != actual.state.gpr[r])
        {
            std::snprintf(line, sizeof line, "gpr[%u] expected=%08X actual=%08X", r,
                          static_cast<unsigned>(expected.state.gpr[r]),
                          static_cast<unsigned>(actual.state.gpr[r]));
            return line;
        }
    }
    if (expected.state.eip != actual.state.eip)
    {
        std::snprintf(line, sizeof line, "eip expected=%08X actual=%08X",
                      static_cast<unsigned>(expected.state.eip), static_cast<unsigned>(actual.state.eip));
        return line;
    }
    if (expected.state.eflags != actual.state.eflags)
    {
        std::snprintf(line, sizeof line, "eflags expected=%08X actual=%08X",
                      static_cast<unsigned>(expected.state.eflags),
                      static_cast<unsigned>(actual.state.eflags));
        return line;
    }
    if (std::memcmp(expected.bytes.data(), actual.bytes.data(), kMemorySize) != 0)
    {
        for (std::uint32_t i = 0; i < kMemorySize; ++i)
        {
            if (expected.bytes[i] != actual.bytes[i])
            {
                std::snprintf(line, sizeof line, "memory[%05X] expected=%02X actual=%02X",
                              static_cast<unsigned>(i), expected.bytes[i], actual.bytes[i]);
                return line;
            }
        }
    }
    for (std::uint32_t page = 0; page < kMemorySize; page += kGuestPageSize)
    {
        if (expected.memory.pages().Get(page) != actual.memory.pages().Get(page))
        {
            std::snprintf(line, sizeof line, "page flags at %05X", static_cast<unsigned>(page));
            return line;
        }
    }
    return {};
}

// The opcodes (one byte, or 0F and one byte) under which some ModRM
// decodes to an instruction the frontend covers.
std::vector<std::vector<std::uint8_t>> CoveredOpcodes(const decode::Decoder& decoder)
{
    std::vector<std::vector<std::uint8_t>> opcodes;
    const Features features;
    const auto consider = [&](const std::vector<std::uint8_t>& opcode) {
        for (unsigned modrm = 0; modrm < 256; modrm += 7)
        {
            std::uint8_t bytes[interp::kFetchWindow] = {};
            std::memcpy(bytes, opcode.data(), opcode.size());
            bytes[opcode.size()] = static_cast<std::uint8_t>(modrm);
            decode::DecodedInstruction d;
            if (decoder.Decode(bytes, sizeof bytes, kCode, &d) && translate::ir::Covered(d, features))
            {
                opcodes.push_back(opcode);
                return;
            }
        }
    };
    for (unsigned op = 0; op < 256; ++op)
    {
        if (op != 0x0F)
        {
            consider({static_cast<std::uint8_t>(op)});
        }
    }
    for (unsigned op = 0; op < 256; ++op)
    {
        consider({0x0F, static_cast<std::uint8_t>(op)});
    }
    return opcodes;
}

struct Runner
{
    explicit Runner(const Options& o) : options(o), generator(o.seed), opcodes(CoveredOpcodes(decoder)) {}

    const Options& options;
    Generator generator;
    decode::Decoder decoder{decode::Decoder::Mode::kLegacy32};
    std::vector<std::vector<std::uint8_t>> opcodes;
    NullEnvironment environment;
    Features features;
    Stats stats;

    void Fail(const std::string& what, const std::uint8_t* code, const unsigned length,
              const translate::ir::Block& block)
    {
        ++stats.mismatches;
        if (stats.first_failure.empty())
        {
            stats.first_failure = what + "\ncode: " + Hex(code, length) + "\n" +
                                  translate::ir::Dump(block);
        }
        if (options.verbose)
        {
            std::fprintf(stderr, "MISMATCH %s\n", what.c_str());
        }
    }

    // A covered instruction that is not a control transfer unless allowed.
    bool Covered(std::uint8_t* bytes, const bool allow_branch, decode::DecodedInstruction* d)
    {
        for (int attempt = 0; attempt < 4000; ++attempt)
        {
            generator.Instruction(bytes, decoder, opcodes);
            if (decoder.Decode(bytes, interp::kFetchWindow, kCode, d) &&
                translate::ir::Covered(*d, features) && (allow_branch || !IsControlTransfer(*d)))
            {
                return true;
            }
        }
        return false;
    }

    // Runs one evaluation of `block` on a copy of `start` and compares it
    // with the interpreter running as many steps on another copy.
    void Compare(const Machine& start, const translate::ir::Block& block, const std::uint8_t* code,
                 const unsigned length, const char* label)
    {
        Machine evaluated(start);
        const translate::ir::ExitResult exit =
            translate::ir::Evaluate(block, evaluated.state, evaluated.memory);
        Machine interpreted(start);
        for (std::uint32_t i = 0; i < exit.steps; ++i)
        {
            const interp::StepResult step =
                interp::Step(interpreted.state, interpreted.memory, environment, features);
            if (step.status != interp::StepStatus::kRetired)
            {
                Fail(std::string(label) + ": the evaluator completed an instruction the interpreter did not retire",
                     code, length, block);
                return;
            }
        }
        const std::string difference = Difference(interpreted, evaluated);
        if (!difference.empty())
        {
            Fail(std::string(label) + ": " + difference, code, length, block);
        }
    }
};

}  // namespace

Stats RunForms(const Options& options)
{
    Runner runner(options);
    for (std::uint64_t c = 0; c < options.cases; ++c)
    {
        ++runner.stats.cases;
        runner.features.cmov = !runner.generator.OneIn(16);
        Machine start;
        runner.generator.FillMachine(&start);
        // Mostly inside the code pages, sometimes up against their end so
        // that the fetch runs out.
        const std::uint32_t at = runner.generator.OneIn(16)
            ? kCodeEnd - 1u - runner.generator.Below(8)
            : kCode + runner.generator.Below(kCodeEnd - kCode - 16);
        std::uint8_t bytes[interp::kFetchWindow] = {};
        decode::DecodedInstruction d;
        if (!runner.Covered(bytes, true, &d))
        {
            continue;
        }
        const unsigned room = std::min<unsigned>(interp::kFetchWindow, kCodeEnd - at);
        std::memcpy(&start.bytes[at], bytes, room);
        start.state.eip = at;

        translate::ir::FrontendOptions frontend;
        frontend.max_instructions = 1;
        translate::ir::Block block;
        if (!translate::ir::FormBlock(start.state, start.memory, runner.features, frontend, &block))
        {
            continue;  // the fetch ran out, or a disabled feature
        }
        translate::ir::Block optimized = block;
        translate::ir::Optimize(&optimized);

        Machine probe(start);
        const translate::ir::ExitResult exit =
            translate::ir::Evaluate(block, probe.state, probe.memory);
        if (exit.steps == 0)
        {
            ++runner.stats.exited;
            // An exit before the instruction leaves the state untouched.
            Machine untouched(start);
            untouched.state.eip = exit.eip;
            const std::string difference = Difference(untouched, probe);
            if (!difference.empty() || exit.eip != at)
            {
                runner.Fail("exit changed the state: " + difference, bytes, d.Length(), block);
            }
        }
        else
        {
            ++runner.stats.compared;
            ++runner.stats.compared_by_mnemonic[d.MnemonicName()];
        }
        runner.Compare(start, block, bytes, d.Length(), "form");
        runner.Compare(start, optimized, bytes, d.Length(), "form optimized");
    }
    return runner.stats;
}

Stats RunBlocks(const Options& options)
{
    Runner runner(options);
    for (std::uint64_t c = 0; c < options.cases; ++c)
    {
        ++runner.stats.cases;
        Machine start;
        runner.generator.FillMachine(&start);
        const unsigned count = 1 + runner.generator.Below(options.max_block);
        std::uint32_t at = kCode + runner.generator.Below(0x800);
        start.state.eip = at;
        std::vector<std::uint8_t> code;
        for (unsigned i = 0; i < count; ++i)
        {
            std::uint8_t bytes[interp::kFetchWindow] = {};
            decode::DecodedInstruction d;
            if (!runner.Covered(bytes, i + 1 == count, &d))
            {
                break;
            }
            std::memcpy(&start.bytes[at], bytes, d.Length());
            code.insert(code.end(), bytes, bytes + d.Length());
            at += d.Length();
        }
        translate::ir::Block block;
        if (code.empty() ||
            !translate::ir::FormBlock(start.state, start.memory, runner.features,
                                      translate::ir::FrontendOptions{}, &block))
        {
            continue;
        }
        translate::ir::Block optimized = block;
        translate::ir::Optimize(&optimized);
        Machine probe(start);
        if (translate::ir::Evaluate(block, probe.state, probe.memory).steps == 0)
        {
            ++runner.stats.exited;
        }
        else
        {
            ++runner.stats.compared;
        }
        runner.Compare(start, block, code.data(), static_cast<unsigned>(code.size()), "block");
        runner.Compare(start, optimized, code.data(), static_cast<unsigned>(code.size()),
                       "block optimized");
    }
    return runner.stats;
}

}  // namespace rex86::irdiff
