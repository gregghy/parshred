# ── parshred core library ──────────────────────────────────────────────
add_library(parshred STATIC
    arena.cpp
    mmap_reader.cpp
    simd_scanner.cpp
    simd/scanner_scalar.cpp
    tokenizer.cpp
    sax_parser.cpp
    fast_sax.cpp
)

target_include_directories(parshred PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(parshred PUBLIC cxx_std_20)

# ── SIMD compilation units with arch-specific flags ───────────────────
# Each scanner variant is compiled with the minimum required ISA flags.
# Runtime dispatch in simd_scanner.cpp selects the best one via CPUID.

# SSE 4.2 — baseline for most x86-64 chips (2008+)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
    # SIMD ISA flags: GCC/Clang use -m<flag>, MSVC uses /arch:<name>.
    # SSE4.2 is always available on x86-64 MSVC (baseline), so no flag needed.
    if(MSVC)
        set(SSE42_FLAGS "")
        set(AVX2_FLAGS "/arch:AVX2")
        set(AVX512_FLAGS "/arch:AVX512")
        set(FAST_SAX_FLAGS "/arch:AVX2")
    else()
        set(SSE42_FLAGS "-msse4.2;-mpclmul")
        set(AVX2_FLAGS "-mavx2;-mpclmul")
        set(AVX512_FLAGS "-mavx512f;-mavx512bw;-mpclmul")
        set(FAST_SAX_FLAGS "-mavx2")
    endif()

    add_library(parshred_sse42 OBJECT simd/scanner_sse42.cpp)
    target_include_directories(parshred_sse42 PRIVATE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    )
    target_compile_options(parshred_sse42 PRIVATE ${SSE42_FLAGS})
    target_link_libraries(parshred PRIVATE parshred_sse42)

    # AVX2 — Haswell+ (2013+)
    add_library(parshred_avx2 OBJECT simd/scanner_avx2.cpp)
    target_include_directories(parshred_avx2 PRIVATE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    )
    target_compile_options(parshred_avx2 PRIVATE ${AVX2_FLAGS})
    target_link_libraries(parshred PRIVATE parshred_avx2)

    # AVX-512 — Skylake-X / Zen 4+ (2017+/2022+)
    add_library(parshred_avx512 OBJECT simd/scanner_avx512.cpp)
    target_include_directories(parshred_avx512 PRIVATE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    )
    target_compile_options(parshred_avx512 PRIVATE ${AVX512_FLAGS})
    target_link_libraries(parshred PRIVATE parshred_avx512)

    target_compile_definitions(parshred PRIVATE PARSHRED_HAS_SSE42=1 PARSHRED_HAS_AVX2=1 PARSHRED_HAS_AVX512=1)

    # Compile the fused SAX parser with AVX2 for SIMD scanning utils
    set_source_files_properties(fast_sax.cpp PROPERTIES COMPILE_OPTIONS "${FAST_SAX_FLAGS}")
endif()
