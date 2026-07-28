/// @file scanner_avx2.cpp
/// @brief AVX2 structural character scanner.
///
/// Processes 32 bytes at a time using _mm256_cmpeq_epi8 to find structural
/// characters, with carry-less multiply for quote masking.

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

static inline void extract_bits_32(uint32_t mask, uint32_t base_offset,
                                   const char* data,
                                   std::vector<uint32_t>& positions,
                                   std::vector<uint8_t>& chars) {
    if (PARSHRED_UNLIKELY(mask == 0)) return;

    // Batch extract into stack buffers to avoid per-bit push_back overhead.
    // A 32-byte chunk has at most 32 structural characters.
    uint32_t pos_buf[32];
    uint8_t  chr_buf[32];
    int count = 0;

    while (mask != 0) {
        int bit = parshred::ctz(mask);
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

void scan_avx2(const char* data, size_t len,
               std::vector<uint32_t>& positions,
               std::vector<uint8_t>& chars)
{
    const size_t CHUNK = 32;

    __m256i v_lt  = _mm256_set1_epi8('<');
    __m256i v_gt  = _mm256_set1_epi8('>');
    __m256i v_sl  = _mm256_set1_epi8('/');
    __m256i v_dq  = _mm256_set1_epi8('"');
    __m256i v_sq  = _mm256_set1_epi8('\'');
    __m256i v_eq  = _mm256_set1_epi8('=');
    __m256i v_amp = _mm256_set1_epi8('&');

    uint64_t prev_dq_state = 0;
    uint64_t prev_sq_state = 0;

    size_t i = 0;
    for (; i + CHUNK <= len; i += CHUNK) {
        __m256i chunk = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(data + i));

        uint32_t m_lt  = static_cast<uint32_t>(_mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk, v_lt)));
        uint32_t m_gt  = static_cast<uint32_t>(_mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk, v_gt)));
        uint32_t m_sl  = static_cast<uint32_t>(_mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk, v_sl)));
        uint32_t m_dq  = static_cast<uint32_t>(_mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk, v_dq)));
        uint32_t m_sq  = static_cast<uint32_t>(_mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk, v_sq)));
        uint32_t m_eq  = static_cast<uint32_t>(_mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk, v_eq)));
        uint32_t m_amp = static_cast<uint32_t>(_mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk, v_amp)));

        // Quote masking via prefix-XOR
        uint64_t dq_mask = prefix_xor(m_dq) ^ prev_dq_state;
        prev_dq_state = (dq_mask >> 31) & 1 ? ~uint64_t(0) : 0;
        dq_mask &= 0xFFFFFFFF;

        uint64_t sq_mask = prefix_xor(m_sq) ^ prev_sq_state;
        prev_sq_state = (sq_mask >> 31) & 1 ? ~uint64_t(0) : 0;
        sq_mask &= 0xFFFFFFFF;

        uint32_t in_quote = static_cast<uint32_t>(dq_mask | sq_mask);
        uint32_t structural = (m_lt | m_gt | m_sl | m_eq | m_amp) & ~in_quote;
        structural |= (m_dq | m_sq); // quotes always structural

        extract_bits_32(structural, static_cast<uint32_t>(i), data, positions, chars);
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

    // Clean up AVX state
    _mm256_zeroupper();
}

} // namespace parshred::detail

