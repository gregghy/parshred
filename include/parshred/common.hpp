// SPDX-License-Identifier: AGPL-3.0-or-later OR Commercial
// Copyright (C) 2024-2025 Parshred Contributors
#pragma once
/// @file common.hpp
/// @brief Common types, error codes, and utilities for parshred.

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <span>

namespace parshred {

// ── Version ───────────────────────────────────────────────────────────
inline constexpr int VERSION_MAJOR = 0;
inline constexpr int VERSION_MINOR = 1;
inline constexpr int VERSION_PATCH = 0;

// ── Error handling ────────────────────────────────────────────────────
/// Exception thrown for XML parsing errors.
class ParseError : public std::runtime_error {
public:
    ParseError(const std::string& msg, size_t offset)
        : std::runtime_error(msg), offset_(offset) {}

    /// Byte offset in the input where the error occurred.
    [[nodiscard]] size_t offset() const noexcept { return offset_; }

private:
    size_t offset_;
};

/// Exception thrown for I/O errors (file not found, mmap failure, etc.)
class IOError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// Exception thrown when security limits are exceeded.
class SecurityError : public ParseError {
public:
    using ParseError::ParseError;
};

// ── Token types ───────────────────────────────────────────────────────
enum class TokenType : uint8_t {
    StartTag,              // <name
    EndTag,                // </name>
    SelfClosingTag,        // <name ... />
    AttributeName,         // attr (inside a start/self-closing tag)
    AttributeValue,        // "value" (inside a start/self-closing tag)
    Text,                  // character data between tags
    CData,                 // <![CDATA[...]]>
    Comment,               // <!-- ... -->
    ProcessingInstruction, // <?target ...?>
    EntityRef,             // &name;
    DocType,               // <!DOCTYPE ...>
    XmlDeclaration,        // <?xml ...?>
    EndOfInput,            // sentinel
};

/// Returns a human-readable name for a token type.
[[nodiscard]] constexpr const char* token_type_name(TokenType t) noexcept {
    switch (t) {
        case TokenType::StartTag:              return "StartTag";
        case TokenType::EndTag:                return "EndTag";
        case TokenType::SelfClosingTag:        return "SelfClosingTag";
        case TokenType::AttributeName:         return "AttributeName";
        case TokenType::AttributeValue:        return "AttributeValue";
        case TokenType::Text:                  return "Text";
        case TokenType::CData:                 return "CData";
        case TokenType::Comment:               return "Comment";
        case TokenType::ProcessingInstruction: return "ProcessingInstruction";
        case TokenType::EntityRef:             return "EntityRef";
        case TokenType::DocType:               return "DocType";
        case TokenType::XmlDeclaration:        return "XmlDeclaration";
        case TokenType::EndOfInput:            return "EndOfInput";
    }
    return "Unknown";
}

/// A single token produced by the tokenizer.
/// All string_views point into the original (mmap'd) input — zero-copy.
struct Token {
    TokenType        type   = TokenType::EndOfInput;
    std::string_view text   = {};   // The textual content / name
    size_t           offset = 0;    // Byte offset in input
};

/// A name=value attribute pair (zero-copy views into input).
struct Attribute {
    std::string_view name;
    std::string_view value;
};

// ── Security defaults ─────────────────────────────────────────────────
inline constexpr size_t DEFAULT_MAX_DEPTH             = 512;
inline constexpr size_t DEFAULT_MAX_ENTITY_EXPANSIONS = 10'000;
inline constexpr size_t DEFAULT_MAX_ATTRIBUTE_COUNT   = 1'000;

} // namespace parshred
