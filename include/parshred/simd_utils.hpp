#pragma once
/// @file simd_utils.hpp
/// @brief SIMD-accelerated scanning primitives for the parser hot path.
///
/// These skip over names, whitespace, and text content 32/64 bytes at a
/// time using AVX2/AVX-512 instead of byte-at-a-time loops.

#include <cstddef>
#include <cstdint>

#ifdef __x86_64__
#include <immintrin.h>
#endif

#include <parshred/lookup_tables.hpp>

namespace parshred {

/// Skip whitespace starting at `pos`, return first non-whitespace position.
/// Falls back to scalar when fewer than 32 bytes remain.
inline size_t skip_whitespace_fast(const char* data, size_t pos, size_t len) noexcept {
#ifdef __AVX2__
    // Whitespace: 0x09 (\t), 0x0A (\n), 0x0D (\r), 0x20 (space)
    // All <= 0x20 and specifically those 4 values.
    // Fast check: compare each byte against ' ' (0x20). If <= 0x20, check exact.
    // Even faster: just check if any byte is NOT whitespace.
    const __m256i v_space = _mm256_set1_epi8(' ');
    const __m256i v_tab   = _mm256_set1_epi8('\t');
    const __m256i v_nl    = _mm256_set1_epi8('\n');
    const __m256i v_cr    = _mm256_set1_epi8('\r');

    while (pos + 32 <= len) {
        __m256i chunk = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(data + pos));
        // A byte is whitespace if it matches any of the 4 whitespace chars
        __m256i is_ws = _mm256_or_si256(
            _mm256_or_si256(
                _mm256_cmpeq_epi8(chunk, v_space),
                _mm256_cmpeq_epi8(chunk, v_tab)
            ),
            _mm256_or_si256(
                _mm256_cmpeq_epi8(chunk, v_nl),
                _mm256_cmpeq_epi8(chunk, v_cr)
            )
        );
        // Invert: find first non-whitespace
        uint32_t not_ws = ~static_cast<uint32_t>(_mm256_movemask_epi8(is_ws));
        if (not_ws != 0) {
            return pos + static_cast<size_t>(__builtin_ctz(not_ws));
        }
        pos += 32;
    }
#endif
    // Scalar tail
    while (pos < len && parshred::is_whitespace(data[pos])) ++pos;
    return pos;
}

