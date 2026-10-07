// The census logic, separated from main.cpp so the unit tests exercise the
// walk and the tallies without a process or a file. See design #5: the tool
// takes a flat code image and explicit entry points; relocation and
// decryption belong to the consumer that produced the dump.

#ifndef REX86_TOOLS_CENSUS_CENSUS_H_
#define REX86_TOOLS_CENSUS_CENSUS_H_

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace rex86::census
{

// A guest-address range the census treats as executable. Ranges outside the
// image, and addresses outside every range, stop the walk and the sweep.
struct ExecRange
{
    std::uint32_t start = 0;
    std::uint32_t length = 0;
};

struct CensusOptions
{
    // The guest address where the image's first byte sits.
    std::uint32_t base = 0;
    // Recursive-descent starting points, as guest addresses.
    std::vector<std::uint32_t> entries;
    // Executable ranges; empty means the whole image.
    std::vector<ExecRange> exec_ranges;
};

struct MnemonicTally
{
    std::uint64_t instructions = 0;
    std::set<std::string> forms;
};

// The three x87 figures that decided design #1's 80-bit commitment, kept so
// a re2DJ measurement answers the same question rePIU's task 514 answered.
struct X87Tally
{
    std::uint64_t total = 0;
    std::uint64_t float80_memory_operands = 0;
    std::uint64_t control_word_access = 0;
    std::uint64_t environment_save_restore = 0;
    std::map<std::string, std::uint64_t> mnemonics;
};

struct CensusResult
{
    // The lower bound: what recursive descent reached from the entries.
    std::uint64_t reached_instructions = 0;
    std::map<std::string, MnemonicTally> mnemonics;
    std::map<std::string, std::uint64_t> form_counts;
    std::map<std::string, std::uint64_t> isa_sets;
    X87Tally x87;
    // Walk stops: decode failures, and edges leaving the executable ranges.
    std::uint64_t decode_stops = 0;
    std::uint64_t out_of_range_edges = 0;

    // The upper bound: a linear sweep over every executable range, counting
    // data as code. The gap between the bounds is what is honestly known.
    std::uint64_t sweep_decoded = 0;
    std::uint64_t sweep_failed = 0;
    std::set<std::string> sweep_mnemonics;
};

CensusResult RunCensus(const std::uint8_t* image, std::size_t image_size,
                       const CensusOptions& options);

}  // namespace rex86::census

#endif  // REX86_TOOLS_CENSUS_CENSUS_H_
