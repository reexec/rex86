cmake_minimum_required(VERSION 3.20)

# The version is the VERSION file, format-checked here and handed to the core
# as a private compile definition; consumers read it through
# rex86::VersionString().
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" REX86_VERSION LIMIT_COUNT 1)
string(STRIP "${REX86_VERSION}" REX86_VERSION)
if(NOT REX86_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "VERSION must be major.minor.patch, got '${REX86_VERSION}'")
endif()

project(rex86 VERSION "${REX86_VERSION}" LANGUAGES CXX)
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

# The core: the CPU state, the guest memory view, the host contract and the
# Cpu. No OS header is included anywhere under include/ or src/; what the
# core needs from the OS arrives through Environment and CodeCacheServices.
add_library(rex86_core STATIC
    src/version.cpp
    src/cpu_state.cpp
    src/guest_memory.cpp
    src/cpu.cpp
)
add_library(rex86::core ALIAS rex86_core)
target_include_directories(rex86_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_definitions(rex86_core PRIVATE REX86_VERSION="${REX86_VERSION}")
target_link_libraries(rex86_core PRIVATE rex86_warnings)

if(REX86_BUILD_TESTS)
    include(CTest)
    enable_testing()

    add_executable(rex86_unit_tests
        tests/unit/main.cpp
        tests/unit/version_test.cpp
        tests/unit/cpu_state_test.cpp
        tests/unit/guest_memory_test.cpp
        tests/unit/cpu_test.cpp
    )
    target_link_libraries(rex86_unit_tests PRIVATE rex86::core rex86_warnings)

    add_executable(rex86_probe src/tools/probe/main.cpp)
    target_link_libraries(rex86_probe PRIVATE rex86::core rex86_warnings)

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
