/// @file test_encoding.cpp
/// @brief Unit tests for encoding detection, line normalization, and transcoding.

#include <parshred/encoding.hpp>
#include <gtest/gtest.h>
#include <string>
#include <cstring>

using namespace parshred;

// ── BOM Detection ────────────────────────────────────────────────────

TEST(Encoding, DetectBomUtf8) {
    const char data[] = "\xEF\xBB\xBF<?xml version=\"1.0\"?>";
    EXPECT_EQ(detect_encoding(data, sizeof(data) - 1), Encoding::UTF8);
}

TEST(Encoding, DetectBomUtf16LE) {
    const char data[] = "\xFF\xFE<\x00";
    EXPECT_EQ(detect_encoding(data, sizeof(data) - 1), Encoding::UTF16_LE);
}

TEST(Encoding, DetectBomUtf16BE) {
    const char data[] = "\xFE\xFF\x00<";
    EXPECT_EQ(detect_encoding(data, sizeof(data) - 1), Encoding::UTF16_BE);
}

TEST(Encoding, DetectBomUtf32LE) {
    const char data[] = "\xFF\xFE\x00\x00<\x00\x00\x00";
    EXPECT_EQ(detect_encoding(data, sizeof(data) - 1), Encoding::UTF32_LE);
}

TEST(Encoding, DetectBomUtf32BE) {
    const char data[] = "\x00\x00\xFE\xFF\x00\x00\x00<";
    EXPECT_EQ(detect_encoding(data, sizeof(data) - 1), Encoding::UTF32_BE);
}

TEST(Encoding, NoBomDefaultsToUtf8) {
    const char data[] = "<?xml version=\"1.0\"?><root/>";
    EXPECT_EQ(detect_encoding(data, sizeof(data) - 1), Encoding::UTF8);
}

TEST(Encoding, XmlDeclarationOverrideEncoding) {
    const char data[] = "<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?><root/>";
    EXPECT_EQ(detect_encoding(data, sizeof(data) - 1), Encoding::ISO_8859_1);
}

TEST(Encoding, XmlDeclarationUtf16) {
    const char data[] = "<?xml version=\"1.0\" encoding=\"UTF-16BE\"?>";
    EXPECT_EQ(detect_encoding(data, sizeof(data) - 1), Encoding::UTF16_BE);
}

TEST(Encoding, XmlDeclarationSingleQuotes) {
    const char data[] = "<?xml version='1.0' encoding='us-ascii'?><root/>";
    EXPECT_EQ(detect_encoding(data, sizeof(data) - 1), Encoding::ASCII);
}

// ── skip_bom ─────────────────────────────────────────────────────────

TEST(SkipBom, Utf8Bom) {
    const char data[] = "\xEF\xBB\xBF<root/>";
    EXPECT_EQ(skip_bom(data, sizeof(data) - 1), 3u);
}

TEST(SkipBom, Utf16LEBom) {
    const char data[] = "\xFF\xFE<\x00";
    EXPECT_EQ(skip_bom(data, sizeof(data) - 1), 2u);
}

TEST(SkipBom, Utf16BEBom) {
    const char data[] = "\xFE\xFF\x00<";
    EXPECT_EQ(skip_bom(data, sizeof(data) - 1), 2u);
}

TEST(SkipBom, Utf32LEBom) {
    const char data[] = "\xFF\xFE\x00\x00rest";
    EXPECT_EQ(skip_bom(data, sizeof(data) - 1), 4u);
}

TEST(SkipBom, Utf32BEBom) {
    const char data[] = "\x00\x00\xFE\xFFrest";
    EXPECT_EQ(skip_bom(data, sizeof(data) - 1), 4u);
}

TEST(SkipBom, NoBom) {
    const char data[] = "<root/>";
    EXPECT_EQ(skip_bom(data, sizeof(data) - 1), 0u);
}

TEST(SkipBom, EmptyInput) {
    EXPECT_EQ(skip_bom(nullptr, 0), 0u);
    EXPECT_EQ(skip_bom("", 0), 0u);
}

TEST(SkipBom, TruncatedBom) {
    // Only first byte of a potential UTF-8 BOM
    const char data[] = "\xEF";
    EXPECT_EQ(skip_bom(data, 1), 0u);

    // Two bytes of UTF-8 BOM (incomplete)
    const char data2[] = "\xEF\xBB";
    EXPECT_EQ(skip_bom(data2, 2), 0u);
}

// ── Line Ending Normalization ────────────────────────────────────────

TEST(LineEndings, CrLfToLf) {
    std::string data = "hello\r\nworld\r\n";
    normalize_line_endings(data);
    EXPECT_EQ(data, "hello\nworld\n");
}

