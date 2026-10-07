#include "rex86/version.h"

#ifndef REX86_VERSION
#error "REX86_VERSION must be defined by the build from the VERSION file."
#endif

namespace rex86
{

const char* VersionString()
{
    return REX86_VERSION;
}

}  // namespace rex86
