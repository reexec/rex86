// Replays one trace case on the core and compares the outcome with the
// recorded expectation (design #22, decision 5). Uses only the public
// contract, so it runs on every host the core builds for.

#ifndef REX86_TRACE_REPLAY_H_
#define REX86_TRACE_REPLAY_H_

#include <string>

#include "trace/trace_format.h"

namespace rex86::trace
{

struct ReplayResult
{
    bool matched = false;
    // The first difference, empty when matched.
    std::string difference;
};

ReplayResult Replay(const Case& value);

}  // namespace rex86::trace

#endif  // REX86_TRACE_REPLAY_H_
