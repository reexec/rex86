#ifndef REX86_TESTS_UNIT_TEST_SUPPORT_H_
#define REX86_TESTS_UNIT_TEST_SUPPORT_H_

#include <cstdio>
#include <string>

// A deliberately small harness, as in rePIU and re2DJ. Adding a test
// framework is a license and build-time decision for its own design note.
namespace rex86::test
{

struct Context
{
    int checks = 0;
    int failures = 0;

    void Check(bool condition, const char* expression, const char* file, int line)
    {
        ++checks;
        if (condition)
        {
            return;
        }
        ++failures;
        std::fprintf(stderr, "%s:%d: FAILED %s\n", file, line, expression);
    }

    void Fail(const std::string& message, const char* file, int line)
    {
        ++checks;
        ++failures;
        std::fprintf(stderr, "%s:%d: FAILED %s\n", file, line, message.c_str());
    }
};

}  // namespace rex86::test

#define REX86_CHECK(context, expression) \
    (context).Check((expression), #expression, __FILE__, __LINE__)

#define REX86_CHECK_EQ(context, actual, expected)                                \
    do                                                                           \
    {                                                                            \
        const auto& rex86_actual = (actual);                                     \
        const auto& rex86_expected = (expected);                                 \
        if (!(rex86_actual == rex86_expected))                                   \
        {                                                                        \
            (context).Fail(std::string(#actual) + " != " + #expected,            \
                           __FILE__,                                             \
                           __LINE__);                                            \
        }                                                                        \
        else                                                                     \
        {                                                                        \
            (context).Check(true, #actual, __FILE__, __LINE__);                  \
        }                                                                        \
    } while (false)

void RunVersionTests(rex86::test::Context& context);
void RunCpuStateTests(rex86::test::Context& context);
void RunGuestMemoryTests(rex86::test::Context& context);
void RunCpuTests(rex86::test::Context& context);
void RunDecoderTests(rex86::test::Context& context);
void RunCensusTests(rex86::test::Context& context);
void RunMooReaderTests(rex86::test::Context& context);
void RunInterpTests(rex86::test::Context& context);
void RunX87Tests(rex86::test::Context& context);
void RunX87TranscendentalTests(rex86::test::Context& context);
void RunPost386Tests(rex86::test::Context& context);
void RunTraceTests(rex86::test::Context& context);
void RunBenchTests(rex86::test::Context& context);
void RunMmxTests(rex86::test::Context& context);
void RunSseTests(rex86::test::Context& context);
void RunSseFloatTests(rex86::test::Context& context);
void RunRobustTests(rex86::test::Context& context);
void RunDecodeCacheTests(rex86::test::Context& context);
void RunBlockTests(rex86::test::Context& context);
void RunRepBudgetTests(rex86::test::Context& context);

#endif  // REX86_TESTS_UNIT_TEST_SUPPORT_H_
