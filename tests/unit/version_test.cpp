#include "rex86/version.h"

#include <cctype>
#include <string>

#include "test_support.h"

void RunVersionTests(rex86::test::Context& context)
{
    const std::string version = rex86::VersionString();
    REX86_CHECK(context, !version.empty());

    // major.minor.patch: three decimal fields and two dots, nothing else.
    int dots = 0;
    bool digits_only = true;
    for (const char c : version)
    {
        if (c == '.')
        {
            ++dots;
        }
        else if (!std::isdigit(static_cast<unsigned char>(c)))
        {
            digits_only = false;
        }
    }
    REX86_CHECK_EQ(context, dots, 2);
    REX86_CHECK(context, digits_only);
}
