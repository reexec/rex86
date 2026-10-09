#include <cstdio>

#include "test_support.h"

int main()
{
    rex86::test::Context context;

    RunVersionTests(context);
    RunCpuStateTests(context);
    RunGuestMemoryTests(context);
    RunCpuTests(context);
    RunDecoderTests(context);
    RunCensusTests(context);
    RunMooReaderTests(context);
    RunInterpTests(context);
    RunX87Tests(context);
    RunX87TranscendentalTests(context);
    RunPost386Tests(context);
    RunTraceTests(context);
    RunBenchTests(context);
    RunMmxTests(context);
    RunSseTests(context);
    RunSseFloatTests(context);
    RunRobustTests(context);

    std::printf("[rex86-unit-tests] checks=%d failures=%d\n", context.checks, context.failures);
    return context.failures == 0 ? 0 : 1;
}
