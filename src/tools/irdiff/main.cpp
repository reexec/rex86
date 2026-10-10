// rex86_irdiff: the IR differential at length (design #43, decision 7).
// Runs the per-form and the block differential from a seed and reports one
// [rex86-irdiff] line each, with the first mismatch when there is one.
//
// Usage: rex86_irdiff [--cases N] [--seed S] [--verbose]
//   --cases N   cases per differential (default 100000); blocks run N / 4

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "tools/irdiff/irdiff.h"

namespace
{

void Print(const char* kind, const rex86::irdiff::Options& options,
           const rex86::irdiff::Stats& stats)
{
    std::printf("[rex86-irdiff] kind=%s seed=%llu cases=%llu compared=%llu exited=%llu "
                "mnemonics=%zu mismatches=%llu\n",
                kind, static_cast<unsigned long long>(options.seed),
                static_cast<unsigned long long>(stats.cases),
                static_cast<unsigned long long>(stats.compared),
                static_cast<unsigned long long>(stats.exited), stats.compared_by_mnemonic.size(),
                static_cast<unsigned long long>(stats.mismatches));
    if (stats.mismatches != 0)
    {
        std::printf("first mismatch: %s\n", stats.first_failure.c_str());
    }
}

}  // namespace

int main(int argc, char** argv)
{
    rex86::irdiff::Options options;
    options.cases = 100000;
    for (int i = 1; i < argc; ++i)
    {
        const bool has_value = i + 1 < argc;
        if (std::strcmp(argv[i], "--cases") == 0 && has_value)
        {
            options.cases = std::strtoull(argv[++i], nullptr, 0);
        }
        else if (std::strcmp(argv[i], "--seed") == 0 && has_value)
        {
            options.seed = std::strtoull(argv[++i], nullptr, 0);
        }
        else if (std::strcmp(argv[i], "--verbose") == 0)
        {
            options.verbose = true;
        }
        else
        {
            std::fprintf(stderr, "usage: rex86_irdiff [--cases N] [--seed S] [--verbose]\n");
            return 2;
        }
    }
    const rex86::irdiff::Stats forms = rex86::irdiff::RunForms(options);
    Print("forms", options, forms);
    rex86::irdiff::Options block_options = options;
    block_options.cases = options.cases / 4 + 1;
    const rex86::irdiff::Stats blocks = rex86::irdiff::RunBlocks(block_options);
    Print("blocks", block_options, blocks);
    const bool ok = forms.mismatches == 0 && blocks.mismatches == 0;
    std::printf("[rex86-irdiff] result=%s\n", ok ? "ok" : "fail");
    return ok ? 0 : 1;
}
