/// @file test_error.cpp
/// @brief Unit tests for the structured error reporting system (error.hpp).
///
/// Coverage:
///   - ErrorCollector: add, query, clear
///   - LineTracker: single-char advance, bulk advance, mixed line endings
///   - ErrorCollector::format_error output shape
///   - extract_context at start / middle / end / near boundaries

#include <parshred/error.hpp>
#include <gtest/gtest.h>

#include <string>
#include <string_view>

using namespace parshred;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Advance a LineTracker one character at a time through a string.
static void advance_string(LineTracker& lt, std::string_view s) {
    for (char c : s) lt.advance(c);
}

// ─────────────────────────────────────────────────────────────────────────────
// ErrorCollector — add / query / clear
// ─────────────────────────────────────────────────────────────────────────────

TEST(ErrorCollector, StartsEmpty) {
    ErrorCollector ec;
    EXPECT_EQ(ec.error_count(), 0u);
    EXPECT_FALSE(ec.has_errors());
    EXPECT_FALSE(ec.has_fatal());
    EXPECT_TRUE(ec.errors().empty());
}

TEST(ErrorCollector, AddWarningDoesNotSetHasErrors) {
    ErrorCollector ec;
    ec.add_error(ErrorSeverity::Warning, ErrorCode::Unknown,
                 1, 1, 0, "just a warning");

    EXPECT_EQ(ec.error_count(), 1u);
    EXPECT_FALSE(ec.has_errors());   // Warning < Error
    EXPECT_FALSE(ec.has_fatal());
}

TEST(ErrorCollector, AddErrorSetsHasErrors) {
    ErrorCollector ec;
    ec.add_error(ErrorSeverity::Error, ErrorCode::MalformedTag,
                 5, 10, 42, "bad tag");

    EXPECT_EQ(ec.error_count(), 1u);
    EXPECT_TRUE(ec.has_errors());
    EXPECT_FALSE(ec.has_fatal());
}

TEST(ErrorCollector, AddFatalSetsBothFlags) {
    ErrorCollector ec;
    ec.add_error(ErrorSeverity::Fatal, ErrorCode::UnexpectedEof,
                 10, 1, 999, "unexpected EOF");

    EXPECT_EQ(ec.error_count(), 1u);
    EXPECT_TRUE(ec.has_errors());
    EXPECT_TRUE(ec.has_fatal());
}

TEST(ErrorCollector, MultipleErrors) {
    ErrorCollector ec;
    ec.add_error(ErrorSeverity::Warning,  ErrorCode::DuplicateAttribute, 1, 5, 4,  "dup attr");
    ec.add_error(ErrorSeverity::Error,    ErrorCode::UnmatchedEndTag,    3, 1, 80, "bad end tag");
    ec.add_error(ErrorSeverity::Fatal,    ErrorCode::UnexpectedEof,      7, 1, 200,"eof");

    EXPECT_EQ(ec.error_count(), 3u);
    EXPECT_TRUE(ec.has_errors());
    EXPECT_TRUE(ec.has_fatal());
}

TEST(ErrorCollector, ErrorsReturnedInOrder) {
    ErrorCollector ec;
    ec.add_error(ErrorSeverity::Warning, ErrorCode::Unknown,       1, 1, 0,  "first");
    ec.add_error(ErrorSeverity::Error,   ErrorCode::MalformedTag,  2, 1, 10, "second");
    ec.add_error(ErrorSeverity::Fatal,   ErrorCode::UnexpectedEof, 3, 1, 20, "third");

    const auto& errs = ec.errors();
    ASSERT_EQ(errs.size(), 3u);
    EXPECT_EQ(errs[0].message, "first");
    EXPECT_EQ(errs[1].message, "second");
    EXPECT_EQ(errs[2].message, "third");
}