TEST(LineEndings, StandaloneCrToLf) {
    std::string data = "hello\rworld\r";
    normalize_line_endings(data);
    EXPECT_EQ(data, "hello\nworld\n");
}

TEST(LineEndings, AlreadyLf) {
    std::string data = "hello\nworld\n";
    normalize_line_endings(data);
    EXPECT_EQ(data, "hello\nworld\n");
}

TEST(LineEndings, MixedEndings) {
    std::string data = "line1\r\nline2\rline3\nline4\r\n";
    normalize_line_endings(data);
    EXPECT_EQ(data, "line1\nline2\nline3\nline4\n");
}

TEST(LineEndings, EmptyString) {
    std::string data;
    normalize_line_endings(data);
    EXPECT_EQ(data, "");
}

TEST(LineEndings, NoCrOrLf) {
    std::string data = "no line endings here";
    normalize_line_endings(data);
    EXPECT_EQ(data, "no line endings here");
}

TEST(LineEndings, OnlyCr) {
    std::string data = "\r";
    normalize_line_endings(data);
    EXPECT_EQ(data, "\n");
}

TEST(LineEndings, OnlyCrLf) {
    std::string data = "\r\n";
    normalize_line_endings(data);
    EXPECT_EQ(data, "\n");
}

TEST(LineEndings, ConsecutiveCr) {
    std::string data = "\r\r\r";
    normalize_line_endings(data);
    EXPECT_EQ(data, "\n\n\n");
}

// ── UTF-8 Validation ─────────────────────────────────────────────────

TEST(Utf8Validation, ValidAscii) {
    const char data[] = "Hello, world!";
    EXPECT_EQ(validate_utf8(data, sizeof(data) - 1), -1);
}

TEST(Utf8Validation, ValidMultibyte) {
    // U+00E9 (e-acute): C3 A9
    // U+4E16 (Chinese char): E4 B8 96
    // U+1F600 (emoji): F0 9F 98 80
    const char data[] = "\xC3\xA9\xE4\xB8\x96\xF0\x9F\x98\x80";
    EXPECT_EQ(validate_utf8(data, sizeof(data) - 1), -1);
}

TEST(Utf8Validation, InvalidContinuationByte) {
    // Start of 2-byte sequence followed by non-continuation byte (0x28 = '(')
    const char data[] = "a\xC3\x28" "b";
    EXPECT_EQ(validate_utf8(data, sizeof(data) - 1), 1);
}

TEST(Utf8Validation, OverlongTwoByte) {
    // Overlong encoding of U+0001: C0 81 (should be just 01)
    const char data[] = "\xC0\x81";
    EXPECT_EQ(validate_utf8(data, sizeof(data) - 1), 0);
}

TEST(Utf8Validation, OverlongThreeByte) {
    // Overlong encoding of U+002F: E0 80 AF
    const char data[] = "\xE0\x80\xAF";
    EXPECT_EQ(validate_utf8(data, sizeof(data) - 1), 0);
}

TEST(Utf8Validation, SurrogateHalf) {
    // U+D800 encoded as UTF-8: ED A0 80
    const char data[] = "\xED\xA0\x80";
    EXPECT_EQ(validate_utf8(data, sizeof(data) - 1), 0);
}

TEST(Utf8Validation, SurrogateLow) {
    // U+DFFF encoded as UTF-8: ED BF BF
    const char data[] = "\xED\xBF\xBF";
    EXPECT_EQ(validate_utf8(data, sizeof(data) - 1), 0);
}

TEST(Utf8Validation, TruncatedSequence) {
    // Start of 3-byte sequence but only 2 bytes present
    const char data[] = "a\xE4\xB8";
    EXPECT_EQ(validate_utf8(data, sizeof(data) - 1), 1);
}

TEST(Utf8Validation, InvalidLeadingByte) {
    // 0xFE is not a valid leading byte
    const char data[] = "a\xFE";
    EXPECT_EQ(validate_utf8(data, sizeof(data) - 1), 1);
}

TEST(Utf8Validation, EmptyInput) {
    EXPECT_EQ(validate_utf8(nullptr, 0), -1);
    EXPECT_EQ(validate_utf8("", 0), -1);
}

TEST(Utf8Validation, BeyondUnicode) {
    // U+110000 would be F4 90 80 80 (beyond max codepoint)
    const char data[] = "\xF4\x90\x80\x80";
    EXPECT_EQ(validate_utf8(data, sizeof(data) - 1), 0);
}

// ── UTF-16 to UTF-8 Transcoding ──────────────────────────────────────

TEST(Transcode, Utf16LEBasicAscii) {
    // "Hi" in UTF-16 LE: 'H'=48 00, 'i'=69 00
    const char data[] = "\x48\x00\x69\x00";
    std::string result = transcode_to_utf8(data, 4, Encoding::UTF16_LE);
    EXPECT_EQ(result, "Hi");
}

