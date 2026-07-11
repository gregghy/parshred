#pragma once
/// @file error.hpp
/// @brief Structured error reporting system for the parshred XML parser.
///
/// Provides:
///   - ErrorSeverity  — Warning / Error / Fatal
///   - ErrorCode      — machine-readable error identifiers
///   - ParseDiagnostic — a single diagnostic record (called ParseError in docs,
///                       but renamed here to avoid collision with the exception
///                       class already declared in common.hpp)
///   - ErrorCollector — accumulates diagnostics; formats them for display
///   - LineTracker    — maps byte offsets to line/column during parsing
///   - extract_context() — pulls ~N chars of source around an offset

#include <parshred/common.hpp>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

// Pull in SIMD headers only on x86-64, used by LineTracker::advance(bulk).
#ifdef __x86_64__
#  include <immintrin.h>
#endif

namespace parshred {

// ── ErrorSeverity ─────────────────────────────────────────────────────────────

/// Indicates the gravity of a reported diagnostic.
enum class ErrorSeverity : uint8_t {
    Warning = 0, ///< Non-fatal issue; parsing continues normally.
    Error   = 1, ///< Recoverable error; parsing may continue with degraded output.
    Fatal   = 2, ///< Unrecoverable error; parsing must stop immediately.
};

/// Human-readable label for an ErrorSeverity value.
[[nodiscard]] constexpr const char* severity_name(ErrorSeverity s) noexcept {
    switch (s) {
        case ErrorSeverity::Warning: return "warning";
        case ErrorSeverity::Error:   return "error";
        case ErrorSeverity::Fatal:   return "fatal";
    }
    return "unknown";
}

// ── ErrorCode ─────────────────────────────────────────────────────────────────

/// Machine-readable error codes.
///
/// Codes are grouped loosely by category:
///   1xx – structural / tag problems
///   2xx – attribute problems
///   3xx – character / encoding problems
///   4xx – entity / reference problems
///   5xx – namespace problems
///   6xx – security / resource limits
enum class ErrorCode : uint16_t {
    // ── Structural ────────────────────────────────────────────────────
    MalformedTag         = 100, ///< Tag syntax is invalid (e.g. missing '>').
    UnexpectedEof        = 101, ///< Input ended while inside a construct.
    UnmatchedEndTag      = 102, ///< </foo> found with no matching <foo>.
    MissingEndTag        = 103, ///< <foo> was opened but never closed.
    NestedCommentMarker  = 104, ///< '--' appears inside a comment body.
    MalformedCData       = 105, ///< ']]>' appears in non-CDATA text content.
    MalformedPI          = 106, ///< Processing instruction is malformed.
    MalformedDocType     = 107, ///< DOCTYPE declaration has invalid syntax.
    MultipleRoots        = 108, ///< More than one root element in the document.

    // ── Attribute ─────────────────────────────────────────────────────
    DuplicateAttribute   = 200, ///< The same attribute name appears twice.
    MalformedAttribute   = 201, ///< Attribute syntax is invalid.
    MissingAttributeValue= 202, ///< Attribute name present but no '=' or value.

    // ── Character / encoding ──────────────────────────────────────────
    InvalidCharRef       = 300, ///< &#nnn; or &#xHH; is not a legal XML char.
    InvalidChar          = 301, ///< A byte / code point forbidden by the XML spec.
    InvalidEncoding      = 302, ///< Encoding declaration is unsupported or wrong.
    EncodingMismatch     = 303, ///< BOM or byte-order does not match declaration.

    // ── Entity / reference ────────────────────────────────────────────
    UndeclaredEntity     = 400, ///< &name; refers to an entity not defined in DTD.
    MalformedEntityRef   = 401, ///< '&' not followed by valid name or '#'.
    RecursiveEntity      = 402, ///< Entity definition is directly or indirectly recursive.