TEST(ErrorCollector, DiagnosticFieldsStored) {
    ErrorCollector ec;
    ec.add_error(ErrorSeverity::Error, ErrorCode::InvalidCharRef,
                 42, 18, 1234, "bad char ref", "<snippet>");

    const auto& d = ec.errors()[0];
    EXPECT_EQ(d.severity,        ErrorSeverity::Error);
    EXPECT_EQ(d.code,            ErrorCode::InvalidCharRef);
    EXPECT_EQ(d.line,            42u);
    EXPECT_EQ(d.column,          18u);
    EXPECT_EQ(d.byte_offset,     1234u);
    EXPECT_EQ(d.message,         "bad char ref");
    EXPECT_EQ(d.context_snippet, "<snippet>");
}

TEST(ErrorCollector, Clear) {
    ErrorCollector ec;
    ec.add_error(ErrorSeverity::Error, ErrorCode::MalformedTag, 1, 1, 0, "x");
    ec.add_error(ErrorSeverity::Fatal, ErrorCode::UnexpectedEof, 2, 1, 5, "y");
    ASSERT_EQ(ec.error_count(), 2u);

    ec.clear();

    EXPECT_EQ(ec.error_count(), 0u);
    EXPECT_FALSE(ec.has_errors());
    EXPECT_FALSE(ec.has_fatal());
}

TEST(ErrorCollector, ClearThenAddAgain) {
    ErrorCollector ec;
    ec.add_error(ErrorSeverity::Fatal, ErrorCode::UnexpectedEof, 1, 1, 0, "first session");
    ec.clear();
    ec.add_error(ErrorSeverity::Warning, ErrorCode::Unknown, 5, 3, 77, "second session");

    EXPECT_EQ(ec.error_count(), 1u);
    EXPECT_EQ(ec.errors()[0].message, "second session");
}

TEST(ErrorCollector, OnlyWarnings_HasErrorsFalse) {
    ErrorCollector ec;
    for (int i = 0; i < 5; ++i) {
        ec.add_error(ErrorSeverity::Warning, ErrorCode::Unknown,
                     static_cast<uint32_t>(i + 1), 1, static_cast<size_t>(i * 10), "warn");
    }
    EXPECT_EQ(ec.error_count(), 5u);
    EXPECT_FALSE(ec.has_errors());
    EXPECT_FALSE(ec.has_fatal());
}

// ─────────────────────────────────────────────────────────────────────────────
// format_error
// ─────────────────────────────────────────────────────────────────────────────

TEST(FormatError, HeaderLinePresent) {
    ParseDiagnostic d;
    d.severity     = ErrorSeverity::Error;
    d.code         = ErrorCode::MalformedTag;
    d.line         = 42;
    d.column       = 18;
    d.byte_offset  = 100;
    d.message      = "Expected '>', got EOF";

    std::string out = ErrorCollector::format_error(d);

    // Must contain the word "error"
    EXPECT_NE(out.find("error"), std::string::npos);
    // Must contain the line number
    EXPECT_NE(out.find("42"), std::string::npos);
    // Must contain the column number
    EXPECT_NE(out.find("18"), std::string::npos);
    // Must contain the message
    EXPECT_NE(out.find("Expected '>', got EOF"), std::string::npos);
}

TEST(FormatError, WarningPrefix) {
    ParseDiagnostic d;
    d.severity = ErrorSeverity::Warning;
    d.code     = ErrorCode::DuplicateAttribute;
    d.line     = 1;
    d.column   = 1;
    d.message  = "duplicate attribute 'id'";

    std::string out = ErrorCollector::format_error(d);
    EXPECT_EQ(out.substr(0, 7), "warning");
}

TEST(FormatError, FatalPrefix) {
    ParseDiagnostic d;
    d.severity = ErrorSeverity::Fatal;
    d.code     = ErrorCode::UnexpectedEof;
    d.line     = 3;
    d.column   = 5;
    d.message  = "unexpected end of file";

    std::string out = ErrorCollector::format_error(d);
    EXPECT_EQ(out.substr(0, 5), "fatal");
}

