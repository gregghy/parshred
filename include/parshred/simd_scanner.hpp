#pragma once
/// @file simd_scanner.hpp
/// @brief SIMD-accelerated structural character scanner.
///
/// Scans the input for XML structural characters (< > / " ' = &) and
/// produces a StructuralIndex — a flat array of byte offsets where these
/// characters appear, with characters inside quoted strings masked out.

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace parshred {

/// The structural index: a sorted array of byte offsets pointing to
/// structural characters in the input.
struct StructuralIndex {
    std::vector<uint32_t> positions;  // byte offsets of structural chars
    std::vector<uint8_t>  chars;      // the character at each position
    size_t                input_size = 0;
};

/// Scan input for structural characters using the best available SIMD ISA.
///
/// Characters inside quoted attribute values (both " and ') are excluded.
/// The scanner handles the full input; no chunking needed by the caller.
///
/// @param input  The raw XML input bytes.
/// @return A StructuralIndex with positions of all un-quoted structural chars.
[[nodiscard]] StructuralIndex simd_scan(std::span<const char> input);

// ── Backend-specific scan functions (called via runtime dispatch) ────
namespace detail {

/// Function signature for a scanner backend.
using ScanFunc = void(*)(const char* data, size_t len,
                         std::vector<uint32_t>& positions,
                         std::vector<uint8_t>& chars);

[[nodiscard]] ScanFunc select_best_scanner() noexcept;

void scan_scalar(const char* data, size_t len,
                 std::vector<uint32_t>& positions,
                 std::vector<uint8_t>& chars);

#ifdef __x86_64__
void scan_sse42(const char* data, size_t len,
                std::vector<uint32_t>& positions,
                std::vector<uint8_t>& chars);

void scan_avx2(const char* data, size_t len,
               std::vector<uint32_t>& positions,
               std::vector<uint8_t>& chars);

void scan_avx512(const char* data, size_t len,
                 std::vector<uint32_t>& positions,
                 std::vector<uint8_t>& chars);
#endif

} // namespace detail
} // namespace parshred
