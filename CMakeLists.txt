cmake_minimum_required(VERSION 3.20)

# The version is the VERSION file, format-checked here and handed to the core
# as a private compile definition; consumers read it through
# rex86::VersionString().
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" REX86_VERSION LIMIT_COUNT 1)
string(STRIP "${REX86_VERSION}" REX86_VERSION)
if(NOT REX86_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "VERSION must be major.minor.patch, got '${REX86_VERSION}'")
endif()

# C is for the vendored Zydis amalgamation; the core itself is C++.
project(rex86 VERSION "${REX86_VERSION}" LANGUAGES C CXX)
message(STATUS "rex86 version ${REX86_VERSION}")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Tests and tools are built when this is the top-level project. A consumer
# pulling rex86 in through FetchContent gets the library alone unless it asks.
option(REX86_BUILD_TESTS "Build the unit tests and the probe" ${PROJECT_IS_TOP_LEVEL})
option(REX86_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

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
endif()

# The vendored Zydis v4.1.1 amalgamation (MIT, with Zycore; see
# THIRD_PARTY_NOTICES.md). Third-party code does not get rex86_warnings, so
# its warnings cannot break a -Werror CI job.
add_library(rex86_zydis STATIC
    third_party/zydis/Zydis.c
)
target_include_directories(rex86_zydis PUBLIC third_party/zydis)
target_compile_definitions(rex86_zydis PUBLIC ZYDIS_STATIC_BUILD)

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
    src/interp/access.cpp
    src/interp/exec_arith.cpp
    src/interp/exec_strings.cpp
    src/interp/interpreter.cpp
)
add_library(rex86::core ALIAS rex86_core)
target_include_directories(rex86_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_include_directories(rex86_core PRIVATE src)
target_compile_definitions(rex86_core PRIVATE REX86_VERSION="${REX86_VERSION}")
target_link_libraries(rex86_core PRIVATE rex86_warnings rex86_zydis)

if(REX86_BUILD_TESTS)
    include(CTest)
    enable_testing()

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
        src/tools/census/census.cpp
        src/tools/sst/moo_reader.cpp
    )
    # The decoder and census tests include internal headers under src/,
    # which is deliberate: the public contract does not expose the decoder.
    target_include_directories(rex86_unit_tests PRIVATE src)
    target_link_libraries(rex86_unit_tests
        PRIVATE rex86::core rex86_warnings rex86_zydis)

    add_executable(rex86_probe src/tools/probe/main.cpp)
    target_link_libraries(rex86_probe PRIVATE rex86::core rex86_warnings)

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

    # Under Emscripten an executable is a .js loader plus a .wasm module, and
    # CTest runs it through Node.
    if(EMSCRIPTEN)
        add_test(NAME rex86_unit_tests COMMAND node $<TARGET_FILE:rex86_unit_tests>)
        add_test(NAME rex86_probe COMMAND node $<TARGET_FILE:rex86_probe>)
    else()
        add_test(NAME rex86_unit_tests COMMAND rex86_unit_tests)
        add_test(NAME rex86_probe COMMAND rex86_probe)
    endif()
endif()
