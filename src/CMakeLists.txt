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

# ── SIMD compilation units with arch-specific flags ───────────────────
# Each scanner variant is compiled with the minimum required ISA flags.
# Runtime dispatch in simd_scanner.cpp selects the best one via CPUID.

# SSE 4.2 — baseline for most x86-64 chips (2008+)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
    add_library(parshred_sse42 OBJECT simd/scanner_sse42.cpp)
    target_include_directories(parshred_sse42 PRIVATE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    )
    target_compile_options(parshred_sse42 PRIVATE -msse4.2 -mpclmul)
    target_link_libraries(parshred PRIVATE parshred_sse42)

    # AVX2 — Haswell+ (2013+)
    add_library(parshred_avx2 OBJECT simd/scanner_avx2.cpp)
    target_include_directories(parshred_avx2 PRIVATE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    )
    target_compile_options(parshred_avx2 PRIVATE -mavx2 -mpclmul)
    target_link_libraries(parshred PRIVATE parshred_avx2)

    # AVX-512 — Skylake-X / Zen 4+ (2017+/2022+)
    add_library(parshred_avx512 OBJECT simd/scanner_avx512.cpp)
    target_include_directories(parshred_avx512 PRIVATE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    )
    target_compile_options(parshred_avx512 PRIVATE -mavx512f -mavx512bw -mpclmul)
    target_link_libraries(parshred PRIVATE parshred_avx512)

    target_compile_definitions(parshred PRIVATE PARSHRED_HAS_SSE42=1 PARSHRED_HAS_AVX2=1 PARSHRED_HAS_AVX512=1)

    # Compile the fused SAX parser with AVX2 for SIMD scanning utils
    set_source_files_properties(fast_sax.cpp PROPERTIES COMPILE_OPTIONS "-mavx2")
endif()
