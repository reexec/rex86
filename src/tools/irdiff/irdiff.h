// The IR differential (design #43, decision 7): random instructions and
// random states, run once through the interpreter and once through the
// frontend and the IR evaluator on identical copies, compared in registers,
// EFLAGS, EIP and memory. Portable: it needs no host CPU to compare with,
// so every host runs it.

#ifndef REX86_TOOLS_IRDIFF_IRDIFF_H_
#define REX86_TOOLS_IRDIFF_IRDIFF_H_

#include <cstdint>
#include <map>
#include <string>

namespace rex86::irdiff
{

struct Options
{
    std::uint64_t seed = 1;
    std::uint64_t cases = 1000;
    // Instructions per block for RunBlocks (1 to this many).
    unsigned max_block = 16;
    bool verbose = false;
};

struct Stats
{
    std::uint64_t cases = 0;
    // Cases where the evaluator ran instructions and was compared.
    std::uint64_t compared = 0;
    // Cases where a check sent the first instruction to the interpreter.
    std::uint64_t exited = 0;
    std::uint64_t mismatches = 0;
    std::map<std::string, std::uint64_t> compared_by_mnemonic;
    // The first mismatch, for the report.
    std::string first_failure;
};

// One covered instruction per case, as a one-instruction block, optimized
// and not.
Stats RunForms(const Options& options);

// Runs of covered instructions, the block ending wherever the frontend
// ends it, compared against the interpreter running as many steps.
Stats RunBlocks(const Options& options);

}  // namespace rex86::irdiff

#endif  // REX86_TOOLS_IRDIFF_IRDIFF_H_
