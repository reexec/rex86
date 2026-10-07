#ifndef REX86_VERSION_H_
#define REX86_VERSION_H_

namespace rex86
{

// The version read from the repository-root VERSION file at configure time,
// as "major.minor.patch". The compile definition carrying it is private to
// the core library, so consumers read it through this function.
const char* VersionString();

}  // namespace rex86

#endif  // REX86_VERSION_H_
