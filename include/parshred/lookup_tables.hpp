#pragma once
/// @file lookup_tables.hpp
/// @brief 256-byte lookup tables for O(1) character classification.
///
/// Inspired by RapidXML's approach: a single indexed load replaces
/// chains of comparisons in the hot path. Each table entry is a
/// bitmask indicating which character classes the byte belongs to.

#include <cstdint>

namespace parshred {

/// Bitmask flags for character classification.
enum CharClass : uint8_t {
    CHCLS_NONE       = 0,
    CHCLS_WHITESPACE = 1 << 0,  // ' ', '\t', '\n', '\r'
    CHCLS_NAME_START = 1 << 1,  // a-z A-Z _ :
    CHCLS_NAME       = 1 << 2,  // a-z A-Z 0-9 _ : - .
    CHCLS_DIGIT      = 1 << 3,  // 0-9
    CHCLS_TAG_END    = 1 << 4,  // > / (chars that end attribute lists)
    CHCLS_STRUCTURAL = 1 << 5,  // < > / = &
    CHCLS_QUOTE      = 1 << 6,  // " '
    CHCLS_HIGH       = 1 << 7,  // >= 0x80 (UTF-8 continuation / multibyte start)
};

namespace detail {

// Build the table at compile time.
constexpr uint8_t classify(unsigned ch) {
    uint8_t r = 0;

    // Whitespace
    if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r')
        r |= CHCLS_WHITESPACE;

    // Digits
    if (ch >= '0' && ch <= '9')
        r |= CHCLS_DIGIT;

    // Name-start: a-z A-Z _ :
    if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
        ch == '_' || ch == ':')
        r |= CHCLS_NAME_START | CHCLS_NAME;
    // Name: also includes digits, -, .
    if ((ch >= '0' && ch <= '9') || ch == '-' || ch == '.')
        r |= CHCLS_NAME;

    // Tag-end chars (end attribute list parsing)
    if (ch == '>' || ch == '/')
        r |= CHCLS_TAG_END;

    // Structural
    if (ch == '<' || ch == '>' || ch == '/' || ch == '=' || ch == '&')
        r |= CHCLS_STRUCTURAL;

    // Quotes
    if (ch == '"' || ch == '\'')
        r |= CHCLS_QUOTE;

    // High bytes
    if (ch >= 0x80)
        r |= CHCLS_HIGH | CHCLS_NAME_START | CHCLS_NAME;  // UTF-8 bytes are valid in names

    return r;
}

struct LookupTable {
    uint8_t data[256];

    constexpr LookupTable() : data{} {
        for (unsigned i = 0; i < 256; ++i)
            data[i] = classify(i);
    }

    constexpr uint8_t operator[](unsigned char ch) const noexcept {
        return data[ch];
    }
};

} // namespace detail

/// Global lookup table — constexpr-initialized, zero runtime cost.
inline constexpr detail::LookupTable char_table{};

/// O(1) character classification functions.
inline bool is_whitespace(char c) noexcept {
    return char_table[static_cast<unsigned char>(c)] & CHCLS_WHITESPACE;
}
inline bool is_name_start(char c) noexcept {
    return char_table[static_cast<unsigned char>(c)] & CHCLS_NAME_START;
}
inline bool is_name_char(char c) noexcept {
    return char_table[static_cast<unsigned char>(c)] & CHCLS_NAME;
}
inline bool is_tag_end(char c) noexcept {
    return char_table[static_cast<unsigned char>(c)] & CHCLS_TAG_END;
}
inline bool is_quote(char c) noexcept {
    return char_table[static_cast<unsigned char>(c)] & CHCLS_QUOTE;
}

} // namespace parshred