TEST(FormatError, NoSnippetNoCaretLine) {
    ParseDiagnostic d;
    d.severity = ErrorSeverity::Error;
    d.code     = ErrorCode::Unknown;
    d.line     = 1;
    d.column   = 1;
    d.message  = "something went wrong";
    // context_snippet intentionally left empty

    std::string out = ErrorCollector::format_error(d);
    // Should contain exactly one newline (end of header line), no caret.
    EXPECT_EQ(std::count(out.begin(), out.end(), '\n'), 1);
    EXPECT_EQ(out.find('^'), std::string::npos);
}

TEST(FormatError, SnippetAndCaretPresent) {
    ParseDiagnostic d;
    d.severity        = ErrorSeverity::Error;
    d.code            = ErrorCode::MalformedTag;
    d.line            = 5;
    d.column          = 12;
    d.byte_offset     = 200;
    d.message         = "malformed tag";
    d.context_snippet = "<unclosed elem...";

    std::string out = ErrorCollector::format_error(d);
    EXPECT_NE(out.find("<unclosed elem..."), std::string::npos);
    EXPECT_NE(out.find('^'), std::string::npos);
    // Three lines: header, snippet, caret
    EXPECT_EQ(std::count(out.begin(), out.end(), '\n'), 3);
}

TEST(FormatError, SnippetNewlinesReplacedWithSpace) {
    ParseDiagnostic d;
    d.severity        = ErrorSeverity::Error;
    d.code            = ErrorCode::MalformedTag;
    d.line            = 2;
    d.column          = 1;
    d.message         = "error";
    d.context_snippet = "line1\nline2\nline3";

    std::string out = ErrorCollector::format_error(d);
    // The snippet line should not contain embedded '\n' (they become spaces).
    // Split by '\n', check that the snippet line has no bare newlines in it.
    // The snippet itself starts after "    " indent on the second line.
    size_t first_nl  = out.find('\n');
    size_t second_nl = out.find('\n', first_nl + 1);
    std::string snippet_line = out.substr(first_nl + 1, second_nl - first_nl - 1);
    EXPECT_EQ(snippet_line.find('\n'), std::string::npos);
    EXPECT_NE(snippet_line.find("line1"), std::string::npos);
}

// ─────────────────────────────────────────────────────────────────────────────
// extract_context
// ─────────────────────────────────────────────────────────────────────────────

TEST(ExtractContext, NullOrEmptyInput) {
    EXPECT_EQ(extract_context(nullptr, 0, 0, 40), "");
    std::string s = "hello";
    EXPECT_EQ(extract_context(s.data(), 0, 0, 40), "");
}

TEST(ExtractContext, OffsetAtMiddle) {
    // "0123456789" — 10 chars; offset 5 with context 4 gives chars 3..6 (clamped).
    std::string s = "0123456789";
    std::string ctx = extract_context(s.data(), s.size(), 5, 4);
    // before = min(2, 5) = 2 → start=3; after = min(2, 5) = 2 → end=7
    EXPECT_EQ(ctx, "3456");
}

TEST(ExtractContext, OffsetAtStart) {
    std::string s = "abcdefghij";
    // Offset 0: no bytes before it; take 40 chars after (but only 10 available).
    std::string ctx = extract_context(s.data(), s.size(), 0, 40);
    EXPECT_EQ(ctx, s);   // whole string
}

TEST(ExtractContext, OffsetAtEnd) {
    std::string s = "abcdefghij";  // len=10
    // Offset 10 (one past end): clamped, take up to 40 bytes before.
    std::string ctx = extract_context(s.data(), s.size(), 10, 40);
    EXPECT_EQ(ctx, s);   // whole string
}

TEST(ExtractContext, OffsetNearStart) {
    std::string s = "abcdefghij";  // len=10
    // Offset 2, context 6: before=min(3,2)=2, after=min(4,8)=4 → start=0, end=6
    std::string ctx = extract_context(s.data(), s.size(), 2, 6);
    EXPECT_EQ(ctx, "abcdef");
}

