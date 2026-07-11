# ── Fuzz targets ───────────────────────────────────────────────────────
#
# Only built when -DPARSHRED_BUILD_FUZZ=ON.
# Requires Clang with libFuzzer support; the build will fail deliberately
# if another compiler is used so that CI catches misconfiguration early.
#
# Usage:
#   cmake -S . -B build/fuzz \
#         -DCMAKE_BUILD_TYPE=RelWithDebInfo \
#         -DPARSHRED_BUILD_FUZZ=ON \
#         -DPARSHRED_BUILD_TESTS=OFF \
#         CC=clang-17 CXX=clang++-17
#   cmake --build build/fuzz --target fuzz_sax fuzz_dom fuzz_xpath

# ── Toolchain guard ────────────────────────────────────────────────────
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR
        "PARSHRED_BUILD_FUZZ=ON requires Clang (found ${CMAKE_CXX_COMPILER_ID}). "
        "Set CC=clang CXX=clang++ (or a versioned variant) before configuring.")
endif()

# ── Shared compile/link flags ──────────────────────────────────────────
# -fsanitize=fuzzer          — links the libFuzzer driver, provides main()
# -fsanitize=address         — AddressSanitizer: heap/stack OOB, UAF, leaks
# -fsanitize=undefined       — UBSanitizer: signed overflow, bad casts, etc.
# -fno-omit-frame-pointer    — keeps stack traces readable in crash reports
# -g                         — debug info for symbolisation
set(FUZZ_COMPILE_FLAGS
    -fsanitize=fuzzer,address,undefined
    -fno-omit-frame-pointer
    -g
)
set(FUZZ_LINK_FLAGS
    -fsanitize=fuzzer,address,undefined
)

# ── Helper function: create one fuzz target ────────────────────────────
# Usage: parshred_fuzz_target(<name> <source>)
function(parshred_fuzz_target TARGET_NAME SOURCE_FILE)
    add_executable(${TARGET_NAME} ${SOURCE_FILE})

    target_include_directories(${TARGET_NAME} PRIVATE
        ${PROJECT_SOURCE_DIR}/include
    )

    target_link_libraries(${TARGET_NAME} PRIVATE parshred)

    target_compile_options(${TARGET_NAME} PRIVATE ${FUZZ_COMPILE_FLAGS})

    target_link_options(${TARGET_NAME} PRIVATE ${FUZZ_LINK_FLAGS})

    # Place the binary in a predictable location that the fuzz workflow uses.
    set_target_properties(${TARGET_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/fuzz"
    )
endfunction()

# ── SAX fuzzer ────────────────────────────────────────────────────────
# Feeds arbitrary bytes to fast_parse (Normal + Turbo modes).
parshred_fuzz_target(fuzz_sax fuzz_sax.cpp)

# ── DOM fuzzer ────────────────────────────────────────────────────────
# Feeds arbitrary bytes to fast_dom_parse<0> and fast_dom_parse<FDOM_FASTEST>.
parshred_fuzz_target(fuzz_dom fuzz_dom.cpp)

# ── XPath fuzzer ──────────────────────────────────────────────────────
# Uses the fuzz input as an XPath expression against a fixed DOM document.
parshred_fuzz_target(fuzz_xpath fuzz_xpath.cpp)