/// Read a name starting at `pos`. Returns position past the last name char.
/// Uses SIMD vpshufb-based classification for 32 bytes at a time,
/// with scalar fallback for the tail.
inline size_t read_name_fast(const char* data, size_t pos, size_t len) noexcept {
    if (pos >= len || !is_name_start(data[pos])) return pos;
    ++pos;

#ifdef __AVX2__
    // vpshufb-based name character classification.
    // Valid name chars: a-z A-Z 0-9 _ : - . and >= 0x80
    //
    // We use a "low nibble / high nibble" approach:
    // For each byte, extract low nibble (0-15) and high nibble (0-15).
    // Two lookup tables tell us if the byte could be a name char.
    //
    // High nibble lookup: for each high nibble value, a bitmask of which
    // low nibbles produce valid name chars at that row.
    // Low nibble lookup: for each low nibble value, a bitmask of which
    // high nibbles could produce valid name chars at that column.
    //
    // If (high_result & low_result) != 0, the byte is a name char.
    //
    // Actually, simpler: use the "non-name char detection" approach.
    // A byte is NOT a name char if: 0x00-0x2C, 0x2F, 0x3B-0x40, 0x5B-0x5E, 0x60, 0x7B-0x7F
    // A byte IS a name char if: 0x2D-0x2E, 0x30-0x3A, 0x41-0x5A, 0x5F, 0x61-0x7A, 0x80-0xFF
    //
    // Use range-based approach with saturating subtraction:
    // For ASCII name chars, they fall into these ranges:
    //   '-' (0x2D) '.' (0x2E)
    //   '0'-'9' (0x30-0x39) ':' (0x3A)  
    //   'A'-'Z' (0x41-0x5A)
    //   '_' (0x5F)
    //   'a'-'z' (0x61-0x7A)
    //   >= 0x80
    //
    // Strategy: check multiple ranges with SIMD comparisons.
    if (pos + 32 <= len) {
        // Precompute range boundaries
        const __m256i v_2c  = _mm256_set1_epi8(0x2C);  // one below '-'
        const __m256i v_2f2 = _mm256_set1_epi8(0x2F); // '/' — upper bound for '-','.'
        const __m256i v_30  = _mm256_set1_epi8(0x2F);  // one below '0'
        const __m256i v_3b  = _mm256_set1_epi8(0x3B);  // one above ':'
        const __m256i v_40  = _mm256_set1_epi8(0x40);  // one below 'A'
        const __m256i v_5f  = _mm256_set1_epi8(0x5F);  // '_'
        const __m256i v_60  = _mm256_set1_epi8(0x60);  // one below 'a'
        const __m256i v_80  = _mm256_set1_epi8(static_cast<char>(0x80));

        while (pos + 32 <= len) {
            __m256i chunk = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(data + pos));
            
            // Check each range independently, OR them together
            // Range 1: '-' '.' (0x2D-0x2E)
            __m256i r1 = _mm256_and_si256(
                _mm256_cmpgt_epi8(chunk, v_2c),   // > 0x2C
                _mm256_cmpgt_epi8(v_2f2, chunk)); // < 0x2F  (i.e., 0x2D or 0x2E)

            // Range 2: '0'-':' (0x30-0x3A)
            __m256i r2 = _mm256_and_si256(
                _mm256_cmpgt_epi8(chunk, v_30),   // > 0x2F
                _mm256_cmpgt_epi8(v_3b, chunk));  // < 0x3B

            // Range 3: 'A'-'Z' (0x41-0x5A)
            __m256i r3 = _mm256_and_si256(
                _mm256_cmpgt_epi8(chunk, v_40),   // > 0x40
                _mm256_cmpgt_epi8(_mm256_set1_epi8(0x5B), chunk)); // < 0x5B

            // Range 4: '_' (0x5F) — single char
            __m256i r4 = _mm256_cmpeq_epi8(chunk, v_5f);

            // Range 5: 'a'-'z' (0x61-0x7A)
            __m256i r5 = _mm256_and_si256(
                _mm256_cmpgt_epi8(chunk, v_60),   // > 0x60
                _mm256_cmpgt_epi8(_mm256_set1_epi8(0x7B), chunk)); // < 0x7B

            // Range 6: >= 0x80 (high bit set)
            // Use signed comparison: byte >= 0x80 means it's negative in signed
            __m256i r6 = _mm256_cmpgt_epi8(v_80, chunk); // Wait, this checks < 0x80
            // Actually for signed comparison: bytes >= 0x80 are negative, so < 0 in signed
            // _mm256_cmpgt_epi8(zero, chunk) would give us bytes where chunk is negative
            __m256i v_zero = _mm256_setzero_si256();
            r6 = _mm256_cmpgt_epi8(v_zero, chunk); // chunk < 0 (i.e., >= 0x80 unsigned)

            // OR all ranges together
            __m256i is_name = _mm256_or_si256(
                _mm256_or_si256(_mm256_or_si256(r1, r2), _mm256_or_si256(r3, r4)),
                _mm256_or_si256(r5, r6));

            // Find first byte that is NOT a name char
            uint32_t not_name = ~static_cast<uint32_t>(_mm256_movemask_epi8(is_name));
            if (not_name != 0) {
                return pos + static_cast<size_t>(__builtin_ctz(not_name));
            }
            pos += 32;
        }
    }
#endif
    // Scalar tail: lookup table makes this just one indexed load per byte
    while (pos < len && is_name_char(data[pos])) ++pos;
    return pos;
}

/// Find the position of a character in data[pos..len), or return len if not found.
/// Uses SIMD to scan 32 bytes at a time.
inline size_t find_char_fast(const char* data, size_t pos, size_t len, char target) noexcept {
#ifdef __AVX2__
    const __m256i v_target = _mm256_set1_epi8(target);
    while (pos + 32 <= len) {
        __m256i chunk = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(data + pos));
        uint32_t mask = static_cast<uint32_t>(
            _mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk, v_target)));
        if (mask != 0) {
            return pos + static_cast<size_t>(__builtin_ctz(mask));
        }
        pos += 32;
    }
#endif
    while (pos < len && data[pos] != target) ++pos;
    return pos;
}

