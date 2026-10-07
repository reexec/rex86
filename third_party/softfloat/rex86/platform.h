// rex86's platform.h for the vendored Berkeley SoftFloat 3 (see
// THIRD_PARTY_NOTICES.md). Every host rex86 supports is little-endian, and
// the build is plain portable C: no compiler builtins or 128-bit
// intrinsics, so the integer arithmetic -- and therefore every x87 result
// -- is identical on x86, x86-64, wasm32 and AArch64. INLINE_LEVEL is left
// undefined, so the primitives are ordinary out-of-line functions and no C99
// inline-linkage rules come into play.

#ifndef REX86_SOFTFLOAT_PLATFORM_H_
#define REX86_SOFTFLOAT_PLATFORM_H_

#define LITTLEENDIAN 1

#define INLINE static inline

// SoftFloat's rounding mode, exception flags and extF80 rounding precision
// are globals set per operation; thread-local storage keeps Cpu instances
// on different threads apart. The core's C++ side includes softfloat.h too,
// so the declaration must be thread-local in both languages.
#if defined(__cplusplus)
#define THREAD_LOCAL thread_local
#elif defined(_MSC_VER)
#define THREAD_LOCAL __declspec(thread)
#else
#define THREAD_LOCAL _Thread_local
#endif

#endif  // REX86_SOFTFLOAT_PLATFORM_H_
