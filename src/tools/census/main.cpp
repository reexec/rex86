// rex86_census: counts the x86 instructions in a flat code image, from
// explicit entry points. The lower bound is recursive descent, the upper
// bound a linear sweep, and the gap between them is what is honestly known.
//
// The image is a dump the consumer produced: rePIU dumps its relocated LE
// image, re2DJ dumps its decrypted PE sections. This tool knows no guest
// format, which is the boundary rule that lets it live in the core repo.
//
// See docs/design/20261007-i005-decoder-and-census.md and
// docs/guides/instruction-census.md.

#include "tools/census/census.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <utility>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace
{

void PrintUsage()
{
    std::cerr
        << "usage: rex86_census <image.bin> [--base ADDR] --entry ADDR\n"
           "                    [--entry ADDR]... [--exec START:LENGTH]...\n"
           "Counts the x86 instructions reachable from the entry points of a\n"
           "flat 32-bit code image. ADDR, START and LENGTH accept 0x prefixes.\n";
}

bool ParseUint32(const std::string_view text, std::uint32_t* value)
{
    if (text.empty())
    {
        return false;
    }
    const std::string owned(text);
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(owned.c_str(), &end, 0);
    if (end == owned.c_str() || *end != '\0' || parsed > 0xFFFFFFFFULL)
    {
        return false;
    }
    *value = static_cast<std::uint32_t>(parsed);
    return true;
}

bool ParseExecRange(const std::string_view text,
                    rex86::census::ExecRange* range)
{
    const std::size_t colon = text.find(':');
    if (colon == std::string_view::npos)
    {
        return false;
    }
    return ParseUint32(text.substr(0, colon), &range->start) &&
        ParseUint32(text.substr(colon + 1), &range->length) &&
        range->length != 0;
}

bool ReadBinaryFile(const std::string& path, std::vector<std::uint8_t>* data)
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

void PrintReport(const rex86::census::CensusOptions& options,
                 const rex86::census::CensusResult& result)
{
    std::cout << "base=0x" << std::hex << options.base << std::dec
              << " entries=" << options.entries.size()
              << " exec_ranges=" << options.exec_ranges.size() << "\n";
    std::cout << "reached_instructions=" << result.reached_instructions
              << " mnemonics=" << result.mnemonics.size()
              << " forms=" << result.form_counts.size()
              << " decode_stops=" << result.decode_stops
              << " out_of_range_edges=" << result.out_of_range_edges << "\n";
    std::cout << "sweep_decoded=" << result.sweep_decoded
              << " sweep_failed=" << result.sweep_failed
              << " sweep_mnemonics=" << result.sweep_mnemonics.size() << "\n";
    std::cout << "x87_total=" << result.x87.total << " x87_float80_memory="
              << result.x87.float80_memory_operands
              << " x87_control_word=" << result.x87.control_word_access
              << " x87_env_save_restore="
              << result.x87.environment_save_restore << "\n";

    std::cout << "\n-- isa sets --\n";
    for (const auto& [name, count] : result.isa_sets)
    {
        std::cout << name << " " << count << "\n";
    }

    // Mnemonics by volume, with their form sets: level A and level B of the
    // rePIU census in one table.
    std::vector<std::pair<std::string, const rex86::census::MnemonicTally*>>
        by_count;
    by_count.reserve(result.mnemonics.size());
    for (const auto& [name, tally] : result.mnemonics)
    {
        by_count.emplace_back(name, &tally);
    }
    std::sort(by_count.begin(), by_count.end(),
              [](const auto& left, const auto& right) {
                  if (left.second->instructions != right.second->instructions)
                  {
                      return left.second->instructions >
                          right.second->instructions;
                  }
                  return left.first < right.first;
              });
    std::cout << "\n-- mnemonics (count, distinct forms) --\n";
    for (const auto& [name, tally] : by_count)
    {
        std::cout << name << " " << tally->instructions << " "
                  << tally->forms.size() << "\n";
    }

    // Cumulative coverage by operand form, the figure that sizes the
    // interpreter: "the top N forms cover X percent".
    std::vector<std::pair<std::string, std::uint64_t>> forms(
        result.form_counts.begin(), result.form_counts.end());
    std::sort(forms.begin(), forms.end(),
              [](const auto& left, const auto& right) {
                  if (left.second != right.second)
                  {
                      return left.second > right.second;
                  }
                  return left.first < right.first;
              });
    std::cout << "\n-- forms by volume (cumulative %) --\n";
    std::uint64_t cumulative = 0;
    for (const auto& [form, count] : forms)
    {
        cumulative += count;
        const double percent = result.reached_instructions == 0
            ? 0.0
            : 100.0 * static_cast<double>(cumulative) /
                static_cast<double>(result.reached_instructions);
        std::cout << form << " " << count << " " << std::fixed
                  << std::setprecision(2) << percent << "\n";
    }

    if (result.x87.total != 0)
    {
        std::cout << "\n-- x87 mnemonics --\n";
        for (const auto& [name, count] : result.x87.mnemonics)
        {
            std::cout << name << " " << count << "\n";
        }
    }
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        PrintUsage();
        return 2;
    }

    std::string path;
    rex86::census::CensusOptions options;
    for (int index = 1; index < argc; ++index)
    {
        const std::string_view argument(argv[index]);
        const auto next_value = [&]() -> const char* {
            return index + 1 < argc ? argv[++index] : nullptr;
        };
        if (argument == "--base")
        {
            const char* const value = next_value();
            if (value == nullptr || !ParseUint32(value, &options.base))
            {
                std::cerr << "--base needs a 32-bit address\n";
                return 2;
            }
        }
        else if (argument == "--entry")
        {
            const char* const value = next_value();
            std::uint32_t entry = 0;
            if (value == nullptr || !ParseUint32(value, &entry))
            {
                std::cerr << "--entry needs a 32-bit address\n";
                return 2;
            }
            options.entries.push_back(entry);
        }
        else if (argument == "--exec")
        {
            const char* const value = next_value();
            rex86::census::ExecRange range;
            if (value == nullptr || !ParseExecRange(value, &range))
            {
                std::cerr << "--exec needs START:LENGTH\n";
                return 2;
            }
            options.exec_ranges.push_back(range);
        }
        else if (!argument.empty() && argument.front() == '-')
        {
            std::cerr << "unknown option: " << argument << "\n";
            PrintUsage();
            return 2;
        }
        else if (path.empty())
        {
            path = std::string(argument);
        }
        else
        {
            PrintUsage();
            return 2;
        }
    }
    if (path.empty() || options.entries.empty())
    {
        PrintUsage();
        return 2;
    }

    std::vector<std::uint8_t> image;
    if (!ReadBinaryFile(path, &image))
    {
        std::cerr << "failed to read " << path << "\n";
        return 1;
    }

    const rex86::census::CensusResult result =
        rex86::census::RunCensus(image.data(), image.size(), options);
    PrintReport(options, result);
    return 0;
}
