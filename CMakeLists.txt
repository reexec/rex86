cmake_minimum_required(VERSION 3.20)

# The version is the VERSION file, format-checked here and handed to the core
# as a private compile definition; consumers read it through
# rex86::VersionString().
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" REX86_VERSION LIMIT_COUNT 1)
string(STRIP "${REX86_VERSION}" REX86_VERSION)
if(NOT REX86_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "VERSION must be major.minor.patch, got '${REX86_VERSION}'")
endif()

# C is for the vendored Zydis amalgamation and SoftFloat; the core itself
# is C++.
project(rex86 VERSION "${REX86_VERSION}" LANGUAGES C CXX)
message(STATUS "rex86 version ${REX86_VERSION}")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Tests and tools are built when this is the top-level project. A consumer
# pulling rex86 in through FetchContent gets the library alone unless it asks.
option(REX86_BUILD_TESTS "Build the unit tests and the probe" ${PROJECT_IS_TOP_LEVEL})
option(REX86_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
# Sanitizers for rex86's own targets (design #22, decision 7), for example
# "address,undefined". third_party stays uninstrumented, as upstream wrote it.
set(REX86_SANITIZE "" CACHE STRING "Comma-separated -fsanitize= list for rex86 targets")
# The robustness harness's libFuzzer target (design #31, decision 3), Clang
# only: rex86's own targets get coverage instrumentation.
option(REX86_LIBFUZZER "Build rex86_robust_libfuzzer (Clang)" OFF)
# Instructions a Cpu retires before it makes its decode cache (design #34);
# 0 sends every test and fuzz through the cache.
set(REX86_DECODE_CACHE_WARMUP "" CACHE STRING "Override the decode cache warm-up (instructions)")
# Every Cpu translates every block at its first entry with the IR evaluator,
# so the whole test suite runs through translation (design #44).
option(REX86_FORCE_TRANSLATION "Default every Cpu to forced translation" OFF)

if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()

add_library(rex86_warnings INTERFACE)
if(MSVC)
    target_compile_options(rex86_warnings INTERFACE /W4 /permissive- /utf-8)
    if(REX86_WARNINGS_AS_ERRORS)
        target_compile_options(rex86_warnings INTERFACE /WX)
    endif()
else()
    target_compile_options(rex86_warnings INTERFACE -Wall -Wextra -Wpedantic)
    if(REX86_WARNINGS_AS_ERRORS)
        target_compile_options(rex86_warnings INTERFACE -Werror)
    endif()
    if(REX86_SANITIZE)
        target_compile_options(rex86_warnings INTERFACE
            -fsanitize=${REX86_SANITIZE} -fno-sanitize-recover=all -fno-omit-frame-pointer)
        target_link_options(rex86_warnings INTERFACE -fsanitize=${REX86_SANITIZE})
    endif()
    if(REX86_LIBFUZZER)
        target_compile_options(rex86_warnings INTERFACE -fsanitize=fuzzer-no-link)
    endif()
endif()

# The vendored Zydis v4.1.1 amalgamation (MIT, with Zycore; see
# THIRD_PARTY_NOTICES.md). Third-party code does not get rex86_warnings, so
# its warnings cannot break a -Werror CI job.
add_library(rex86_zydis STATIC
    third_party/zydis/Zydis.c
)
target_include_directories(rex86_zydis PUBLIC third_party/zydis)
target_compile_definitions(rex86_zydis PUBLIC ZYDIS_STATIC_BUILD)

# The vendored Berkeley SoftFloat 3 (BSD 3-Clause; see
# THIRD_PARTY_NOTICES.md): the source list of its template-FAST_INT64
# build with the 8086 (x87) specialization, unmodified, plus rex86's own
# platform.h. Like Zydis it does not get rex86_warnings. The core links it
# PRIVATE: only src/fpu/ includes its headers.
file(GLOB REX86_SOFTFLOAT_SOURCES CONFIGURE_DEPENDS
    third_party/softfloat/source/*.c
    third_party/softfloat/source/8086/*.c
)
add_library(rex86_softfloat STATIC ${REX86_SOFTFLOAT_SOURCES})
target_include_directories(rex86_softfloat
    PRIVATE third_party/softfloat/source/8086
    PUBLIC third_party/softfloat/rex86 third_party/softfloat/source/include
)
target_compile_definitions(rex86_softfloat
    PUBLIC SOFTFLOAT_FAST_INT64
    PRIVATE SOFTFLOAT_ROUND_ODD SOFTFLOAT_FAST_DIV32TO16
            SOFTFLOAT_FAST_DIV64TO32
)
if(MSVC)
    # SoftFloat is C99; MSVC's C front end accepts it but warns about
    # conversions SoftFloat makes on purpose.
    target_compile_options(rex86_softfloat PRIVATE /W0)
endif()

# The core: the CPU state, the guest memory view, the host contract, the
# decoder and the Cpu. No OS header is included anywhere under include/ or
# src/; what the core needs from the OS arrives through Environment and
# CodeCacheServices. Zydis is PRIVATE: no public header exposes its types,
# which is what keeps the decoder replaceable.
add_library(rex86_core STATIC
    src/version.cpp
    src/cpu_state.cpp
    src/guest_memory.cpp
    src/cpu.cpp
    src/decode/decoder.cpp
    src/fpu/float80.cpp
    src/fpu/softfloat_bridge.cpp
    src/fpu/sse_math.cpp
    src/fpu/transcendental_kernels.cpp
    src/fpu/wide_float.cpp
    src/fpu/x87_math.cpp
    src/fpu/x87_transcendental.cpp
    src/interp/access.cpp
    src/interp/exec_arith.cpp
    src/interp/exec_bcd.cpp
    src/interp/exec_post386.cpp
    src/interp/exec_segments.cpp
    src/interp/exec_strings.cpp
    src/interp/exec_x87.cpp
    src/interp/exec_x87_env.cpp
    src/interp/exec_x87_transcendental.cpp
    src/interp/exec_mmx.cpp
    src/interp/exec_sse.cpp
    src/interp/exec_sse_float.cpp
    src/interp/exec_fxsave.cpp
    src/interp/simd_access.cpp
    src/interp/x87_access.cpp
    src/interp/x87_stack.cpp
    src/interp/interpreter.cpp
    src/interp/decode_cache.cpp
    src/translate/ir/ir.cpp
    src/translate/ir/evaluator.cpp
    src/translate/ir/frontend.cpp
    src/translate/ir/optimize.cpp
    src/translate/runtime.cpp
)
add_library(rex86::core ALIAS rex86_core)
target_include_directories(rex86_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_include_directories(rex86_core PRIVATE src)
target_compile_definitions(rex86_core PRIVATE REX86_VERSION="${REX86_VERSION}")
if(NOT REX86_DECODE_CACHE_WARMUP STREQUAL "")
    target_compile_definitions(rex86_core PRIVATE REX86_DECODE_CACHE_WARMUP=${REX86_DECODE_CACHE_WARMUP})
endif()
if(REX86_FORCE_TRANSLATION)
    target_compile_definitions(rex86_core PRIVATE REX86_FORCE_TRANSLATION=1)
endif()
target_link_libraries(rex86_core PRIVATE rex86_warnings rex86_zydis rex86_softfloat)

if(REX86_BUILD_TESTS)
    include(CTest)
    enable_testing()
    # The test tools run near Emscripten's default 16 MiB heap (the
    # benchmark alone holds a 4 MiB guest and the decode cache); let it grow,
    # as a browser consumer does.
    if(EMSCRIPTEN)
        add_link_options(-sALLOW_MEMORY_GROWTH=1)
    endif()

    # The trace format and replay (design #22, decision 5): outside the
    # core, on the public contract alone, so it builds on every host.
    add_library(rex86_trace_format STATIC
        src/trace/trace_format.cpp
        src/trace/replay.cpp
    )
    target_include_directories(rex86_trace_format PUBLIC src)
    target_link_libraries(rex86_trace_format
        PUBLIC rex86::core PRIVATE rex86_warnings)
    if(MSVC)
        # std::fopen is the portable C++ API; MSVC's C4996 would steer it
        # to the non-standard fopen_s.
        target_compile_definitions(rex86_trace_format PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()

    # The benchmark harness (design #27): the byte assembler, the synthetic
    # workloads with their reference models and the frame loop, on the
    # public contract alone so that it measures every engine the same way.
    add_library(rex86_bench_lib STATIC
        src/tools/bench/asm.cpp
        src/tools/bench/workloads.cpp
        src/tools/bench/runner.cpp
    )
    target_include_directories(rex86_bench_lib PUBLIC src)
    target_link_libraries(rex86_bench_lib PUBLIC rex86::core PRIVATE rex86_warnings)

    # The robustness harness (design #31): arbitrary bytes and state against
    # the invariants that must hold for any input. Host-neutral, so it
    # builds and runs on every host; the decoder fuzz reaches the internal
    # decoder.
    add_library(rex86_robust_lib STATIC src/tools/robust/robust_case.cpp)
    target_include_directories(rex86_robust_lib PUBLIC src)
    target_link_libraries(rex86_robust_lib PUBLIC rex86::core rex86_zydis PRIVATE rex86_warnings)

    # The IR differential (design #43): shared by the unit tests and the
    # long-run tool, portable to every host.
    add_library(rex86_irdiff_lib STATIC src/tools/irdiff/irdiff.cpp)
    target_include_directories(rex86_irdiff_lib PUBLIC src)
    target_link_libraries(rex86_irdiff_lib PUBLIC rex86::core rex86_zydis PRIVATE rex86_warnings)

    add_executable(rex86_unit_tests
        tests/unit/main.cpp
        tests/unit/version_test.cpp
        tests/unit/cpu_state_test.cpp
        tests/unit/guest_memory_test.cpp
        tests/unit/cpu_test.cpp
        tests/unit/decoder_test.cpp
        tests/unit/census_test.cpp
        tests/unit/moo_reader_test.cpp
        tests/unit/interp_test.cpp
        tests/unit/x87_test.cpp
        tests/unit/x87_transcendental_test.cpp
        tests/unit/post386_test.cpp
        tests/unit/trace_test.cpp
        tests/unit/bench_test.cpp
        tests/unit/mmx_test.cpp
        tests/unit/sse_test.cpp
        tests/unit/sse_float_test.cpp
        tests/unit/robust_test.cpp
        tests/unit/decode_cache_test.cpp
        tests/unit/block_test.cpp
        tests/unit/rep_budget_test.cpp
        tests/unit/ir_test.cpp
        tests/unit/translate_runtime_test.cpp
        src/tools/census/census.cpp
        src/tools/sst/moo_reader.cpp
    )
    # The decoder, census and x87 transcendental tests include internal
    # headers under src/, which is deliberate: the public contract exposes
    # neither the decoder nor the numeric kernels (whose headers need
    # SoftFloat's include paths).
    target_include_directories(rex86_unit_tests PRIVATE src)
    target_link_libraries(rex86_unit_tests
        PRIVATE rex86::core rex86_trace_format rex86_bench_lib rex86_robust_lib rex86_irdiff_lib
                rex86_warnings rex86_zydis rex86_softfloat)

    add_executable(rex86_probe src/tools/probe/main.cpp)
    target_link_libraries(rex86_probe PRIVATE rex86::core rex86_warnings)

    # The trace replay tool and the committed corpus, replayed on every
    # host (design #22, decision 5).
    add_executable(rex86_trace src/tools/trace/main.cpp)
    target_link_libraries(rex86_trace PRIVATE rex86_trace_format rex86_warnings)
    if(EMSCRIPTEN)
        target_link_options(rex86_trace PRIVATE -sNODERAWFS=1)
    endif()
    # rex86_bench measures on every host; --dump writes files, which under
    # Emscripten needs the Node filesystem. $<CONFIG> is printed on its first
    # line so that Debug and Release figures never mix.
    add_executable(rex86_bench src/tools/bench/main.cpp)
    target_link_libraries(rex86_bench PRIVATE rex86_bench_lib rex86_warnings)
    target_compile_definitions(rex86_bench PRIVATE REX86_BENCH_CONFIG="$<CONFIG>")
    if(MSVC)
        target_compile_definitions(rex86_bench PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    if(EMSCRIPTEN)
        target_link_options(rex86_bench PRIVATE -sNODERAWFS=1)
    endif()

    add_executable(rex86_irdiff src/tools/irdiff/main.cpp)
    target_link_libraries(rex86_irdiff PRIVATE rex86_irdiff_lib rex86_warnings)

    add_executable(rex86_robust src/tools/robust/main.cpp)
    target_link_libraries(rex86_robust PRIVATE rex86_robust_lib rex86_warnings)
    if(REX86_LIBFUZZER)
        add_executable(rex86_robust_libfuzzer src/tools/robust/libfuzzer.cpp)
        target_link_libraries(rex86_robust_libfuzzer PRIVATE rex86_robust_lib rex86_warnings)
        target_link_options(rex86_robust_libfuzzer PRIVATE -fsanitize=fuzzer)
    endif()

    file(GLOB REX86_TRACE_CORPUS CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/traces/*.rxt")

    # The census tool reads dump files off the host filesystem, and
    # measurement happens on native hosts, so it is not built under
    # Emscripten; the decoder module itself builds everywhere.
    if(NOT EMSCRIPTEN)
        add_executable(rex86_census
            src/tools/census/main.cpp
            src/tools/census/census.cpp
        )
        target_include_directories(rex86_census PRIVATE src)
        target_link_libraries(rex86_census
            PRIVATE rex86::core rex86_warnings rex86_zydis)

        # The SingleStepTests runner (design #7). The suite's MOO files stay
        # outside the repository; point REX86_SST_DIR at a gunzipped copy to
        # register the ctest entry.
        add_executable(rex86_sst
            src/tools/sst/main.cpp
            src/tools/sst/moo_reader.cpp
        )
        target_include_directories(rex86_sst PRIVATE src)
        target_link_libraries(rex86_sst
            PRIVATE rex86::core rex86_warnings rex86_zydis)
        set(REX86_SST_DIR "" CACHE PATH
            "Directory of gunzipped SingleStepTests .MOO files")
        if(REX86_SST_DIR)
            add_test(NAME rex86_sst COMMAND rex86_sst "${REX86_SST_DIR}")
        endif()
    endif()

    # The x87 host-CPU comparison fuzz (design #19, decision 6): the host
    # executes the same x87 bytes as the core, so it builds only where the
    # host is x86 Linux. A fixed-seed short run is a ctest entry; longer
    # runs take an iteration count and a seed.
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
       CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|i[3-6]86)$")
        add_executable(rex86_x87_fuzz tests/host/linux/x87_fuzz.cpp)
        target_include_directories(rex86_x87_fuzz PRIVATE src)
        target_link_libraries(rex86_x87_fuzz
            PRIVATE rex86::core rex86_trace_format rex86_warnings rex86_zydis)
        add_test(NAME rex86_x87_fuzz COMMAND rex86_x87_fuzz 20000 1)

        # The SIMD comparison (design #29, decision 7) runs the same
        # encodings on i386 and x86-64 hosts.
        add_executable(rex86_simd_fuzz tests/host/linux/simd_fuzz.cpp)
        target_include_directories(rex86_simd_fuzz PRIVATE src)
        target_link_libraries(rex86_simd_fuzz
            PRIVATE rex86::core rex86_trace_format rex86_warnings rex86_zydis)
        add_test(NAME rex86_simd_fuzz COMMAND rex86_simd_fuzz 20000 1)

        # The integer comparison (design #22, decisions 1 to 4) executes
        # 32-bit encodings natively, so it needs an i386 process.
        if(CMAKE_SIZEOF_VOID_P EQUAL 4)
            add_executable(rex86_int_fuzz tests/host/linux/int_fuzz.cpp)
            target_include_directories(rex86_int_fuzz PRIVATE src)
            target_link_libraries(rex86_int_fuzz
                PRIVATE rex86::core rex86_trace_format rex86_warnings rex86_zydis)
            add_test(NAME rex86_int_fuzz COMMAND rex86_int_fuzz 50000 1)
        endif()
    endif()

    # Under Emscripten an executable is a .js loader plus a .wasm module, and
    # CTest runs it through Node.
    if(EMSCRIPTEN)
        add_test(NAME rex86_unit_tests COMMAND node $<TARGET_FILE:rex86_unit_tests>)
        add_test(NAME rex86_probe COMMAND node $<TARGET_FILE:rex86_probe>)
        add_test(NAME rex86_bench_smoke COMMAND node $<TARGET_FILE:rex86_bench> --smoke)
        add_test(NAME rex86_robust_smoke COMMAND node $<TARGET_FILE:rex86_robust> --cases 300)
        add_test(NAME rex86_irdiff_smoke COMMAND node $<TARGET_FILE:rex86_irdiff> --cases 20000)
        if(REX86_TRACE_CORPUS)
            add_test(NAME rex86_trace_corpus
                COMMAND node $<TARGET_FILE:rex86_trace> ${REX86_TRACE_CORPUS})
        endif()
    else()
        add_test(NAME rex86_unit_tests COMMAND rex86_unit_tests)
        add_test(NAME rex86_probe COMMAND rex86_probe)
        add_test(NAME rex86_bench_smoke COMMAND rex86_bench --smoke)
        add_test(NAME rex86_robust_smoke COMMAND rex86_robust --cases 300)
        add_test(NAME rex86_irdiff_smoke COMMAND rex86_irdiff --cases 20000)
        if(REX86_TRACE_CORPUS)
            add_test(NAME rex86_trace_corpus COMMAND rex86_trace ${REX86_TRACE_CORPUS})
        endif()
    endif()
endif()
