#pragma once
/// @file simd_neon.hpp
/// @brief ARM NEON SIMD utilities for parshred.
///
/// Provides NEON equivalents of the AVX2 functions in simd_utils.hpp.
/// Used on ARM64 (aarch64) platforms — Apple M-series, AWS Graviton,
/// Raspberry Pi 4+, Android, etc.

#include <parshred/common.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>

#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

namespace parshred {
namespace neon {

#ifdef __ARM_NEON

// ── Skip text (find '<') ─────────────────────────────────────────────

/// NEON implementation of skip_text_turbo: scan 16 bytes at a time for '<'.
inline size_t skip_text_neon(const char* data, size_t pos, size_t len) noexcept {
    const uint8x16_t v_lt = vdupq_n_u8('<');

    while (pos + 16 <= len) {
        uint8x16_t chunk = vld1q_u8(reinterpret_cast<const uint8_t*>(data + pos));
        uint8x16_t cmp = vceqq_u8(chunk, v_lt);

        // Check if any byte matched
        // NEON doesn't have movemask like SSE. Use max-across to check for non-zero.
        uint8_t max_val = vmaxvq_u8(cmp);
        if (max_val) {
            // Find exact position — scan bytes
            for (size_t i = 0; i < 16; ++i) {
                if (data[pos + i] == '<') return pos + i;
            }
        }
        pos += 16;
    }

    // Scalar tail
    while (pos < len && data[pos] != '<') ++pos;
    return pos;
}

// ── Find character ───────────────────────────────────────────────────

/// NEON implementation of find_char_fast.
inline size_t find_char_neon(const char* data, size_t pos, size_t len, char target) noexcept {
    const uint8x16_t v_target = vdupq_n_u8(static_cast<uint8_t>(target));

    while (pos + 16 <= len) {
        uint8x16_t chunk = vld1q_u8(reinterpret_cast<const uint8_t*>(data + pos));
        uint8x16_t cmp = vceqq_u8(chunk, v_target);

        uint8_t max_val = vmaxvq_u8(cmp);
        if (max_val) {
            for (size_t i = 0; i < 16; ++i) {
                if (data[pos + i] == target) return pos + i;
            }
        }
        pos += 16;
    }

    while (pos < len && data[pos] != target) ++pos;
    return pos;
}

// ── Skip whitespace ──────────────────────────────────────────────────

/// NEON implementation of skip_whitespace.
inline size_t skip_whitespace_neon(const char* data, size_t pos, size_t len) noexcept {
    // Whitespace: 0x20 (space), 0x09 (tab), 0x0A (LF), 0x0D (CR)
    const uint8x16_t v_space = vdupq_n_u8(0x20);
    const uint8x16_t v_tab = vdupq_n_u8(0x09);
    const uint8x16_t v_lf = vdupq_n_u8(0x0A);
    const uint8x16_t v_cr = vdupq_n_u8(0x0D);

    while (pos + 16 <= len) {
        uint8x16_t chunk = vld1q_u8(reinterpret_cast<const uint8_t*>(data + pos));

        uint8x16_t is_ws = vorrq_u8(
            vorrq_u8(vceqq_u8(chunk, v_space), vceqq_u8(chunk, v_tab)),
            vorrq_u8(vceqq_u8(chunk, v_lf), vceqq_u8(chunk, v_cr)));

        // Check if ALL bytes are whitespace (all lanes 0xFF)
        uint8_t min_val = vminvq_u8(is_ws);
        if (min_val == 0) {
            // Found a non-whitespace byte — scan for exact position
            for (size_t i = 0; i < 16; ++i) {
                if (!parshred::is_whitespace(data[pos + i])) return pos + i;
            }
        }
        pos += 16;
    }

    while (pos < len && parshred::is_whitespace(data[pos])) ++pos;
    return pos;
}

// ── Name scanner ─────────────────────────────────────────────────────

/// NEON implementation of read_name_fast.
/// Uses range comparisons like the AVX2 version but with 128-bit vectors.
inline size_t read_name_neon(const char* data, size_t pos, size_t len) noexcept {
    if (pos >= len || !parshred::is_name_start(data[pos])) return pos;
    ++pos;

    const uint8x16_t v_2c = vdupq_n_u8(0x2C);
    const uint8x16_t v_2f = vdupq_n_u8(0x2F);
    const uint8x16_t v_3b = vdupq_n_u8(0x3B);
    const uint8x16_t v_40 = vdupq_n_u8(0x40);
    const uint8x16_t v_5b = vdupq_n_u8(0x5B);
    const uint8x16_t v_5f = vdupq_n_u8(0x5F);
    const uint8x16_t v_60 = vdupq_n_u8(0x60);
    const uint8x16_t v_7b = vdupq_n_u8(0x7B);
    const uint8x16_t v_7f = vdupq_n_u8(0x7F);

    while (pos + 16 <= len) {
        uint8x16_t chunk = vld1q_u8(reinterpret_cast<const uint8_t*>(data + pos));

        // Range checks using unsigned comparisons (vcgt is signed, use sub trick)
        // '-' '.' : 0x2D-0x2E
        uint8x16_t r1 = vandq_u8(vcgtq_u8(chunk, v_2c), vcgtq_u8(v_2f, chunk));
        // '0'-':' : 0x30-0x3A
        uint8x16_t r2 = vandq_u8(vcgtq_u8(chunk, vdupq_n_u8(0x2F)), vcgtq_u8(v_3b, chunk));
        // 'A'-'Z' : 0x41-0x5A
        uint8x16_t r3 = vandq_u8(vcgtq_u8(chunk, v_40), vcgtq_u8(v_5b, chunk));
        // '_' : 0x5F
        uint8x16_t r4 = vceqq_u8(chunk, v_5f);
        // 'a'-'z' : 0x61-0x7A
        uint8x16_t r5 = vandq_u8(vcgtq_u8(chunk, v_60), vcgtq_u8(v_7b, chunk));
        // >= 0x80
        uint8x16_t r6 = vcgtq_u8(chunk, v_7f);

        uint8x16_t is_name = vorrq_u8(
            vorrq_u8(vorrq_u8(r1, r2), vorrq_u8(r3, r4)),
            vorrq_u8(r5, r6));

        // Check if all are name chars
        uint8_t min_val = vminvq_u8(is_name);
        if (min_val == 0) {
            // Found non-name char — find exact position
            for (size_t i = 0; i < 16; ++i) {
                if (!parshred::is_name_char(data[pos + i])) return pos + i;
            }
        }
        pos += 16;
    }

    // Scalar tail
    while (pos < len && parshred::is_name_char(data[pos])) ++pos;
    return pos;
}

// ── Find comment end (-->) ───────────────────────────────────────────

/// NEON implementation of find_comment_end.
inline size_t find_comment_end_neon(const char* data, size_t pos, size_t len) noexcept {
    const uint8x16_t v_dash = vdupq_n_u8('-');

    while (pos + 16 <= len) {
        uint8x16_t chunk = vld1q_u8(reinterpret_cast<const uint8_t*>(data + pos));
        uint8x16_t cmp = vceqq_u8(chunk, v_dash);

        uint8_t max_val = vmaxvq_u8(cmp);
        if (max_val) {
            // Check for -- followed by >
            for (size_t i = 0; i < 16; ++i) {
                if (data[pos + i] == '-' && pos + i + 2 < len &&
                    data[pos + i + 1] == '-' && data[pos + i + 2] == '>') {
                    return pos + i;
                }
            }
        }
        pos += 16;
    }

    // Scalar tail
    while (pos + 2 < len) {
        if (data[pos] == '-' && data[pos+1] == '-' && data[pos+2] == '>') return pos;
        ++pos;
    }
    return len;
}

#else
// Non-ARM fallback stubs (should never be called on x86)
inline size_t skip_text_neon(const char*, size_t pos, size_t) noexcept { return pos; }
inline size_t find_char_neon(const char*, size_t pos, size_t, char) noexcept { return pos; }
inline size_t skip_whitespace_neon(const char*, size_t pos, size_t) noexcept { return pos; }
inline size_t read_name_neon(const char*, size_t pos, size_t) noexcept { return pos; }
inline size_t find_comment_end_neon(const char*, size_t pos, size_t) noexcept { return pos; }
#endif

} // namespace neon
} // namespace parshred
