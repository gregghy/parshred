#pragma once
/// @file encoding.hpp
/// @brief Encoding detection, line normalization, and transcoding for parshred.

#include <parshred/common.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace parshred {

// ── Encoding enum ─────────────────────────────────────────────────────

enum class Encoding : uint8_t {
    UTF8,
    UTF16_LE,
    UTF16_BE,
    UTF32_LE,
    UTF32_BE,
    ISO_8859_1,
    ASCII,
    Unknown,
};

/// Returns a human-readable name for an encoding.
[[nodiscard]] inline constexpr const char* encoding_name(Encoding e) noexcept {
    switch (e) {
        case Encoding::UTF8:       return "UTF-8";
        case Encoding::UTF16_LE:   return "UTF-16LE";
        case Encoding::UTF16_BE:   return "UTF-16BE";
        case Encoding::UTF32_LE:   return "UTF-32LE";
        case Encoding::UTF32_BE:   return "UTF-32BE";
        case Encoding::ISO_8859_1: return "ISO-8859-1";
        case Encoding::ASCII:      return "ASCII";
        case Encoding::Unknown:    return "Unknown";
    }
    return "Unknown";
}

// ── BOM detection ─────────────────────────────────────────────────────

/// Returns the number of bytes in the BOM at the beginning of `data`,
/// or 0 if no BOM is present.
[[nodiscard]] inline size_t skip_bom(const char* data, size_t len) noexcept {
    if (!data || len == 0) return 0;

    const auto* u = reinterpret_cast<const unsigned char*>(data);

    // UTF-32 LE: FF FE 00 00 (must check before UTF-16 LE since it shares prefix)
    if (len >= 4 && u[0] == 0xFF && u[1] == 0xFE && u[2] == 0x00 && u[3] == 0x00)
        return 4;

    // UTF-32 BE: 00 00 FE FF
    if (len >= 4 && u[0] == 0x00 && u[1] == 0x00 && u[2] == 0xFE && u[3] == 0xFF)
        return 4;

    // UTF-8 BOM: EF BB BF
    if (len >= 3 && u[0] == 0xEF && u[1] == 0xBB && u[2] == 0xBF)
        return 3;

    // UTF-16 LE: FF FE (not followed by 00 00, already checked above)
    if (len >= 2 && u[0] == 0xFF && u[1] == 0xFE)
        return 2;

    // UTF-16 BE: FE FF
    if (len >= 2 && u[0] == 0xFE && u[1] == 0xFF)
        return 2;

    return 0;
}

// ── Internal helpers ──────────────────────────────────────────────────