TEST(ExtractContext, OffsetNearEnd) {
    std::string s = "abcdefghij";  // len=10
    // Offset 8, context 6: before=min(3,8)=3, after=min(3,2)=2 → start=5, end=10
    std::string ctx = extract_context(s.data(), s.size(), 8, 6);
    EXPECT_EQ(ctx, "fghij");
}

TEST(ExtractContext, Default40Chars) {
    // Build 80-char string; offset at the exact middle (40).
    std::string s(80, 'x');
    for (size_t i = 0; i < 80; ++i) s[i] = static_cast<char>('a' + (i % 26));
    std::string ctx = extract_context(s.data(), s.size(), 40);
    // before=min(20,40)=20, after=min(20,40)=20 → 40 chars
    EXPECT_EQ(ctx.size(), 40u);
    EXPECT_EQ(ctx, s.substr(20, 40));
}

TEST(ExtractContext, ShorterThanWindow) {
    // Input is only 5 chars; context 40 → whole string.
    std::string s = "hello";
    std::string ctx = extract_context(s.data(), s.size(), 2, 40);
    EXPECT_EQ(ctx, s);
}

TEST(ExtractContext, ExactBoundary) {
    // Offset exactly equals len.
    std::string s = "abc";
    std::string ctx = extract_context(s.data(), s.size(), 3, 4);
    // offset clamped to 3, before=min(2,3)=2, after=min(2,0)=0 → start=1, end=3
    EXPECT_EQ(ctx, "bc");
}

// ─────────────────────────────────────────────────────────────────────────────
// LineTracker — single-char advance
// ─────────────────────────────────────────────────────────────────────────────

TEST(LineTracker, InitialState) {
    LineTracker lt;
    EXPECT_EQ(lt.line(),   1u);
    EXPECT_EQ(lt.column(), 1u);
    EXPECT_EQ(lt.offset(), 0u);
}

TEST(LineTracker, AdvanceSingleChars) {
    LineTracker lt;
    lt.advance('a');
    EXPECT_EQ(lt.line(),   1u);
    EXPECT_EQ(lt.column(), 2u);
    EXPECT_EQ(lt.offset(), 1u);

    lt.advance('b');
    EXPECT_EQ(lt.line(),   1u);
    EXPECT_EQ(lt.column(), 3u);
    EXPECT_EQ(lt.offset(), 2u);
}

TEST(LineTracker, UnixNewline) {
    LineTracker lt;
    lt.advance('a');
    lt.advance('\n');
    EXPECT_EQ(lt.line(),   2u);
    EXPECT_EQ(lt.column(), 1u);
    EXPECT_EQ(lt.offset(), 2u);

    lt.advance('b');
    EXPECT_EQ(lt.line(),   2u);
    EXPECT_EQ(lt.column(), 2u);
}

TEST(LineTracker, CarriageReturn) {
    LineTracker lt;
    lt.advance('a');
    lt.advance('\r');
    EXPECT_EQ(lt.line(),   2u);
    EXPECT_EQ(lt.column(), 1u);
    EXPECT_EQ(lt.offset(), 2u);

    lt.advance('b');
    EXPECT_EQ(lt.line(),   2u);
    EXPECT_EQ(lt.column(), 2u);
}

TEST(LineTracker, CrLfPairCountsAsOneLine) {
    LineTracker lt;
    lt.advance('a');
    lt.advance('\r');
    EXPECT_EQ(lt.line(), 2u);
    lt.advance('\n');
    // '\n' after '\r' must NOT bump the line again.
    EXPECT_EQ(lt.line(),   2u);
    EXPECT_EQ(lt.column(), 1u);
    EXPECT_EQ(lt.offset(), 3u);

    lt.advance('b');
    EXPECT_EQ(lt.line(),   2u);
    EXPECT_EQ(lt.column(), 2u);
}

TEST(LineTracker, MultipleCrLf) {
    LineTracker lt;
    // Feed three '\r\n' pairs.
    for (int i = 0; i < 3; ++i) {
        lt.advance('\r');
        lt.advance('\n');
    }
    EXPECT_EQ(lt.line(),   4u);   // started at 1, three line breaks → line 4
    EXPECT_EQ(lt.column(), 1u);
    EXPECT_EQ(lt.offset(), 6u);
}

