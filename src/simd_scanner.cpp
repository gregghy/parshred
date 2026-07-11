/// @file simd_scanner.cpp
/// @brief SIMD scanner dispatch — selects the best backend at load time via CPUID.

#include <parshred/simd_scanner.hpp>

#ifdef __x86_64__
#include <cpuid.h>
#endif

namespace parshred {

namespace detail {

ScanFunc select_best_scanner() noexcept {
#ifdef __x86_64__
    unsigned int eax, ebx, ecx, edx;

    // Check for AVX-512F + AVX-512BW (leaf 7, subleaf 0)
    if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) {
        bool has_avx512f  = (ebx >> 16) & 1;  // bit 16 of EBX
        bool has_avx512bw = (ebx >> 30) & 1;  // bit 30 of EBX

        if (has_avx512f && has_avx512bw) {
            return scan_avx512;
        }

        bool has_avx2 = (ebx >> 5) & 1; // bit 5 of EBX
        if (has_avx2) {
            return scan_avx2;
        }
    }

    // Check for SSE 4.2 (leaf 1)
    if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
        bool has_sse42 = (ecx >> 20) & 1; // bit 20 of ECX
        if (has_sse42) {
            return scan_sse42;
        }
    }
#endif // __x86_64__

    return scan_scalar;
}

} // namespace detail

// Global function pointer, initialized once at load time.
static detail::ScanFunc g_scanner = detail::select_best_scanner();

StructuralIndex simd_scan(std::span<const char> input) {
    StructuralIndex idx;
    idx.input_size = input.size();

    if (input.empty()) return idx;

    // Reserve a reasonable estimate (~1 structural char per 8 bytes)
    idx.positions.reserve(input.size() / 8);
    idx.chars.reserve(input.size() / 8);

    g_scanner(input.data(), input.size(), idx.positions, idx.chars);

    return idx;
}

} // namespace parshred
