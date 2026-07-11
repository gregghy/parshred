cmake_minimum_required(VERSION 3.20...4.2)
project(parshred
    VERSION 0.1.0
    DESCRIPTION "The World's Fastest XML Parser"
    LANGUAGES CXX
)

# ── C++20 ──────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Options ────────────────────────────────────────────────────────────
option(PARSHRED_BUILD_TESTS  "Build unit tests"        ON)
option(PARSHRED_BUILD_BENCH  "Build benchmarks"         OFF)
option(PARSHRED_BUILD_PYTHON "Build Python bindings"    OFF)
option(PARSHRED_BUILD_CLI    "Build CLI tool"           ON)
option(PARSHRED_BUILD_FUZZ   "Build libFuzzer harnesses (Clang only)" OFF)

# ── Global compiler flags ─────────────────────────────────────────────
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-O3 -DNDEBUG)
    endif()
endif()

# ── Core library ───────────────────────────────────────────────────────
add_subdirectory(src)

# ── Tests ──────────────────────────────────────────────────────────────
if(PARSHRED_BUILD_TESTS)
    enable_testing()
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.17.0
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(googletest)
    add_subdirectory(tests)
endif()

# ── Benchmarks ─────────────────────────────────────────────────────────
if(PARSHRED_BUILD_BENCH)
    include(FetchContent)
    FetchContent_Declare(
        benchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG        v1.9.4
        GIT_SHALLOW    TRUE
    )
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(benchmark)
    add_subdirectory(bench)
endif()

# ── Python bindings ────────────────────────────────────────────────────
if(PARSHRED_BUILD_PYTHON)
    add_subdirectory(python)
endif()

# ── CLI tool ───────────────────────────────────────────────────────────
if(PARSHRED_BUILD_CLI)
    add_subdirectory(tools)
endif()

# ── Fuzz targets (Clang + libFuzzer only) ──────────────────────────────
if(PARSHRED_BUILD_FUZZ)
    add_subdirectory(tests/fuzz)
endif()