namespace detail {

/// Detect encoding from BOM only.
[[nodiscard]] inline Encoding detect_from_bom(const char* data, size_t len) noexcept {
    if (!data || len == 0) return Encoding::Unknown;

    const auto* u = reinterpret_cast<const unsigned char*>(data);

    // UTF-32 LE: FF FE 00 00
    if (len >= 4 && u[0] == 0xFF && u[1] == 0xFE && u[2] == 0x00 && u[3] == 0x00)
        return Encoding::UTF32_LE;

    // UTF-32 BE: 00 00 FE FF
    if (len >= 4 && u[0] == 0x00 && u[1] == 0x00 && u[2] == 0xFE && u[3] == 0xFF)
        return Encoding::UTF32_BE;

    // UTF-8: EF BB BF
    if (len >= 3 && u[0] == 0xEF && u[1] == 0xBB && u[2] == 0xBF)
        return Encoding::UTF8;

    // UTF-16 LE: FF FE
    if (len >= 2 && u[0] == 0xFF && u[1] == 0xFE)
        return Encoding::UTF16_LE;

    // UTF-16 BE: FE FF
    if (len >= 2 && u[0] == 0xFE && u[1] == 0xFF)
        return Encoding::UTF16_BE;

    return Encoding::Unknown;
}

/// Convert an encoding name string (case-insensitive) to Encoding enum.
[[nodiscard]] inline Encoding encoding_from_name(std::string_view name) noexcept {
    // Lowercase copy for comparison
    auto lower = [](std::string_view sv) {
        std::string s(sv);
        for (auto& c : s) {
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
        }
        // Remove hyphens and underscores for normalized comparison
        return s;
    };

    std::string norm = lower(name);

    if (norm == "utf-8" || norm == "utf8") return Encoding::UTF8;
    if (norm == "utf-16le" || norm == "utf-16 le" || norm == "utf16le") return Encoding::UTF16_LE;
    if (norm == "utf-16be" || norm == "utf-16 be" || norm == "utf16be") return Encoding::UTF16_BE;
    if (norm == "utf-16" || norm == "utf16") return Encoding::UTF16_BE; // default to BE per spec
    if (norm == "utf-32le" || norm == "utf32le") return Encoding::UTF32_LE;
    if (norm == "utf-32be" || norm == "utf32be") return Encoding::UTF32_BE;
    if (norm == "utf-32" || norm == "utf32") return Encoding::UTF32_BE;
    if (norm == "iso-8859-1" || norm == "iso88591" || norm == "latin1" ||
        norm == "latin-1" || norm == "iso_8859_1" || norm == "iso-8859-1")
        return Encoding::ISO_8859_1;
    if (norm == "ascii" || norm == "us-ascii" || norm == "usascii")
        return Encoding::ASCII;

    return Encoding::Unknown;
}

/// Try to extract the encoding= attribute from an XML declaration in UTF-8/ASCII text.
/// Returns Encoding::Unknown if not found.
[[nodiscard]] inline Encoding detect_from_xml_declaration(const char* data, size_t len) noexcept {
    if (!data || len < 6) return Encoding::Unknown;

    // Check for <?xml at the start
    if (data[0] != '<' || data[1] != '?' ||
        (data[2] != 'x' && data[2] != 'X') ||
        (data[3] != 'm' && data[3] != 'M') ||
        (data[4] != 'l' && data[4] != 'L'))
        return Encoding::Unknown;

    // Find the end of the declaration '?>'
    size_t end = 5;
    while (end + 1 < len) {
        if (data[end] == '?' && data[end + 1] == '>') break;
        ++end;
    }
    if (end + 1 >= len) return Encoding::Unknown;

    // Search for encoding= within [5, end)
    std::string_view decl(data + 5, end - 5);
    auto pos = decl.find("encoding");
    if (pos == std::string_view::npos) return Encoding::Unknown;

    pos += 8; // skip "encoding"
    // skip whitespace and '='
    while (pos < decl.size() && (decl[pos] == ' ' || decl[pos] == '\t' ||
                                  decl[pos] == '\r' || decl[pos] == '\n'))
        ++pos;
    if (pos >= decl.size() || decl[pos] != '=') return Encoding::Unknown;
    ++pos;
    while (pos < decl.size() && (decl[pos] == ' ' || decl[pos] == '\t' ||
                                  decl[pos] == '\r' || decl[pos] == '\n'))
        ++pos;

    if (pos >= decl.size()) return Encoding::Unknown;

    char quote = decl[pos];
    if (quote != '\'' && quote != '"') return Encoding::Unknown;
    ++pos;

    auto end_quote = decl.find(quote, pos);
    if (end_quote == std::string_view::npos) return Encoding::Unknown;

    std::string_view enc_name = decl.substr(pos, end_quote - pos);
    return encoding_from_name(enc_name);
}

} // namespace detail

// ── detect_encoding ───────────────────────────────────────────────────

/// Detect the encoding of an XML document from its BOM and/or XML declaration.
/// The XML declaration's encoding= attribute overrides BOM detection when both are
/// present and the BOM is UTF-8 (since UTF-8 BOM is informational).
[[nodiscard]] inline Encoding detect_encoding(const char* data, size_t len) noexcept {
    if (!data || len == 0) return Encoding::Unknown;

    Encoding bom_enc = detail::detect_from_bom(data, len);

    // For UTF-16/32 BOMs, trust the BOM (XML decl would need transcoding to read)
    if (bom_enc == Encoding::UTF16_LE || bom_enc == Encoding::UTF16_BE ||
        bom_enc == Encoding::UTF32_LE || bom_enc == Encoding::UTF32_BE)
        return bom_enc;

    // Skip UTF-8 BOM if present, then look at XML declaration
    size_t bom_len = skip_bom(data, len);
    const char* after_bom = data + bom_len;
    size_t remaining = len - bom_len;

    Encoding decl_enc = detail::detect_from_xml_declaration(after_bom, remaining);

    // XML declaration encoding= overrides when present
    if (decl_enc != Encoding::Unknown) return decl_enc;

    // If we had a UTF-8 BOM, return UTF-8
    if (bom_enc == Encoding::UTF8) return Encoding::UTF8;

    // No BOM, no declaration — assume UTF-8 (most common, default per XML spec)
    return Encoding::UTF8;
}

