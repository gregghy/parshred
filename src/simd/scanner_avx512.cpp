/// @file scanner_avx512.cpp
/// @brief AVX-512 structural character scanner.
///
/// Processes 64 bytes at a time using _mm512_cmpeq_epi8_mask to find
/// structural characters. Uses carry-less multiply for quote masking.
/// This is the fastest backend — ideal for Zen 4+ and Skylake-X+.

#include <parshred/simd_scanner.hpp>
#include <parshred/platform.hpp>

#include <immintrin.h>
#include <cstring>

namespace parshred::detail {

static inline uint64_t prefix_xor(uint64_t mask) {
    __m128i m = _mm_set_epi64x(0, static_cast<long long>(mask));
    __m128i ones = _mm_set_epi64x(0, -1LL);
    __m128i result = _mm_clmulepi64_si128(m, ones, 0x00);
    return static_cast<uint64_t>(_mm_cvtsi128_si64(result));
}

static inline void extract_bits_64(uint64_t mask, uint32_t base_offset,
                                   const char* PARSHRED_RESTRICT data,
                                   std::vector<uint32_t>& positions,
                                   std::vector<uint8_t>& chars) {
    if (PARSHRED_UNLIKELY(mask == 0)) return;

    // Batch extract into stack buffers to avoid per-bit push_back overhead.
    // A 64-byte chunk has at most 64 structural characters.
    uint32_t pos_buf[64];
    uint8_t  chr_buf[64];
    int count = 0;

    while (mask != 0) {
        int bit = parshred::ctzll(mask);
        uint32_t pos = base_offset + static_cast<uint32_t>(bit);
        pos_buf[count] = pos;
        chr_buf[count] = static_cast<uint8_t>(data[pos]);
        ++count;
        mask &= mask - 1;
    }

    // Single bulk insert — one capacity check + memcpy instead of N push_backs.
    size_t old_size = positions.size();
    positions.resize(old_size + static_cast<size_t>(count));
    chars.resize(chars.size() + static_cast<size_t>(count));
    std::memcpy(positions.data() + old_size, pos_buf,
                static_cast<size_t>(count) * sizeof(uint32_t));
    std::memcpy(chars.data() + chars.size() - static_cast<size_t>(count),
                chr_buf, static_cast<size_t>(count));
}

void scan_avx512(const char* PARSHRED_RESTRICT data, size_t len,
                 std::vector<uint32_t>& positions,
                 std::vector<uint8_t>& chars)
{
    const size_t CHUNK = 64;
    // Prefetch 512 bytes ahead (8 cache lines) to hide DRAM latency.
    constexpr size_t PREFETCH_DIST = 512;

    __m512i v_lt  = _mm512_set1_epi8('<');
    __m512i v_gt  = _mm512_set1_epi8('>');
    __m512i v_sl  = _mm512_set1_epi8('/');
    __m512i v_dq  = _mm512_set1_epi8('"');
    __m512i v_sq  = _mm512_set1_epi8('\'');
    __m512i v_eq  = _mm512_set1_epi8('=');
    __m512i v_amp = _mm512_set1_epi8('&');

    uint64_t prev_dq_state = 0;
    uint64_t prev_sq_state = 0;

    size_t i = 0;
    for (; i + CHUNK <= len; i += CHUNK) {
        // Software prefetch for large files exceeding L3 cache.
        if (i + PREFETCH_DIST < len) {
            PARSHRED_PREFETCH_L2(data + i + PREFETCH_DIST);
        }

        __m512i chunk = _mm512_loadu_si512(reinterpret_cast<const __m512i*>(data + i));

        // AVX-512 cmpeq returns a 64-bit mask register directly — no movemask needed!
        uint64_t m_lt  = _mm512_cmpeq_epi8_mask(chunk, v_lt);
        uint64_t m_gt  = _mm512_cmpeq_epi8_mask(chunk, v_gt);
        uint64_t m_sl  = _mm512_cmpeq_epi8_mask(chunk, v_sl);
        uint64_t m_dq  = _mm512_cmpeq_epi8_mask(chunk, v_dq);
        uint64_t m_sq  = _mm512_cmpeq_epi8_mask(chunk, v_sq);
        uint64_t m_eq  = _mm512_cmpeq_epi8_mask(chunk, v_eq);
        uint64_t m_amp = _mm512_cmpeq_epi8_mask(chunk, v_amp);

        // Quote masking via prefix-XOR
        uint64_t dq_mask = prefix_xor(m_dq) ^ prev_dq_state;
        prev_dq_state = (dq_mask >> 63) ? ~uint64_t(0) : 0;

        uint64_t sq_mask = prefix_xor(m_sq) ^ prev_sq_state;
        prev_sq_state = (sq_mask >> 63) ? ~uint64_t(0) : 0;

        uint64_t in_quote = dq_mask | sq_mask;
        uint64_t structural = (m_lt | m_gt | m_sl | m_eq | m_amp) & ~in_quote;
        structural |= (m_dq | m_sq); // quotes always emitted

        extract_bits_64(structural, static_cast<uint32_t>(i), data, positions, chars);
    }

    // Tail with scalar
    char in_quote_char = 0;
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

