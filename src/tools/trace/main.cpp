// rex86_trace: replays trace files on the core and reports every case whose
// outcome differs from the recorded one (design #22, decision 5).
//
// Usage: rex86_trace [--quiet] <file.rxt>...
//        rex86_trace --dump <case index> <file.rxt>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "trace/replay.h"
#include "trace/trace_format.h"

int main(int argc, char** argv)
{
    if (argc == 4 && std::strcmp(argv[1], "--dump") == 0)
    {
        std::vector<rex86::trace::Case> cases;
        std::string error;
        if (!rex86::trace::ReadFile(argv[3], &cases, &error))
        {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 2;
        }
        const unsigned long index = std::strtoul(argv[2], nullptr, 0);
        if (index >= cases.size())
        {
            std::fprintf(stderr, "case %lu out of range (%zu cases)\n", index, cases.size());
            return 2;
        }
        const rex86::trace::Case& c = cases[index];
        std::printf("%s", rex86::trace::Describe(c).c_str());
        const rex86::trace::ReplayResult r = rex86::trace::Replay(c);
        std::printf("replay %s%s\n", r.matched ? "matched" : "MISMATCH: ",
                    r.difference.c_str());
        return r.matched ? 0 : 1;
    }

    bool quiet = false;
    std::vector<std::string> files;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--quiet") == 0)
        {
            quiet = true;
        }
        else
        {
            files.emplace_back(argv[i]);
        }
    }
    if (files.empty())
    {
        std::fprintf(stderr, "usage: rex86_trace [--quiet] <file.rxt>...\n"
                             "       rex86_trace --dump <case> <file.rxt>\n");
        return 2;
    }
    unsigned long long total_cases = 0;
    unsigned long long total_mismatches = 0;
    for (const std::string& file : files)
    {
        std::vector<rex86::trace::Case> cases;
        std::string error;
        if (!rex86::trace::ReadFile(file, &cases, &error))
        {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 2;
        }
        unsigned long long mismatches = 0;
        for (std::size_t i = 0; i < cases.size(); ++i)
        {
            const rex86::trace::ReplayResult r = rex86::trace::Replay(cases[i]);
            if (!r.matched)
            {
                ++mismatches;
                if (!quiet || mismatches <= 10)
                {
                    std::printf("MISMATCH %s case %zu: %s\n", file.c_str(), i,
                                r.difference.c_str());
                }
            }
        }
        std::printf("[rex86-trace] file=%s cases=%zu mismatches=%llu\n", file.c_str(),
                    cases.size(), mismatches);
        total_cases += cases.size();
        total_mismatches += mismatches;
    }
    std::printf("[rex86-trace] cases=%llu mismatches=%llu\n", total_cases, total_mismatches);
    return total_mismatches == 0 ? 0 : 1;
}