// ── normalize_line_endings ────────────────────────────────────────────

/// Normalize line endings in-place per XML 1.0 section 2.11:
///   - \r\n → \n
///   - standalone \r → \n
/// Modifies the string in place and may shrink it.
inline void normalize_line_endings(std::string& data) noexcept {
    if (data.empty()) return;

    size_t write = 0;
    size_t read = 0;
    const size_t size = data.size();

    while (read < size) {
        if (data[read] == '\r') {
            data[write++] = '\n';
            ++read;
            // If \r is followed by \n, skip the \n (already wrote \n)
            if (read < size && data[read] == '\n') {
                ++read;
            }
        } else {
            data[write++] = data[read++];
        }
    }

    data.resize(write);
}

// ── validate_utf8 ────────────────────────────────────────────────────

/// Validate a UTF-8 byte sequence.
/// Returns the byte offset of the first invalid sequence, or -1 (as ptrdiff_t)
/// if the entire input is valid UTF-8.
[[nodiscard]] inline ptrdiff_t validate_utf8(const char* data, size_t len) noexcept {
    if (!data || len == 0) return -1;

    const auto* u = reinterpret_cast<const unsigned char*>(data);
    size_t i = 0;

    while (i < len) {
        unsigned char b0 = u[i];

        if (b0 <= 0x7F) {
            // ASCII byte
            ++i;
            continue;
        }

        size_t seq_len = 0;
        uint32_t codepoint = 0;
        uint32_t min_cp = 0;

        if ((b0 & 0xE0) == 0xC0) {
            // 2-byte sequence: 110xxxxx 10xxxxxx
            seq_len = 2;
            codepoint = b0 & 0x1F;
            min_cp = 0x80;
        } else if ((b0 & 0xF0) == 0xE0) {
            // 3-byte sequence: 1110xxxx 10xxxxxx 10xxxxxx
            seq_len = 3;
            codepoint = b0 & 0x0F;
            min_cp = 0x800;
        } else if ((b0 & 0xF8) == 0xF0) {
            // 4-byte sequence: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
            seq_len = 4;
            codepoint = b0 & 0x07;
            min_cp = 0x10000;
        } else {
            // Invalid leading byte (10xxxxxx, 11111xxx, etc.)
            return static_cast<ptrdiff_t>(i);
        }

        // Check we have enough continuation bytes
        if (i + seq_len > len) {
            return static_cast<ptrdiff_t>(i);
        }

        // Validate continuation bytes and accumulate codepoint
        for (size_t j = 1; j < seq_len; ++j) {
            unsigned char bj = u[i + j];
            if ((bj & 0xC0) != 0x80) {
                return static_cast<ptrdiff_t>(i);
            }
            codepoint = (codepoint << 6) | (bj & 0x3F);
        }

        // Check for overlong encodings
        if (codepoint < min_cp) {
            return static_cast<ptrdiff_t>(i);
        }

        // Check for surrogates (U+D800 - U+DFFF)
        if (codepoint >= 0xD800 && codepoint <= 0xDFFF) {
            return static_cast<ptrdiff_t>(i);
        }

        // Check for values beyond Unicode range
        if (codepoint > 0x10FFFF) {
            return static_cast<ptrdiff_t>(i);
        }

        i += seq_len;
    }

    return -1;
}

// ── transcode_to_utf8 ────────────────────────────────────────────────

namespace detail {

/// Encode a single Unicode codepoint as UTF-8 and append to `out`.
inline void encode_utf8(uint32_t cp, std::string& out) {
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0x10FFFF) {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        // Replacement character U+FFFD
        out.push_back(static_cast<char>(0xEF));
        out.push_back(static_cast<char>(0xBF));
        out.push_back(static_cast<char>(0xBD));
    }
}

