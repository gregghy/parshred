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
    CC_NONE       = 0,
    CC_WHITESPACE = 1 << 0,  // ' ', '\t', '\n', '\r'
    CC_NAME_START = 1 << 1,  // a-z A-Z _ :
    CC_NAME       = 1 << 2,  // a-z A-Z 0-9 _ : - .
    CC_DIGIT      = 1 << 3,  // 0-9
    CC_TAG_END    = 1 << 4,  // > / (chars that end attribute lists)
    CC_STRUCTURAL = 1 << 5,  // < > / = &
    CC_QUOTE      = 1 << 6,  // " '
    CC_HIGH       = 1 << 7,  // >= 0x80 (UTF-8 continuation / multibyte start)
};

namespace detail {

// Build the table at compile time.
constexpr uint8_t classify(unsigned ch) {
    uint8_t r = 0;

    // Whitespace
    if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r')
        r |= CC_WHITESPACE;

    // Digits
    if (ch >= '0' && ch <= '9')
        r |= CC_DIGIT;

    // Name-start: a-z A-Z _ :
    if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
        ch == '_' || ch == ':')
        r |= CC_NAME_START | CC_NAME;
    // Name: also includes digits, -, .
    if ((ch >= '0' && ch <= '9') || ch == '-' || ch == '.')
        r |= CC_NAME;

    // Tag-end chars (end attribute list parsing)
    if (ch == '>' || ch == '/')
        r |= CC_TAG_END;

    // Structural
    if (ch == '<' || ch == '>' || ch == '/' || ch == '=' || ch == '&')
        r |= CC_STRUCTURAL;

    // Quotes
    if (ch == '"' || ch == '\'')
        r |= CC_QUOTE;

    // High bytes
    if (ch >= 0x80)
        r |= CC_HIGH | CC_NAME_START | CC_NAME;  // UTF-8 bytes are valid in names

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
    return char_table[static_cast<unsigned char>(c)] & CC_WHITESPACE;
}
inline bool is_name_start(char c) noexcept {
    return char_table[static_cast<unsigned char>(c)] & CC_NAME_START;
}
inline bool is_name_char(char c) noexcept {
    return char_table[static_cast<unsigned char>(c)] & CC_NAME;
}
inline bool is_tag_end(char c) noexcept {
    return char_table[static_cast<unsigned char>(c)] & CC_TAG_END;
}
inline bool is_quote(char c) noexcept {
    return char_table[static_cast<unsigned char>(c)] & CC_QUOTE;
}

} // namespace parshred
