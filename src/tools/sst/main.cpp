// rex86_sst: runs the SingleStepTests/80386 suite (MIT, hardware-generated
// on a real 386EX) against the core. Stage 1 validates the decoder: every
// test's instruction bytes must decode, with the decoded length equal to
// the recorded byte count. Stage 2 (the interpreter task) will apply the
// initial state, execute and compare the final state under the undefined
// masks.
//
// The suite stays outside the repository; this tool takes a directory of
// gunzipped .MOO files or a single file. See design #7 and
// docs/guides/singlesteptests.md.

#include "decode/decoder.h"
#include "interp/access.h"
#include "interp/interpreter.h"
#include "rex86/cpu_state.h"
#include "rex86/environment.h"
#include "rex86/guest_memory.h"
#include "tools/sst/moo_reader.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace
{

struct Totals
{
    std::uint64_t files = 0;
    std::uint64_t parse_failures = 0;
    std::uint64_t tests = 0;
    std::uint64_t decode_failures = 0;
    std::uint64_t length_mismatches = 0;
    std::uint64_t expected_ud = 0;          // decode refusals matching #UD
    std::uint64_t mnemonic_mismatches = 0;  // informational only

    // --execute (stage 2).
    std::uint64_t executed = 0;
    std::uint64_t exec_passed = 0;
    std::uint64_t exec_mismatches = 0;
    std::uint64_t skipped_unimplemented = 0;
    std::uint64_t skipped_exception = 0;
    std::uint64_t skipped_boundary = 0;  // INT n / port I/O host delegation
    std::uint64_t skipped_unrepresentable = 0;  // >16MiB: 24-bit bus wrap
    std::uint64_t skipped_hw_quirk = 0;  // 386EX deviates from the SDM
};

// The execute harness: a real-mode-style machine the suite's tests run on.
// 16 MiB covers the 386EX's 24 address lines.
class ExecuteHarness final : public rex86::Environment
{
public:
    ExecuteHarness()
        : buffer_(16u * 1024u * 1024u, 0),
          memory_(buffer_.data(), static_cast<std::uint32_t>(buffer_.size()))
    {
        memory_.pages().Set(0, static_cast<std::uint32_t>(buffer_.size()),
                            rex86::kPageReadWriteExecute);
    }

    bool LoadDescriptor(std::uint16_t selector,
                        rex86::Descriptor* descriptor) override
    {
        // Real-mode base, but the 4 GiB limit the 386EX test rig shows:
        // tests access 32-bit effective addresses beyond 0xFFFF without
        // recording a fault, so the rig's descriptor caches are 'unreal'.
        descriptor->base = static_cast<std::uint32_t>(selector) << 4;
        descriptor->limit = 0xFFFFFFFFu;
        descriptor->present = true;
        descriptor->executable = true;
        descriptor->writable = true;
        descriptor->default_32bit = false;
        return true;
    }

    bool PortRead(std::uint16_t, std::uint8_t, std::uint32_t*) override
    {
        return false;
    }

    bool PortWrite(std::uint16_t, std::uint8_t, std::uint32_t) override
    {
        return false;
    }

    bool InterruptTarget(std::uint8_t, std::uint16_t*, std::uint32_t*) override
    {
        return false;
    }

    std::uint64_t ReadTimeStampCounter() override
    {
        return 0;
    }

    void Cpuid(std::uint32_t, std::uint32_t, std::uint32_t registers[4]) override
    {
        registers[0] = registers[1] = registers[2] = registers[3] = 0;
    }

    rex86::GuestMemory& memory()
    {
        return memory_;
    }

private:
    std::vector<std::uint8_t> buffer_;
    rex86::GuestMemory memory_;
};

void ApplySegment(rex86::CpuState* state, const rex86::Segment segment,
                  const std::uint32_t selector)
{
    rex86::SegmentRegister& seg = state->Seg(segment);
    seg.selector = static_cast<std::uint16_t>(selector);
    seg.base = (selector & 0xFFFFu) << 4;
    // See LoadDescriptor: the rig's caches carry a 4 GiB limit.
    seg.limit = 0xFFFFFFFFu;
    seg.present = true;
    seg.executable = true;
    seg.writable = true;
    seg.default_32bit = false;
}

// RG32 index -> where it lands in CpuState. Returns false for registers
// the user-mode core does not model (cr0, cr3, dr6, dr7).
bool ApplyRegister(rex86::CpuState* state, const std::size_t index,
                   const std::uint32_t value)
{
    using rex86::Gpr;
    using rex86::Segment;
    switch (index)
    {
        case rex86::sst::kRegEax: state->Set(Gpr::kEax, value); return true;
        case rex86::sst::kRegEbx: state->Set(Gpr::kEbx, value); return true;
        case rex86::sst::kRegEcx: state->Set(Gpr::kEcx, value); return true;
        case rex86::sst::kRegEdx: state->Set(Gpr::kEdx, value); return true;
        case rex86::sst::kRegEsi: state->Set(Gpr::kEsi, value); return true;
        case rex86::sst::kRegEdi: state->Set(Gpr::kEdi, value); return true;
        case rex86::sst::kRegEbp: state->Set(Gpr::kEbp, value); return true;
        case rex86::sst::kRegEsp: state->Set(Gpr::kEsp, value); return true;
        case rex86::sst::kRegEip: state->eip = value; return true;
        case rex86::sst::kRegEflags:
            // The 386's EFLAGS has no bits above VM (17); the generator's
            // raw initial values may carry junk there that the real
            // register dropped, so drop it here too.
            state->eflags = (value & 0x0003FFFFu) | 0x2u;
            return true;
        case rex86::sst::kRegCs: ApplySegment(state, Segment::kCs, value); return true;
        case rex86::sst::kRegDs: ApplySegment(state, Segment::kDs, value); return true;
        case rex86::sst::kRegEs: ApplySegment(state, Segment::kEs, value); return true;
        case rex86::sst::kRegFs: ApplySegment(state, Segment::kFs, value); return true;
        case rex86::sst::kRegGs: ApplySegment(state, Segment::kGs, value); return true;
        case rex86::sst::kRegSs: ApplySegment(state, Segment::kSs, value); return true;
        default: return false;
    }
}

bool ReadBackRegister(const rex86::CpuState& state, const std::size_t index,
                      std::uint32_t* value)
{
    using rex86::Gpr;
    using rex86::Segment;
    switch (index)
    {
        case rex86::sst::kRegEax: *value = state.Get(Gpr::kEax); return true;
        case rex86::sst::kRegEbx: *value = state.Get(Gpr::kEbx); return true;
        case rex86::sst::kRegEcx: *value = state.Get(Gpr::kEcx); return true;
        case rex86::sst::kRegEdx: *value = state.Get(Gpr::kEdx); return true;
        case rex86::sst::kRegEsi: *value = state.Get(Gpr::kEsi); return true;
        case rex86::sst::kRegEdi: *value = state.Get(Gpr::kEdi); return true;
        case rex86::sst::kRegEbp: *value = state.Get(Gpr::kEbp); return true;
        case rex86::sst::kRegEsp: *value = state.Get(Gpr::kEsp); return true;
        case rex86::sst::kRegEip: *value = state.eip; return true;
        case rex86::sst::kRegEflags: *value = state.eflags; return true;
        case rex86::sst::kRegCs: *value = state.Seg(Segment::kCs).selector; return true;
        case rex86::sst::kRegDs: *value = state.Seg(Segment::kDs).selector; return true;
        case rex86::sst::kRegEs: *value = state.Seg(Segment::kEs).selector; return true;
        case rex86::sst::kRegFs: *value = state.Seg(Segment::kFs).selector; return true;
        case rex86::sst::kRegGs: *value = state.Seg(Segment::kGs).selector; return true;
        case rex86::sst::kRegSs: *value = state.Seg(Segment::kSs).selector; return true;
        default: return false;
    }
}

const char* RegisterName(const std::size_t index)
{
    static const char* const kNames[] = {
        "cr0", "cr3", "eax", "ebx", "ecx", "edx", "esi", "edi", "ebp",
        "esp", "cs", "ds", "es", "fs", "gs", "ss", "eip", "eflags",
        "dr6", "dr7"};
    return index < 20 ? kNames[index] : "?";
}

// Runs one test on the harness and compares the final state. Returns
// through the totals; prints the first mismatches when verbose.
void ExecuteTest(ExecuteHarness* harness, const rex86::sst::MooFile& file,
                 const rex86::sst::MooTest& test, const bool verbose,
                 const std::string& file_name, Totals* totals)
{
    if (test.has_exception)
    {
        // Exception semantics (real-mode IVT frames) are a later
        // increment; see design #11.
        ++totals->skipped_exception;
        return;
    }

    {
        // 386EX quirk: a SIB with no index register (index field 100) but
        // a nonzero scale applies the scale to the BASE (EA = base*scale
        // + disp), where the SDM says the scale is ignored. Verified
        // numerically on lea/ALU tests; the core follows the SDM, so
        // these encodings are counted apart, not compared. See
        // docs/analysis/singlesteptests-386ex-deviations.md.
        const rex86::decode::Decoder decoder16(
            rex86::decode::Decoder::Mode::kLegacy16);
        rex86::decode::DecodedInstruction decoded;
        if (decoder16.Decode(test.bytes.data(), test.bytes.size(), 0,
                             &decoded) &&
            (decoded.instruction.attributes & ZYDIS_ATTRIB_HAS_SIB) != 0 &&
            decoded.instruction.raw.sib.index == 4 &&
            decoded.instruction.raw.sib.scale != 0)
        {
            ++totals->skipped_hw_quirk;
            return;
        }
    }

    rex86::CpuState state;
    state.Reset();
    for (std::size_t index = 0; index < rex86::sst::RegisterSet32::kCount;
         ++index)
    {
        if (test.initial.regs.Has(index))
        {
            ApplyRegister(&state, index, test.initial.regs.values[index]);
        }
    }
    for (const rex86::sst::RamEntry& entry : test.initial.ram)
    {
        harness->memory().Write8(entry.address, entry.value);
    }

    // The generator runs the tested instruction AND the injected HLT (a
    // HLT also sits at every branch target), capturing the final state on
    // the HALT. So: step until a HLT retires, which normally takes two
    // steps. The cap only guards a runaway.
    const rex86::Features features;
    rex86::interp::StepResult step;
    int instructions = 0;
    std::uint32_t eip_after_first = 0;
    for (;;)
    {
        step = rex86::interp::Step(state, harness->memory(), *harness,
                                   features);
        ++instructions;
        if (instructions == 1)
        {
            eip_after_first = state.eip;
        }
        if (step.status != rex86::interp::StepStatus::kRetired ||
            instructions > 32)
        {
            break;
        }
    }

    bool failed = false;
    std::string failure;
    if (step.status == rex86::interp::StepStatus::kUnimplemented &&
        instructions == 1)
    {
        ++totals->skipped_unimplemented;
    }
    else if (step.status == rex86::interp::StepStatus::kRetiredAndStopped &&
             step.event.reason != rex86::StopReason::kHalted)
    {
        // INT n and port I/O stop for the host by design; the hardware
        // completed them itself, so the final states are incomparable.
        ++totals->skipped_boundary;
    }
    else if (step.status == rex86::interp::StepStatus::kFaulted)
    {
        if (step.event.fault_kind == rex86::FaultKind::kAccessViolation &&
            step.event.fault_address >= 16u * 1024u * 1024u)
        {
            // The rig's physical bus wraps at 24 bits; a linear address
            // beyond 16 MiB is unrepresentable in this flat harness.
            ++totals->skipped_unrepresentable;
        }
        else
        {
            failed = true;
            char text[96];
            std::snprintf(text, sizeof text,
                          "unexpected fault kind=%d address=0x%08X",
                          static_cast<int>(step.event.fault_kind),
                          step.event.fault_address);
            failure = text;
        }
    }
    else if (step.status != rex86::interp::StepStatus::kRetiredAndStopped)
    {
        // A runaway or an unimplemented stop after the first instruction:
        // the tested instruction steered execution somewhere wrong.
        failed = true;
        char text[96];
        std::snprintf(text, sizeof text,
                      "did not reach the trailing hlt (status=%d steps=%d "
                      "eip=0x%08X eip_after_first=0x%08X)",
                      static_cast<int>(step.status), instructions, state.eip,
                      eip_after_first);
        failure = text;
    }
    else
    {
        ++totals->executed;
        // Registers: only those FINA carries, undefined bits removed by
        // the RM32 masks (a set mask bit marks an undefined bit).
        for (std::size_t index = 0;
             index < rex86::sst::RegisterSet32::kCount && !failed; ++index)
        {
            if (!test.final_state.regs.Has(index))
            {
                continue;
            }
            std::uint32_t actual = 0;
            if (!ReadBackRegister(state, index, &actual))
            {
                continue;  // cr0/cr3/dr6/dr7: not modeled
            }
            std::uint32_t undefined = 0;
            if (index == rex86::sst::kRegEflags)
            {
                // The SMM register dump reports the 386's nonexistent
                // EFLAGS bits (18-31) as ones, while the architectural
                // register pushes them as zeros (PUSHFD image). Compare
                // only the bits the register has.
                undefined |= 0xFFFC0000u;
            }
            if (file.has_file_masks && file.file_masks.Has(index))
            {
                undefined |= file.file_masks.values[index];
            }
            if (test.final_state.has_masks &&
                test.final_state.masks.Has(index))
            {
                undefined |= test.final_state.masks.values[index];
            }
            const std::uint32_t expected = test.final_state.regs.values[index];
            if (((actual ^ expected) & ~undefined) != 0)
            {
                failed = true;
                char text[128];
                std::snprintf(text, sizeof text,
                              "%s actual=0x%08X expected=0x%08X mask=0x%08X",
                              RegisterName(index), actual, expected,
                              undefined);
                failure = text;
            }
        }
        for (const rex86::sst::RamEntry& entry : test.final_state.ram)
        {
            if (failed)
            {
                break;
            }
            std::uint8_t actual = 0;
            harness->memory().Read8(entry.address, &actual);
            if (actual != entry.value)
            {
                failed = true;
                char text[96];
                std::snprintf(text, sizeof text,
                              "ram[0x%08X] actual=0x%02X expected=0x%02X",
                              entry.address, actual, entry.value);
                failure = text;
            }
        }
        if (!failed)
        {
            ++totals->exec_passed;
        }
    }

    if (failed)
    {
        ++totals->exec_mismatches;
        if (verbose || totals->exec_mismatches <= 20)
        {
            std::cerr << file_name << " #" << test.index << " \""
                      << test.name << "\": " << failure << " bytes=";
            for (const std::uint8_t byte : test.bytes)
            {
                char hex[4];
                std::snprintf(hex, sizeof hex, "%02X", byte);
                std::cerr << hex;
            }
            // Our own idea of the first memory operand, from the initial
            // state, next to the hardware's EA record.
            {
                rex86::CpuState initial;
                initial.Reset();
                for (std::size_t index = 0;
                     index < rex86::sst::RegisterSet32::kCount; ++index)
                {
                    if (test.initial.regs.Has(index))
                    {
                        ApplyRegister(&initial, index,
                                      test.initial.regs.values[index]);
                    }
                }
                const rex86::decode::Decoder decoder16(
                    rex86::decode::Decoder::Mode::kLegacy16);
                rex86::decode::DecodedInstruction decoded;
                if (decoder16.Decode(test.bytes.data(), test.bytes.size(),
                                     initial.eip, &decoded))
                {
                    for (ZyanU8 i = 0; i < decoded.instruction.operand_count;
                         ++i)
                    {
                        const ZydisDecodedOperand& op = decoded.operands[i];
                        if (op.type != ZYDIS_OPERAND_TYPE_MEMORY)
                        {
                            continue;
                        }
                        rex86::interp::Ctx probe{initial, harness->memory(),
                                                 *harness, decoded,
                                                 rex86::Event{}, false};
                        const std::uint32_t offset =
                            rex86::interp::EffectiveAddress(probe, op);
                        char ours[96];
                        std::snprintf(
                            ours, sizeof ours,
                            " [ours seg=%d offset=0x%08X linear=0x%08X]",
                            static_cast<int>(
                                rex86::interp::SegmentOf(decoded, op)),
                            offset,
                            initial
                                    .Seg(rex86::interp::SegmentOf(decoded,
                                                                  op))
                                    .base +
                                offset);
                        std::cerr << ours;
                        break;
                    }
                }
            }
            if (test.initial.has_ea)
            {
                char ea[96];
                std::snprintf(ea, sizeof ea,
                              " [hw ea offset=0x%08X l=0x%08X p=0x%08X]",
                              test.initial.ea.offset,
                              test.initial.ea.linear_address,
                              test.initial.ea.physical_address);
                std::cerr << ea;
            }
            std::cerr << "\n";
        }
    }

    // Revert only what this test touched, so the next test starts clean
    // without clearing 16 MiB.
    for (const rex86::sst::RamEntry& entry : test.initial.ram)
    {
        harness->memory().Write8(entry.address, 0);
    }
    for (const rex86::sst::RamEntry& entry : test.final_state.ram)
    {
        harness->memory().Write8(entry.address, 0);
    }
}

bool ReadBinaryFile(const std::filesystem::path& path,
                    std::vector<std::uint8_t>* data)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        return false;
    }
    stream.seekg(0, std::ios::end);
    const std::streamoff size = stream.tellg();
    if (size < 0)
    {
        return false;
    }
    stream.seekg(0, std::ios::beg);
    data->resize(static_cast<std::size_t>(size));
    return size == 0 ||
        static_cast<bool>(
            stream.read(reinterpret_cast<char*>(data->data()), size));
}