TEST(Transcode, Utf16LEWithBom) {
    // BOM + "A" in UTF-16 LE: FF FE 41 00
    const char data[] = "\xFF\xFE\x41\x00";
    std::string result = transcode_to_utf8(data, 4, Encoding::UTF16_LE);
    EXPECT_EQ(result, "A");
}

TEST(Transcode, Utf16BEBasicAscii) {
    // "Hi" in UTF-16 BE: 00 48, 00 69
    const char data[] = "\x00\x48\x00\x69";
    std::string result = transcode_to_utf8(data, 4, Encoding::UTF16_BE);
    EXPECT_EQ(result, "Hi");
}

TEST(Transcode, Utf16LESurrogatePair) {
    // U+1F600 (grinning face) in UTF-16 LE: D83D DE00 -> 3D D8 00 DE
    const char data[] = "\x3D\xD8\x00\xDE";
    std::string result = transcode_to_utf8(data, 4, Encoding::UTF16_LE);
    EXPECT_EQ(result, "\xF0\x9F\x98\x80");
}

TEST(Transcode, Utf16BESurrogatePair) {
    // U+1F600 in UTF-16 BE: D8 3D DE 00
    const char data[] = "\xD8\x3D\xDE\x00";
    std::string result = transcode_to_utf8(data, 4, Encoding::UTF16_BE);
    EXPECT_EQ(result, "\xF0\x9F\x98\x80");
}

TEST(Transcode, Utf16LEMultibyte) {
    // U+00E9 (e-acute) in UTF-16 LE: E9 00
    const char data[] = "\xE9\x00";
    std::string result = transcode_to_utf8(data, 2, Encoding::UTF16_LE);
    EXPECT_EQ(result, "\xC3\xA9");
}

// ── ISO-8859-1 to UTF-8 Transcoding ─────────────────────────────────

TEST(Transcode, Iso8859_1Ascii) {
    const char data[] = "Hello";
    std::string result = transcode_to_utf8(data, 5, Encoding::ISO_8859_1);
    EXPECT_EQ(result, "Hello");
}

TEST(Transcode, Iso8859_1HighBytes) {
    // 0xE9 = e-acute in ISO-8859-1, maps to U+00E9 = C3 A9 in UTF-8
    // 0xF1 = n-tilde in ISO-8859-1, maps to U+00F1 = C3 B1 in UTF-8
    const char data[] = "\xE9\xF1";
    std::string result = transcode_to_utf8(data, 2, Encoding::ISO_8859_1);
    EXPECT_EQ(result, "\xC3\xA9\xC3\xB1");
}

TEST(Transcode, Iso8859_1FullRange) {
    // 0xA9 = copyright sign -> U+00A9 = C2 A9
    const char data[] = "\xA9";
    std::string result = transcode_to_utf8(data, 1, Encoding::ISO_8859_1);
    EXPECT_EQ(result, "\xC2\xA9");
}

// ── Empty Input Handling ─────────────────────────────────────────────

TEST(Encoding, DetectEmptyInput) {
    EXPECT_EQ(detect_encoding(nullptr, 0), Encoding::Unknown);
    EXPECT_EQ(detect_encoding("", 0), Encoding::Unknown);
}

TEST(Transcode, EmptyInput) {
    std::string result = transcode_to_utf8(nullptr, 0, Encoding::UTF8);
    EXPECT_EQ(result, "");

    result = transcode_to_utf8("", 0, Encoding::UTF16_LE);
    EXPECT_EQ(result, "");
}

TEST(Transcode, Utf8Passthrough) {
    const char data[] = "\xC3\xA9hello";
    std::string result = transcode_to_utf8(data, sizeof(data) - 1, Encoding::UTF8);
    EXPECT_EQ(result, "\xC3\xA9hello");
}

TEST(Transcode, Utf8WithBomStripped) {
    const char data[] = "\xEF\xBB\xBFhello";
    std::string result = transcode_to_utf8(data, sizeof(data) - 1, Encoding::UTF8);
    EXPECT_EQ(result, "hello");
}

// ── Encoding name helpers ────────────────────────────────────────────

TEST(Encoding, EncodingName) {
    EXPECT_STREQ(encoding_name(Encoding::UTF8), "UTF-8");
    EXPECT_STREQ(encoding_name(Encoding::UTF16_LE), "UTF-16LE");
    EXPECT_STREQ(encoding_name(Encoding::UTF16_BE), "UTF-16BE");
    EXPECT_STREQ(encoding_name(Encoding::ISO_8859_1), "ISO-8859-1");
    EXPECT_STREQ(encoding_name(Encoding::ASCII), "ASCII");
    EXPECT_STREQ(encoding_name(Encoding::Unknown), "Unknown");
}