    // ── Namespace ─────────────────────────────────────────────────────
    UndeclaredNamespace  = 500, ///< Prefix used with no xmlns:prefix in scope.
    MalformedQName       = 501, ///< Qualified name has more than one colon.
    ReservedPrefix       = 502, ///< 'xml' or 'xmlns' used as a user prefix.

    // ── Security / resource limits ────────────────────────────────────
    MaxDepthExceeded         = 600, ///< Element nesting depth exceeds the configured limit.
    EntityExpansionLimit     = 601, ///< Entity expansion count exceeds the configured limit.
    MaxAttributeCountExceeded= 602, ///< Attribute count on one element exceeds the limit.
    InputTooLarge            = 603, ///< Input byte length exceeds the configured maximum.

    // ── Generic ───────────────────────────────────────────────────────
    Unknown              = 999, ///< Catch-all for errors without a specific code.
};

/// Human-readable short description for an ErrorCode.
[[nodiscard]] constexpr const char* error_code_name(ErrorCode c) noexcept {
    switch (c) {
        case ErrorCode::MalformedTag:             return "MalformedTag";
        case ErrorCode::UnexpectedEof:            return "UnexpectedEof";
        case ErrorCode::UnmatchedEndTag:          return "UnmatchedEndTag";
        case ErrorCode::MissingEndTag:            return "MissingEndTag";
        case ErrorCode::NestedCommentMarker:      return "NestedCommentMarker";
        case ErrorCode::MalformedCData:           return "MalformedCData";
        case ErrorCode::MalformedPI:              return "MalformedPI";
        case ErrorCode::MalformedDocType:         return "MalformedDocType";
        case ErrorCode::MultipleRoots:            return "MultipleRoots";
        case ErrorCode::DuplicateAttribute:       return "DuplicateAttribute";
        case ErrorCode::MalformedAttribute:       return "MalformedAttribute";
        case ErrorCode::MissingAttributeValue:    return "MissingAttributeValue";
        case ErrorCode::InvalidCharRef:           return "InvalidCharRef";
        case ErrorCode::InvalidChar:              return "InvalidChar";
        case ErrorCode::InvalidEncoding:          return "InvalidEncoding";
        case ErrorCode::EncodingMismatch:         return "EncodingMismatch";
        case ErrorCode::UndeclaredEntity:         return "UndeclaredEntity";
        case ErrorCode::MalformedEntityRef:       return "MalformedEntityRef";
        case ErrorCode::RecursiveEntity:          return "RecursiveEntity";
        case ErrorCode::UndeclaredNamespace:      return "UndeclaredNamespace";
        case ErrorCode::MalformedQName:           return "MalformedQName";
        case ErrorCode::ReservedPrefix:           return "ReservedPrefix";
        case ErrorCode::MaxDepthExceeded:         return "MaxDepthExceeded";
        case ErrorCode::EntityExpansionLimit:     return "EntityExpansionLimit";
        case ErrorCode::MaxAttributeCountExceeded:return "MaxAttributeCountExceeded";
        case ErrorCode::InputTooLarge:            return "InputTooLarge";
        case ErrorCode::Unknown:                  return "Unknown";
    }
    return "Unknown";
}

// ── ParseDiagnostic ───────────────────────────────────────────────────────────

/// A single structured diagnostic produced during parsing.
///
/// Note: named ParseDiagnostic rather than ParseError to avoid collision with
/// the exception class parshred::ParseError declared in common.hpp.
/// A type alias ParseErrorInfo = ParseDiagnostic is provided for convenience.
struct ParseDiagnostic {
    ErrorSeverity severity       = ErrorSeverity::Error;
    ErrorCode     code           = ErrorCode::Unknown;
    uint32_t      line           = 1;   ///< 1-based line number.
    uint32_t      column         = 1;   ///< 1-based column number (bytes, not code points).
    size_t        byte_offset    = 0;   ///< Byte offset from the start of input.
    std::string   message;              ///< Human-readable description.
    std::string   context_snippet;      ///< ~40 chars of source around the error site.
};

/// Convenience alias — use ParseErrorInfo when "ParseError" reads more naturally.
using ParseErrorInfo = ParseDiagnostic;

// ── extract_context ───────────────────────────────────────────────────────────

/// Extract up to `context_size` bytes of source text centred on `offset`.
///
/// The returned string contains the raw source bytes with no newlines replaced,
/// so the caller (e.g. format_error) can handle presentation.  If the window
/// would extend past the start or end of the input it is clamped.
///
/// @param data          Pointer to the full input buffer.
/// @param len           Length of the input buffer in bytes.
/// @param offset        The byte offset of the error site within the buffer.
/// @param context_size  Total number of source bytes to include (default 40).
/// @return              A std::string containing the extracted source snippet.
[[nodiscard]] inline std::string extract_context(
    const char* data,
    size_t      len,
    size_t      offset,
    size_t      context_size = 40) noexcept
{
    if (data == nullptr || len == 0) return {};

    // Clamp offset to valid range.
    if (offset > len) offset = len;

    // How many bytes to take before / after the error position.
    const size_t half   = context_size / 2;
    const size_t before = (offset >= half) ? half : offset;
    const size_t after  = (offset + (context_size - before) <= len)
                              ? (context_size - before)
                              : (len - offset);

    const size_t start = offset - before;
    const size_t end   = offset + after;       // exclusive

    std::string snippet;
    snippet.reserve(end - start);
    snippet.assign(data + start, end - start);
    return snippet;
}

// ── ErrorCollector ────────────────────────────────────────────────────────────

/// Accumulates ParseDiagnostic records produced during a parse pass.
///
/// Typical usage:
/// @code
///   ErrorCollector ec;
///   // ... inside parser ...
///   ec.add_error(ErrorSeverity::Error, ErrorCode::UnmatchedEndTag,
///                42, 18, 1234, "Expected </foo>, got </bar>",
///                extract_context(data, len, 1234));
///   if (ec.has_errors()) {
///       for (const auto& d : ec.errors())
///           std::cerr << ec.format_error(d) << '\n';
///   }
/// @endcode
class ErrorCollector {
public:
    ErrorCollector() = default;

