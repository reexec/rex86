// The libFuzzer entry point of the robustness harness (design #31,
// decision 3): the first 16 input bytes are the case generator's random
// source and the rest is copied into guest memory at the initial EIP, so
// coverage-guided mutation acts on the state and on the instructions. A
// violated invariant aborts, which libFuzzer reports as a crash with the
// input that reproduces it. Built with REX86_LIBFUZZER (Clang).

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "tools/robust/robust_case.h"

extern "C" int LLVMFuzzerInitialize(int*, char***)
{
    rex86::robust::SetMaxMemory(0x10000u);
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, const std::size_t size)
{
    constexpr std::size_t kHeader = 16;
    const std::size_t header = size < kHeader ? size : kHeader;
    const rex86::robust::CaseResult result = rex86::robust::RunCaseTwice(
        [data, header] { return rex86::robust::Random(data, header); }, data + header,
        size - header);
    if (!result.ok)
    {
        std::fprintf(stderr, "VIOLATION: %s\n", result.failure.c_str());
        std::abort();
    }
    rex86::robust::Random decoder_random(data, size);
    std::string failure;
    if (!rex86::robust::FuzzDecoder(decoder_random, &failure))
    {
        std::fprintf(stderr, "VIOLATION: %s\n", failure.c_str());
        std::abort();
    }
    return 0;
}
