/// @file scanner_sse42.cpp
/// @brief SSE 4.2 structural character scanner.
///
/// Processes 16 bytes at a time using _mm_cmpeq_epi8 to find structural
/// characters, with carry-less multiply for quote masking.

#include <parshred/simd_scanner.hpp>
#include <parshred/platform.hpp>

#include <immintrin.h>
#include <nmmintrin.h>

namespace parshred::detail {

/// Prefix-XOR via carry-less multiply: computes running XOR parity of bits.
/// This turns "toggle at each quote" into "mask of in-quote regions".
static inline uint64_t prefix_xor(uint64_t mask) {
    // clmul(mask, 0xFFFFFFFFFFFFFFFF) computes prefix-XOR
    __m128i m = _mm_set_epi64x(0, static_cast<long long>(mask));
    __m128i ones = _mm_set_epi64x(0, -1LL);
    __m128i result = _mm_clmulepi64_si128(m, ones, 0x00);
    return static_cast<uint64_t>(_mm_cvtsi128_si64(result));
}

static inline void extract_bits(uint32_t mask, uint32_t base_offset,
                                const char* data,
                                std::vector<uint32_t>& positions,
                                std::vector<uint8_t>& chars) {
    while (mask != 0) {
        int bit = parshred::ctz(mask);
        uint32_t pos = base_offset + static_cast<uint32_t>(bit);
        positions.push_back(pos);
        chars.push_back(static_cast<uint8_t>(data[pos]));
        mask &= mask - 1; // clear lowest set bit
    }
}

void scan_sse42(const char* data, size_t len,
                std::vector<uint32_t>& positions,
                std::vector<uint8_t>& chars)
{
    const size_t CHUNK = 16;

    // Broadcast each structural character
    __m128i v_lt  = _mm_set1_epi8('<');
    __m128i v_gt  = _mm_set1_epi8('>');
    __m128i v_sl  = _mm_set1_epi8('/');
    __m128i v_dq  = _mm_set1_epi8('"');
    __m128i v_sq  = _mm_set1_epi8('\'');
    __m128i v_eq  = _mm_set1_epi8('=');
    __m128i v_amp = _mm_set1_epi8('&');

    uint64_t prev_dq_state = 0; // carry-over double-quote parity
    uint64_t prev_sq_state = 0; // carry-over single-quote parity

    size_t i = 0;
    for (; i + CHUNK <= len; i += CHUNK) {
        __m128i chunk = _mm_loadu_si128(reinterpret_cast<const __m128i*>(data + i));

        // Compare for each structural character → 16-bit masks
        uint32_t m_lt  = static_cast<uint32_t>(_mm_movemask_epi8(_mm_cmpeq_epi8(chunk, v_lt)));
        uint32_t m_gt  = static_cast<uint32_t>(_mm_movemask_epi8(_mm_cmpeq_epi8(chunk, v_gt)));
        uint32_t m_sl  = static_cast<uint32_t>(_mm_movemask_epi8(_mm_cmpeq_epi8(chunk, v_sl)));
        uint32_t m_dq  = static_cast<uint32_t>(_mm_movemask_epi8(_mm_cmpeq_epi8(chunk, v_dq)));
        uint32_t m_sq  = static_cast<uint32_t>(_mm_movemask_epi8(_mm_cmpeq_epi8(chunk, v_sq)));
        uint32_t m_eq  = static_cast<uint32_t>(_mm_movemask_epi8(_mm_cmpeq_epi8(chunk, v_eq)));
        uint32_t m_amp = static_cast<uint32_t>(_mm_movemask_epi8(_mm_cmpeq_epi8(chunk, v_amp)));

        // Quote masking: compute regions inside double and single quotes.
        // prefix_xor on quote positions gives a mask of "inside quote" regions.
        uint64_t dq_mask = prefix_xor(m_dq) ^ prev_dq_state;
        prev_dq_state = (dq_mask >> 15) & 1 ? ~uint64_t(0) : 0; // carry the parity
        // Restrict to 16 bits
        dq_mask &= 0xFFFF;

        uint64_t sq_mask = prefix_xor(m_sq) ^ prev_sq_state;
        prev_sq_state = (sq_mask >> 15) & 1 ? ~uint64_t(0) : 0;
        sq_mask &= 0xFFFF;

        // Characters inside either quote type are masked out,
        // BUT the quote characters themselves are still structural.
        uint32_t in_quote = static_cast<uint32_t>(dq_mask | sq_mask);
        uint32_t structural = (m_lt | m_gt | m_sl | m_eq | m_amp) & ~in_quote;
        structural |= (m_dq | m_sq); // quotes are always structural

        uint32_t base = static_cast<uint32_t>(i);
        extract_bits(structural & 0xFFFF, base, data, positions, chars);
    }

    // Handle remaining bytes with scalar fallback
    char in_quote_char = 0;
    // Determine current quote state from carry
    if (prev_dq_state) in_quote_char = '"';
    else if (prev_sq_state) in_quote_char = '\'';

    for (; i < len; ++i) {
        char c = data[i];
        if (in_quote_char) {
            if (c == in_quote_char) {
                positions.push_back(static_cast<uint32_t>(i));
                chars.push_back(static_cast<uint8_t>(c));
                in_quote_char = 0;
            }
            continue;
        }
        switch (c) {
            case '<': case '>': case '/': case '=': case '&':
                positions.push_back(static_cast<uint32_t>(i));
                chars.push_back(static_cast<uint8_t>(c));
                break;
            case '"': case '\'':
                positions.push_back(static_cast<uint32_t>(i));
                chars.push_back(static_cast<uint8_t>(c));
                in_quote_char = c;
                break;
        }
    }
}

} // namespace parshred::detail

