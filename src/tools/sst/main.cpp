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
#include "tools/sst/moo_reader.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
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
};

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
             Totals* totals)
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
    std::cerr << "usage: rex86_sst <dir-or-file.MOO> [--verbose]\n"
                 "Validates the decoder against SingleStepTests MOO files\n"
                 "(gunzipped). A directory is scanned for *.MOO files.\n";
}

}  // namespace

int main(int argc, char** argv)
{
    std::filesystem::path input;
    bool verbose = false;
    for (int index = 1; index < argc; ++index)
    {
        const std::string_view argument(argv[index]);
        if (argument == "--verbose")
        {
            verbose = true;
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
            RunFile(path, decoder, verbose, &totals);
        }
    }
    else
    {
        RunFile(input, decoder, verbose, &totals);
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