/// Transcode UTF-16 LE to UTF-8.
[[nodiscard]] inline std::string utf16le_to_utf8(const char* data, size_t len) {
    std::string result;
    result.reserve(len); // rough estimate

    const auto* u = reinterpret_cast<const unsigned char*>(data);
    size_t i = 0;

    while (i + 1 < len) {
        uint16_t code_unit = static_cast<uint16_t>(u[i]) |
                             (static_cast<uint16_t>(u[i + 1]) << 8);
        i += 2;

        if (code_unit >= 0xD800 && code_unit <= 0xDBFF) {
            // High surrogate — need low surrogate
            if (i + 1 >= len) {
                encode_utf8(0xFFFD, result); // incomplete surrogate pair
                break;
            }
            uint16_t low = static_cast<uint16_t>(u[i]) |
                           (static_cast<uint16_t>(u[i + 1]) << 8);
            i += 2;
            if (low >= 0xDC00 && low <= 0xDFFF) {
                uint32_t cp = 0x10000 +
                    ((static_cast<uint32_t>(code_unit - 0xD800) << 10) |
                     static_cast<uint32_t>(low - 0xDC00));
                encode_utf8(cp, result);
            } else {
                encode_utf8(0xFFFD, result); // invalid low surrogate
            }
        } else if (code_unit >= 0xDC00 && code_unit <= 0xDFFF) {
            // Lone low surrogate
            encode_utf8(0xFFFD, result);
        } else {
            encode_utf8(static_cast<uint32_t>(code_unit), result);
        }
    }

    // Handle trailing odd byte
    if (i < len) {
        encode_utf8(0xFFFD, result);
    }

    return result;
}

/// Transcode UTF-16 BE to UTF-8.
[[nodiscard]] inline std::string utf16be_to_utf8(const char* data, size_t len) {
    std::string result;
    result.reserve(len);

    const auto* u = reinterpret_cast<const unsigned char*>(data);
    size_t i = 0;

    while (i + 1 < len) {
        uint16_t code_unit = (static_cast<uint16_t>(u[i]) << 8) |
                              static_cast<uint16_t>(u[i + 1]);
        i += 2;

        if (code_unit >= 0xD800 && code_unit <= 0xDBFF) {
            if (i + 1 >= len) {
                encode_utf8(0xFFFD, result);
                break;
            }
            uint16_t low = (static_cast<uint16_t>(u[i]) << 8) |
                            static_cast<uint16_t>(u[i + 1]);
            i += 2;
            if (low >= 0xDC00 && low <= 0xDFFF) {
                uint32_t cp = 0x10000 +
                    ((static_cast<uint32_t>(code_unit - 0xD800) << 10) |
                     static_cast<uint32_t>(low - 0xDC00));
                encode_utf8(cp, result);
            } else {
                encode_utf8(0xFFFD, result);
            }
        } else if (code_unit >= 0xDC00 && code_unit <= 0xDFFF) {
            encode_utf8(0xFFFD, result);
        } else {
            encode_utf8(static_cast<uint32_t>(code_unit), result);
        }
    }

    if (i < len) {
        encode_utf8(0xFFFD, result);
    }

    return result;
}

/// Transcode ISO-8859-1 (Latin-1) to UTF-8.
/// Each byte maps directly to its Unicode codepoint (U+0000 to U+00FF).
[[nodiscard]] inline std::string iso8859_1_to_utf8(const char* data, size_t len) {
    std::string result;
    result.reserve(len * 2); // worst case: each byte -> 2 UTF-8 bytes

    const auto* u = reinterpret_cast<const unsigned char*>(data);
    for (size_t i = 0; i < len; ++i) {
        encode_utf8(static_cast<uint32_t>(u[i]), result);
    }

    return result;
}

} // namespace detail

/// Transcode data from the given source encoding to UTF-8.
/// For UTF-8 and ASCII input, this simply copies the data (after skipping BOM).
/// Throws ParseError if the source encoding is unsupported (UTF-32).
[[nodiscard]] inline std::string transcode_to_utf8(const char* data, size_t len,
                                                    Encoding src_enc) {
    if (!data || len == 0) return {};

    switch (src_enc) {
        case Encoding::UTF8:
        case Encoding::ASCII: {
            // Skip BOM if present
            size_t bom = skip_bom(data, len);
            return std::string(data + bom, len - bom);
        }

        case Encoding::UTF16_LE: {
            size_t bom = skip_bom(data, len);
            return detail::utf16le_to_utf8(data + bom, len - bom);
        }

        case Encoding::UTF16_BE: {
            size_t bom = skip_bom(data, len);
            return detail::utf16be_to_utf8(data + bom, len - bom);
        }

        case Encoding::ISO_8859_1: {
            return detail::iso8859_1_to_utf8(data, len);
        }

        case Encoding::UTF32_LE:
        case Encoding::UTF32_BE:
            throw ParseError("UTF-32 transcoding is not supported", 0);

        case Encoding::Unknown:
            // Assume UTF-8 for unknown
            return std::string(data, len);
    }

    return std::string(data, len);
}

} // namespace parshred