    // Non-copyable (errors_ may be large), but movable.
    ErrorCollector(const ErrorCollector&)            = delete;
    ErrorCollector& operator=(const ErrorCollector&) = delete;
    ErrorCollector(ErrorCollector&&)                 = default;
    ErrorCollector& operator=(ErrorCollector&&)      = default;

    // ── Mutation ─────────────────────────────────────────────────────

    /// Record a diagnostic.
    ///
    /// @param severity  Severity level.
    /// @param code      Machine-readable error code.
    /// @param line      1-based line number.
    /// @param col       1-based column number.
    /// @param offset    Byte offset in the input.
    /// @param message   Human-readable description of the error.
    /// @param snippet   Optional source context (use extract_context()).
    void add_error(
        ErrorSeverity   severity,
        ErrorCode       code,
        uint32_t        line,
        uint32_t        col,
        size_t          offset,
        std::string     message,
        std::string     snippet = {})
    {
        errors_.push_back(ParseDiagnostic{
            severity,
            code,
            line,
            col,
            offset,
            std::move(message),
            std::move(snippet),
        });
    }

    /// Remove all recorded diagnostics.
    void clear() noexcept { errors_.clear(); }

    // ── Query ────────────────────────────────────────────────────────

    /// True if any diagnostic with severity >= Error is present.
    [[nodiscard]] bool has_errors() const noexcept {
        for (const auto& d : errors_) {
            if (d.severity >= ErrorSeverity::Error) return true;
        }
        return false;
    }

    /// True if any Fatal diagnostic is present.
    [[nodiscard]] bool has_fatal() const noexcept {
        for (const auto& d : errors_) {
            if (d.severity == ErrorSeverity::Fatal) return true;
        }
        return false;
    }