// Zydis spells mnemonics in lower case; META uses upper case. Compare
// case-insensitively, ASCII only, since both sides are ASCII.
bool MnemonicsEqual(const std::string_view left, const std::string_view right)
{
    if (left.size() != right.size())
    {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index)
    {
        const char a = left[index];
        const char b = right[index];
        const char la = a >= 'A' && a <= 'Z' ? static_cast<char>(a + 32) : a;
        const char lb = b >= 'A' && b <= 'Z' ? static_cast<char>(b + 32) : b;
        if (la != lb)
        {
            return false;
        }
    }
    return true;
}

void RunFile(const std::filesystem::path& path,
             const rex86::decode::Decoder& decoder, const bool verbose,
             ExecuteHarness* harness, Totals* totals)
{
    ++totals->files;

    std::vector<std::uint8_t> data;
    if (!ReadBinaryFile(path, &data))
    {
        std::cerr << path.string() << ": read failed\n";
        ++totals->parse_failures;
        return;
    }

    rex86::sst::MooFile file;
    std::string error;
    if (!rex86::sst::ParseMooFile(data.data(), data.size(), &file, &error))
    {
        std::cerr << path.string() << ": parse failed: " << error << "\n";
        ++totals->parse_failures;
        return;
    }

    if (harness != nullptr)
    {
        const std::uint64_t before = totals->exec_mismatches;
        for (const rex86::sst::MooTest& test : file.tests)
        {
            ++totals->tests;
            ExecuteTest(harness, file, test, verbose,
                        path.filename().string(), totals);
        }
        if (totals->exec_mismatches != before)
        {
            std::cout << path.filename().string() << " mnemonic="
                      << file.mnemonic << " mismatches="
                      << (totals->exec_mismatches - before) << "\n";
        }
        return;
    }

    std::uint64_t file_decode_failures = 0;
    std::uint64_t file_length_mismatches = 0;
    for (const rex86::sst::MooTest& test : file.tests)
    {
        ++totals->tests;
        const std::uint32_t eip = test.initial.regs.Has(rex86::sst::kRegEip)
            ? test.initial.regs.values[rex86::sst::kRegEip]
            : 0;

        // BYTS holds the tested instruction followed by the HLT the test
        // generator injects to detect the instruction boundary (confirmed
        // empirically; see the work log). The tested instruction therefore
        // ends one byte early whenever the final byte is HLT (0xF4) -- and
        // when the instruction's own last byte is 0xF4 the arithmetic is
        // identical, so no case is lost.
        const bool ends_with_hlt =
            !test.bytes.empty() && test.bytes.back() == 0xF4;
        const std::size_t expected_length =
            ends_with_hlt && test.bytes.size() > 1 ? test.bytes.size() - 1
                                                   : test.bytes.size();

        rex86::decode::DecodedInstruction decoded;
        if (!decoder.Decode(test.bytes.data(), test.bytes.size(), eip,
                            &decoded))
        {
            // The hardware raises #UD for encodings such as LOCK on a
            // non-lockable instruction, and the suite records exception 6
            // for them. A decode refusal there is agreement, not failure.
            if (test.has_exception && test.exception.number == 6)
            {
                ++totals->expected_ud;
                continue;
            }
            ++totals->decode_failures;
            ++file_decode_failures;
            if (verbose)
            {
                std::cerr << path.filename().string() << " #" << test.index
                          << " \"" << test.name << "\": decode failed\n";
            }
            continue;
        }
        if (decoded.Length() != expected_length)
        {
            ++totals->length_mismatches;
            ++file_length_mismatches;
            if (verbose)
            {
                std::cerr << path.filename().string() << " #" << test.index
                          << " \"" << test.name << "\": length "
                          << decoded.Length() << " != "
                          << expected_length << "\n";
            }
            continue;
        }
        if (!file.mnemonic.empty() &&
            !MnemonicsEqual(decoded.MnemonicName(), file.mnemonic))
        {
            // Informational: Zydis and the suite spell some mnemonics
            // differently (e.g. jz/je), which is not a decoder defect.
            ++totals->mnemonic_mismatches;
        }
    }

    if (verbose || file_decode_failures != 0 || file_length_mismatches != 0)
    {
        std::cout << path.filename().string() << " cpu=" << file.cpu_id
                  << " mnemonic=" << file.mnemonic
                  << " tests=" << file.tests.size()
                  << " decode_failures=" << file_decode_failures
                  << " length_mismatches=" << file_length_mismatches << "\n";
    }
}