TEST(LineTracker, MultipleUnixNewlines) {
    LineTracker lt;
    advance_string(lt, "line1\nline2\nline3\n");
    EXPECT_EQ(lt.line(),   4u);
    EXPECT_EQ(lt.column(), 1u);
    EXPECT_EQ(lt.offset(), 18u);
}

TEST(LineTracker, MixedNewlines) {
    // '\n', '\r', '\r\n' each count as one line terminator.
    LineTracker lt;
    lt.advance('\n');   // line 2
    lt.advance('\r');   // line 3
    lt.advance('\n');   // NOT a new line (continues the \r above)
    lt.advance('\r');   // line 4
    lt.advance('\n');   // NOT a new line
    lt.advance('\n');   // line 5
    EXPECT_EQ(lt.line(), 5u);
}

TEST(LineTracker, ColumnResetAfterNewline) {
    LineTracker lt;
    advance_string(lt, "hello\nworld");
    EXPECT_EQ(lt.line(),   2u);
    EXPECT_EQ(lt.column(), 6u);   // 'w','o','r','l','d' → col 6 after advancing 5 chars
    EXPECT_EQ(lt.offset(), 11u);
}

TEST(LineTracker, OffsetTracking) {
    LineTracker lt;
    std::string s = "ab\ncd\nef";
    advance_string(lt, s);
    EXPECT_EQ(lt.offset(), s.size());
}

// ─────────────────────────────────────────────────────────────────────────────
// LineTracker — bulk advance
// ─────────────────────────────────────────────────────────────────────────────

TEST(LineTracker, BulkAdvanceMatchesSingleChar) {
    const std::string text =
        "Hello, world!\n"
        "Second line here\n"
        "And a third\n"
        "No newline at end";

    // Reference: single-char advance.
    LineTracker lt_ref;
    advance_string(lt_ref, text);

    // Bulk advance.
    LineTracker lt_bulk;
    lt_bulk.advance(text.data(), text.size());

    EXPECT_EQ(lt_bulk.line(),   lt_ref.line());
    EXPECT_EQ(lt_bulk.column(), lt_ref.column());
    EXPECT_EQ(lt_bulk.offset(), lt_ref.offset());
}

TEST(LineTracker, BulkAdvanceCrLfMatchesSingleChar) {
    const std::string text = "line1\r\nline2\r\nline3\r\n";

    LineTracker lt_ref;
    advance_string(lt_ref, text);

    LineTracker lt_bulk;
    lt_bulk.advance(text.data(), text.size());

    EXPECT_EQ(lt_bulk.line(),   lt_ref.line());
    EXPECT_EQ(lt_bulk.column(), lt_ref.column());
    EXPECT_EQ(lt_bulk.offset(), lt_ref.offset());
}

TEST(LineTracker, BulkAdvanceBareCarriageReturn) {
    const std::string text = "a\rb\rc\r";

    LineTracker lt_ref;
    advance_string(lt_ref, text);

    LineTracker lt_bulk;
    lt_bulk.advance(text.data(), text.size());

    EXPECT_EQ(lt_bulk.line(),   lt_ref.line());
    EXPECT_EQ(lt_bulk.column(), lt_ref.column());
    EXPECT_EQ(lt_bulk.offset(), lt_ref.offset());
}

TEST(LineTracker, BulkAdvanceEmptyString) {
    LineTracker lt;
    lt.advance("", 0);
    EXPECT_EQ(lt.line(),   1u);
    EXPECT_EQ(lt.column(), 1u);
    EXPECT_EQ(lt.offset(), 0u);
}

TEST(LineTracker, BulkAdvanceNullptr) {
    LineTracker lt;
    lt.advance(nullptr, 0);
    EXPECT_EQ(lt.line(),   1u);
    EXPECT_EQ(lt.column(), 1u);
    EXPECT_EQ(lt.offset(), 0u);
}