    /// Total number of recorded diagnostics (all severities).
    [[nodiscard]] size_t error_count() const noexcept { return errors_.size(); }

    /// Direct access to the recorded diagnostics.
    [[nodiscard]] const std::vector<ParseDiagnostic>& errors() const noexcept {
        return errors_;
    }

    // ── Formatting ───────────────────────────────────────────────────

    /// Format a single diagnostic as a human-readable string.
    ///
    /// Output format (mirrors clang/GCC style):
    /// @code
    ///   error: line 42, col 18: Expected '>', got EOF
    ///       <unclosed elem...
    ///                        ^
    /// @endcode
    ///
    /// If the diagnostic has no context_snippet the caret line is omitted.
    [[nodiscard]] static std::string format_error(const ParseDiagnostic& d) {
        std::string out;
        out.reserve(128);

        // Header: "severity: line L, col C: message"
        out += severity_name(d.severity);
        out += ": line ";
        out += std::to_string(d.line);
        out += ", col ";
        out += std::to_string(d.column);
        out += ": ";
        out += d.message;
        out += '\n';

        if (d.context_snippet.empty()) return out;

        // Source line — replace embedded newlines with a space so the snippet
        // stays on one visual line, then indent by four spaces.
        out += "    ";
        for (char c : d.context_snippet) {
            out += (c == '\n' || c == '\r') ? ' ' : c;
        }
        out += '\n';

        // Caret: points to column within the snippet.
        // The snippet is centred on the error; the caret sits at the midpoint
        // (i.e. context_size/2 characters from the left), clamped to the
        // start of the snippet when the error is near the beginning of input.
        // We use byte_offset to compute the actual position inside the snippet.
        // Since extract_context places the error at position `before` in the
        // snippet (before = min(half, offset)), the caret offset equals
        // min(context_size/2, offset).  We default context_size to 40, so
        // half = 20.  To be robust we infer the caret position from the
        // column field when the full input is unavailable.
        //
        // Simple heuristic that works without the raw input: place the caret
        // at snippet position = min(snippet.size()/2, snippet.size()-1).
        const size_t snippet_len   = d.context_snippet.size();
        const size_t caret_in_snip = (snippet_len > 1)
                                         ? std::min(snippet_len / 2, snippet_len - 1)
                                         : 0;

        // four spaces indent + caret_in_snip spaces + '^'
        out.append(4 + caret_in_snip, ' ');
        out += '^';
        out += '\n';

        return out;
    }

private:
    std::vector<ParseDiagnostic> errors_;
};

// ── LineTracker ───────────────────────────────────────────────────────────────

/// Tracks the current line, column, and byte offset as the parser advances
/// through the input.
///
/// Line and column are 1-based (first character is line 1, column 1).
/// A bare '\n', a bare '\r', or the two-byte sequence '\r\n' all count as a
/// single line terminator and advance the line counter by 1.
///
/// Thread safety: none — use one LineTracker per parsing context.
class LineTracker {
public:
    /// Construct a tracker positioned at the very start of the input.
    LineTracker() noexcept = default;

    // ── Advance (single character) ───────────────────────────────────

    /// Feed a single character and update line/column/offset.
    ///
    /// '\r\n' sequences: the '\r' increments the line and sets a flag so the
    /// following '\n' is treated as the continuation of the same line break
    /// (i.e. it does not bump the line counter again).
    void advance(char c) noexcept {
        ++offset_;
        if (c == '\n') {
            if (!last_was_cr_) {
                ++line_;
                col_ = 1;
            } else {
                // '\r\n' pair — '\r' already incremented line; '\n' is just
                // the second byte of the same line terminator.
                col_ = 1;
            }
            last_was_cr_ = false;
        } else if (c == '\r') {
            ++line_;
            col_ = 1;
            last_was_cr_ = true;
        } else {
            ++col_;
            last_was_cr_ = false;
        }
    }

