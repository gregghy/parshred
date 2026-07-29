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
option(PARSHRED_ENABLE_PGO   "Enable profile-guided optimization (2-stage build)" OFF)
option(PARSHRED_ENABLE_LTO   "Enable link-time optimization" OFF)

# ── Global compiler flags ─────────────────────────────────────────────
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-O3 -DNDEBUG)
    endif()

    # ── Profile-Guided Optimization (PGO) ───────────────────────────
    # Two-stage build:
    #   1. cmake -DPARSHRED_ENABLE_PGO=ON -DPGO_STAGE=generate ...
    #      Build instrumented binary, run benchmarks/tests to collect profile.
    #   2. cmake -DPARSHRED_ENABLE_PGO=ON -DPGO_STAGE=use ...
    #      Rebuild with profile data for 5-15% speedup on hot paths.
    if(PARSHRED_ENABLE_PGO)
        set(PGO_STAGE "use" CACHE STRING "PGO stage: generate or use")
        if(PGO_STAGE STREQUAL "generate")
            add_compile_options(-fprofile-generate)
            add_link_options(-fprofile-generate)
        elseif(PGO_STAGE STREQUAL "use")
            add_compile_options(-fprofile-use -fprofile-correction)
            add_link_options(-fprofile-use -fprofile-correction)
        endif()
    endif()

    # ── Link-Time Optimization (LTO) ────────────────────────────────
    # Enables cross-module inlining and dead code elimination.
    # 3-8% speedup typical, at the cost of longer link times.
    if(PARSHRED_ENABLE_LTO)
        add_compile_options(-flto=auto)
        add_link_options(-flto=auto)
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
if(PARSHRED_BUILD_CLI AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/CMakeLists.txt")
    add_subdirectory(tools)
endif()

# ── Fuzz targets (Clang + libFuzzer only) ──────────────────────────────
if(PARSHRED_BUILD_FUZZ)
    add_subdirectory(tests/fuzz)
endif()

# ── Installation ───────────────────────────────────────────────────────
# Skip C++ library/header installation when building only the Python wheel.
# scikit-build-core sets SKBUILD when building via pip.
if(NOT SKBUILD)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    # Collect all targets that need exporting (parshred + SIMD object libs)
    set(_parshred_install_targets parshred)
    if(TARGET parshred_sse42)
        list(APPEND _parshred_install_targets parshred_sse42)
    endif()
    if(TARGET parshred_avx2)
        list(APPEND _parshred_install_targets parshred_avx2)
    endif()
    if(TARGET parshred_avx512)
        list(APPEND _parshred_install_targets parshred_avx512)
    endif()

    install(TARGETS ${_parshred_install_targets}
        EXPORT parshredTargets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    install(DIRECTORY include/parshred
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "*.hpp"
    )

    install(EXPORT parshredTargets
        FILE parshredTargets.cmake
        NAMESPACE parshred::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/parshred
    )

    configure_package_config_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/parshredConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/parshredConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/parshred
    )

    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/parshredConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )

    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/parshredConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/parshredConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/parshred
    )
endif()