void PrintUsage()
{
    std::cerr << "usage: rex86_sst <dir-or-file.MOO> [--execute] [--verbose]\n"
                 "Validates the core against SingleStepTests MOO files\n"
                 "(gunzipped). A directory is scanned for *.MOO files.\n"
                 "Default: decoder validation. --execute: run each test on\n"
                 "the interpreter and compare the final state.\n";
}

}  // namespace

int main(int argc, char** argv)
{
    std::filesystem::path input;
    bool verbose = false;
    bool execute = false;
    for (int index = 1; index < argc; ++index)
    {
        const std::string_view argument(argv[index]);
        if (argument == "--verbose")
        {
            verbose = true;
        }
        else if (argument == "--execute")
        {
            execute = true;
        }
        else if (input.empty())
        {
            input = std::filesystem::path(argument);
        }
        else
        {
            PrintUsage();
            return 2;
        }
    }
    if (input.empty())
    {
        PrintUsage();
        return 2;
    }

    // Real mode is the suite's complete section, and its default operand
    // size is 16 bits.
    const rex86::decode::Decoder decoder(
        rex86::decode::Decoder::Mode::kLegacy16);

    Totals totals;
    std::unique_ptr<ExecuteHarness> harness;
    if (execute)
    {
        harness = std::make_unique<ExecuteHarness>();
    }
    std::error_code ec;
    if (std::filesystem::is_directory(input, ec))
    {
        std::vector<std::filesystem::path> files;
        for (const auto& entry :
             std::filesystem::directory_iterator(input, ec))
        {
            if (entry.is_regular_file() &&
                entry.path().extension() == ".MOO")
            {
                files.push_back(entry.path());
            }
        }
        std::sort(files.begin(), files.end());
        for (const std::filesystem::path& path : files)
        {
            RunFile(path, decoder, verbose, harness.get(), &totals);
        }
    }
    else
    {
        RunFile(input, decoder, verbose, harness.get(), &totals);
    }

    if (execute)
    {
        std::cout << "files=" << totals.files
                  << " parse_failures=" << totals.parse_failures
                  << " tests=" << totals.tests
                  << " executed=" << totals.executed
                  << " passed=" << totals.exec_passed
                  << " mismatches=" << totals.exec_mismatches
                  << " skipped_unimplemented="
                  << totals.skipped_unimplemented
                  << " skipped_exception=" << totals.skipped_exception
                  << " skipped_boundary=" << totals.skipped_boundary
                  << " skipped_unrepresentable="
                  << totals.skipped_unrepresentable
                  << " skipped_hw_quirk=" << totals.skipped_hw_quirk
                  << "\n";
        const bool failed = totals.parse_failures != 0 ||
            totals.exec_mismatches != 0 || totals.files == 0;
        return failed ? 1 : 0;
    }

    std::cout << "files=" << totals.files
              << " parse_failures=" << totals.parse_failures
              << " tests=" << totals.tests
              << " decode_failures=" << totals.decode_failures
              << " length_mismatches=" << totals.length_mismatches
              << " expected_ud=" << totals.expected_ud
              << " mnemonic_mismatches=" << totals.mnemonic_mismatches
              << "\n";
    const bool failed = totals.parse_failures != 0 ||
        totals.decode_failures != 0 || totals.length_mismatches != 0 ||
        totals.files == 0;
    return failed ? 1 : 0;
}