TEST(LineTracker, BulkAdvanceLargeInput) {
    // 1000 lines of 79 chars + '\n'
    std::string text;
    text.reserve(1000 * 80);
    for (int i = 0; i < 1000; ++i) {
        text.append(79, 'x');
        text += '\n';
    }

    LineTracker lt_ref;
    advance_string(lt_ref, text);

    LineTracker lt_bulk;
    lt_bulk.advance(text.data(), text.size());

    EXPECT_EQ(lt_bulk.line(),   lt_ref.line());
    EXPECT_EQ(lt_bulk.column(), lt_ref.column());
    EXPECT_EQ(lt_bulk.offset(), lt_ref.offset());
}

// ─────────────────────────────────────────────────────────────────────────────
// LineTracker — reset
// ─────────────────────────────────────────────────────────────────────────────

TEST(LineTracker, Reset) {
    LineTracker lt;
    advance_string(lt, "some\ntext\nhere");
    ASSERT_GT(lt.line(), 1u);

    lt.reset();
    EXPECT_EQ(lt.line(),   1u);
    EXPECT_EQ(lt.column(), 1u);
    EXPECT_EQ(lt.offset(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// ErrorCode / ErrorSeverity names
// ─────────────────────────────────────────────────────────────────────────────

TEST(EnumNames, SeverityNames) {
    EXPECT_EQ(std::string_view(severity_name(ErrorSeverity::Warning)), "warning");
    EXPECT_EQ(std::string_view(severity_name(ErrorSeverity::Error)),   "error");
    EXPECT_EQ(std::string_view(severity_name(ErrorSeverity::Fatal)),   "fatal");
}

TEST(EnumNames, ErrorCodeNames) {
    EXPECT_EQ(std::string_view(error_code_name(ErrorCode::MalformedTag)),    "MalformedTag");
    EXPECT_EQ(std::string_view(error_code_name(ErrorCode::UnexpectedEof)),   "UnexpectedEof");
    EXPECT_EQ(std::string_view(error_code_name(ErrorCode::UnmatchedEndTag)), "UnmatchedEndTag");
    EXPECT_EQ(std::string_view(error_code_name(ErrorCode::DuplicateAttribute)), "DuplicateAttribute");
    EXPECT_EQ(std::string_view(error_code_name(ErrorCode::InvalidCharRef)),  "InvalidCharRef");
    EXPECT_EQ(std::string_view(error_code_name(ErrorCode::UndeclaredEntity)),"UndeclaredEntity");
    EXPECT_EQ(std::string_view(error_code_name(ErrorCode::UndeclaredNamespace)), "UndeclaredNamespace");
    EXPECT_EQ(std::string_view(error_code_name(ErrorCode::InvalidEncoding)), "InvalidEncoding");
    EXPECT_EQ(std::string_view(error_code_name(ErrorCode::MaxDepthExceeded)),"MaxDepthExceeded");
    EXPECT_EQ(std::string_view(error_code_name(ErrorCode::EntityExpansionLimit)), "EntityExpansionLimit");
}

// ─────────────────────────────────────────────────────────────────────────────
// ParseErrorInfo alias
// ─────────────────────────────────────────────────────────────────────────────

TEST(ParseErrorInfo, AliasWorks) {
    // ParseErrorInfo is a typedef for ParseDiagnostic; the two must be the
    // same type.
    static_assert(std::is_same_v<ParseErrorInfo, ParseDiagnostic>,
                  "ParseErrorInfo must be an alias for ParseDiagnostic");

    ParseErrorInfo info;
    info.severity    = ErrorSeverity::Error;
    info.code        = ErrorCode::MalformedTag;
    info.line        = 7;
    info.column      = 3;
    info.byte_offset = 42;
    info.message     = "test";

    ErrorCollector ec;
    ec.add_error(info.severity, info.code,
                 info.line, info.column, info.byte_offset,
                 info.message, info.context_snippet);

    EXPECT_EQ(ec.error_count(), 1u);
    EXPECT_EQ(ec.errors()[0].line, 7u);
}