/// Find the position of a character that is NOT a text character (i.e., find '<' or '&').
/// This is the hot loop for skipping text content between tags.
inline size_t skip_text_fast(const char* data, size_t pos, size_t len) noexcept {
#ifdef __AVX2__
    const __m256i v_lt  = _mm256_set1_epi8('<');
    const __m256i v_amp = _mm256_set1_epi8('&');
    while (pos + 32 <= len) {
        __m256i chunk = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(data + pos));
        uint32_t hit = static_cast<uint32_t>(_mm256_movemask_epi8(
            _mm256_or_si256(
                _mm256_cmpeq_epi8(chunk, v_lt),
                _mm256_cmpeq_epi8(chunk, v_amp)
            )
        ));
        if (hit != 0) {
            return pos + static_cast<size_t>(__builtin_ctz(hit));
        }
        pos += 32;
    }
#endif
    while (pos < len && data[pos] != '<' && data[pos] != '&') ++pos;
    return pos;
}

/// Skip text content in turbo mode (no entity detection — only stop at '<').
inline size_t skip_text_turbo(const char* data, size_t pos, size_t len) noexcept {
#ifdef __AVX2__
    const __m256i v_lt = _mm256_set1_epi8('<');
    while (pos + 32 <= len) {
        __m256i chunk = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(data + pos));
        uint32_t hit = static_cast<uint32_t>(
            _mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk, v_lt)));
        if (hit != 0) {
            return pos + static_cast<size_t>(__builtin_ctz(hit));
        }
        pos += 32;
    }
#endif
    while (pos < len && data[pos] != '<') ++pos;
    return pos;
}

/// Find the end of a quoted attribute value. Scans for the closing quote char.
inline size_t skip_attr_value(const char* data, size_t pos, size_t len, char quote) noexcept {
    return find_char_fast(data, pos, len, quote);
}

/// Find pattern "-->" starting at pos. Returns position of '-' or len if not found.
inline size_t find_comment_end(const char* data, size_t pos, size_t len) noexcept {
#ifdef __AVX2__
    const __m256i v_dash = _mm256_set1_epi8('-');
    while (pos + 34 <= len) {  // need 3 chars for "-->"
        __m256i chunk = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(data + pos));
        uint32_t dashes = static_cast<uint32_t>(
            _mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk, v_dash)));
        while (dashes != 0) {
            int bit = __builtin_ctz(dashes);
            size_t p = pos + static_cast<size_t>(bit);
            if (p + 2 < len && data[p + 1] == '-' && data[p + 2] == '>') {
                return p;
            }
            dashes &= dashes - 1;
        }
        pos += 32;
    }
#endif
    while (pos + 2 < len) {
        if (data[pos] == '-' && data[pos + 1] == '-' && data[pos + 2] == '>') return pos;
        ++pos;
    }
    return len;
}

/// Find pattern "]]>" starting at pos.
inline size_t find_cdata_end(const char* data, size_t pos, size_t len) noexcept {
#ifdef __AVX2__
    const __m256i v_bracket = _mm256_set1_epi8(']');
    while (pos + 34 <= len) {
        __m256i chunk = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(data + pos));
        uint32_t brackets = static_cast<uint32_t>(
            _mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk, v_bracket)));
        while (brackets != 0) {
            int bit = __builtin_ctz(brackets);
            size_t p = pos + static_cast<size_t>(bit);
            if (p + 2 < len && data[p + 1] == ']' && data[p + 2] == '>') {
                return p;
            }
            brackets &= brackets - 1;
        }
        pos += 32;
    }
#endif
    while (pos + 2 < len) {
        if (data[pos] == ']' && data[pos + 1] == ']' && data[pos + 2] == '>') return pos;
        ++pos;
    }
    return len;
}

/// Find "?>" starting at pos.
inline size_t find_pi_end(const char* data, size_t pos, size_t len) noexcept {
#ifdef __AVX2__
    const __m256i v_q = _mm256_set1_epi8('?');
    while (pos + 33 <= len) {
        __m256i chunk = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(data + pos));
        uint32_t qs = static_cast<uint32_t>(
            _mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk, v_q)));
        while (qs != 0) {
            int bit = __builtin_ctz(qs);
            size_t p = pos + static_cast<size_t>(bit);
            if (p + 1 < len && data[p + 1] == '>') return p;
            qs &= qs - 1;
        }
        pos += 32;
    }
#endif
    while (pos + 1 < len) {
        if (data[pos] == '?' && data[pos + 1] == '>') return pos;
        ++pos;
    }
    return len;
}

} // namespace parshred