    // ── Advance (bulk) ───────────────────────────────────────────────

    /// Feed a block of characters efficiently.
    ///
    /// Uses a SIMD-friendly approach on x86-64: count '\n' bytes in 16-byte
    /// chunks with SSE4.2 pcmpeqb, falling back to a scalar loop for the tail
    /// and for non-x86 targets.  '\r' and '\r\n' handling is kept correct by
    /// the scalar pass for any chunk containing a '\r'.
    void advance(const char* data, size_t len) noexcept {
        if (data == nullptr || len == 0) return;

        size_t pos = 0;

#if defined(__SSE4_2__) || defined(__SSE2__)
        // Fast path: count newlines in 16-byte lanes.
        // We only use this path when the chunk has no '\r' at all (the common
        // case for Unix-line-ending files).  If a '\r' is detected, fall
        // through to the scalar loop for the entire remainder so we never
        // miscount '\r\n' pairs.
        //
        // We detect the presence of '\r' in the same SIMD pass by checking
        // a second comparator, and abort to the scalar path when any '\r'
        // is found in a chunk.
#ifdef __SSE2__
        const __m128i v_nl = _mm_set1_epi8('\n');
        const __m128i v_cr = _mm_set1_epi8('\r');

        while (pos + 16 <= len) {
            __m128i chunk = _mm_loadu_si128(
                reinterpret_cast<const __m128i*>(data + pos));

            // Check for any '\r' in this 16-byte window.
            int cr_mask = _mm_movemask_epi8(_mm_cmpeq_epi8(chunk, v_cr));
            if (cr_mask != 0) {
                // Fall back to scalar for remainder (includes this chunk).
                break;
            }

            // Count '\n' bytes (each set bit in the movemask is one '\n').
            int nl_mask = _mm_movemask_epi8(_mm_cmpeq_epi8(chunk, v_nl));
            int nl_count = __builtin_popcount(static_cast<unsigned>(nl_mask));

            if (nl_count > 0) {
                // One or more newlines in this chunk.  We need to update col_
                // correctly: after the last '\n', col_ restarts at 1 plus
                // however many non-newline bytes follow it in the chunk.
                //
                // Find position of the last '\n' in the chunk.
                int last_nl_bit = 31 - __builtin_clz(static_cast<unsigned>(nl_mask));
                size_t trailing  = 15 - static_cast<size_t>(last_nl_bit); // bytes after last '\n'

                line_ += static_cast<uint32_t>(nl_count);
                col_   = static_cast<uint32_t>(1 + trailing);
            } else {
                col_ += 16;
            }

            offset_ += 16;
            pos     += 16;
            last_was_cr_ = false;
        }
#endif // __SSE2__
#endif // SSE4_2 || SSE2

        // Scalar tail — also handles '\r', '\r\n', and any leftover bytes.
        for (; pos < len; ++pos) {
            advance(data[pos]);
        }
    }

    // ── Getters ──────────────────────────────────────────────────────

    /// Current 1-based line number.
    [[nodiscard]] uint32_t line()   const noexcept { return line_; }

    /// Current 1-based column number (counts bytes, not Unicode code points).
    [[nodiscard]] uint32_t column() const noexcept { return col_; }

    /// Byte offset of the *next* character to be consumed (i.e. the number
    /// of bytes already fed to advance()).
    [[nodiscard]] size_t   offset() const noexcept { return offset_; }

    // ── Reset ────────────────────────────────────────────────────────

    /// Reset to the initial state (line 1, col 1, offset 0).
    void reset() noexcept {
        line_        = 1;
        col_         = 1;
        offset_      = 0;
        last_was_cr_ = false;
    }

private:
    uint32_t line_        = 1;
    uint32_t col_         = 1;
    size_t   offset_      = 0;
    bool     last_was_cr_ = false;
};

} // namespace parshred
