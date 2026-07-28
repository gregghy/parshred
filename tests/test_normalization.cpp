/// @file test_normalization.cpp
/// @brief Tests for FDOM_NORMALIZE: entity expansion and attribute normalization.
///
/// Coverage:
///   - Predefined entities in text (&lt; &gt; &amp; &apos; &quot;)
///   - Decimal character references (&#NNN;)
///   - Hex character references (&#xHHH;)
///   - Multi-byte Unicode via character references (&#8364; → U+20AC '€')
///   - Attribute normalization (\t \n \r → space, plus entity expansion)
///   - Mixed text with entities
///   - Unknown entities passed through unchanged
///   - FDOM_FASTEST does NOT expand entities (raw bytes preserved)
///   - FDOM_NORMALIZE DOES expand entities
///   - expand_entities_inline and normalize_attr_value helpers tested directly

#include <parshred/dom_fast.hpp>
#include <parshred/xpath.hpp>
#include <gtest/gtest.h>
#include <string>
#include <string_view>
#include <vector>

using namespace parshred;
using namespace parshred::xpath;

// ── Helper struct: keeps source string alive alongside the parsed DOM ──
//
// The FastDom holds raw pointers (name_ptr) and a data_ptr that point into
// the original source buffer.  Those pointers are only valid while the source
// string lives.  Use ParsedFastDom everywhere so the lifetime is correct.

struct ParsedFastDom {
    std::string src;   // source XML — must outlive dom
    FastDom     dom;

    ParsedFastDom() = default;

    // Disallow copies (FastDom is non-copyable)
    ParsedFastDom(const ParsedFastDom&) = delete;
    ParsedFastDom& operator=(const ParsedFastDom&) = delete;

    ParsedFastDom(ParsedFastDom&&) = default;
    ParsedFastDom& operator=(ParsedFastDom&&) = default;
};

template<unsigned Flags>
static ParsedFastDom parse_xml(std::string xml) {
    ParsedFastDom pfd;
    pfd.src = std::move(xml);
    pfd.dom = fast_dom_parse<Flags>(pfd.src.data(), pfd.src.size());
    return pfd;
}

static ParsedFastDom parse_normalized(std::string xml) {
    return parse_xml<FDOM_NORMALIZE>(std::move(xml));
}

static ParsedFastDom parse_fastest(std::string xml) {
    return parse_xml<FDOM_FASTEST>(std::move(xml));
}

static ParsedFastDom parse_default(std::string xml) {
    return parse_xml<0>(std::move(xml));
}

// ── Unit tests for expand_entities_inline ────────────────────────────

TEST(ExpandEntities, PredefinedLt) {
    std::vector<char> out;
    expand_entities_inline("&lt;", out);
    EXPECT_EQ(std::string(out.begin(), out.end()), "<");
}

TEST(ExpandEntities, PredefinedGt) {
    std::vector<char> out;
    expand_entities_inline("&gt;", out);
    EXPECT_EQ(std::string(out.begin(), out.end()), ">");
}

TEST(ExpandEntities, PredefinedAmp) {
    std::vector<char> out;
    expand_entities_inline("&amp;", out);
    EXPECT_EQ(std::string(out.begin(), out.end()), "&");
}

TEST(ExpandEntities, PredefinedApos) {
    std::vector<char> out;
    expand_entities_inline("&apos;", out);
    EXPECT_EQ(std::string(out.begin(), out.end()), "'");
}

TEST(ExpandEntities, PredefinedQuot) {
    std::vector<char> out;
    expand_entities_inline("&quot;", out);
    EXPECT_EQ(std::string(out.begin(), out.end()), "\"");
}

TEST(ExpandEntities, DecimalRef_A) {
    // &#65; → 'A' (U+0041)
    std::vector<char> out;
    expand_entities_inline("&#65;", out);
    EXPECT_EQ(std::string(out.begin(), out.end()), "A");
}

TEST(ExpandEntities, HexRef_A) {
    // &#x41; → 'A'
    std::vector<char> out;
    expand_entities_inline("&#x41;", out);
    EXPECT_EQ(std::string(out.begin(), out.end()), "A");
}

TEST(ExpandEntities, HexRef_uppercase) {
    // &#X41; — uppercase X should also work per implementation
    std::vector<char> out;
    expand_entities_inline("&#X41;", out);
    EXPECT_EQ(std::string(out.begin(), out.end()), "A");
}

TEST(ExpandEntities, MultiByte_Euro) {
    // &#8364; → U+20AC '€' encoded as 3-byte UTF-8: 0xE2 0x82 0xAC
    std::vector<char> out;
    expand_entities_inline("&#8364;", out);
    ASSERT_EQ(out.size(), 3u);
    EXPECT_EQ(static_cast<unsigned char>(out[0]), 0xE2u);
    EXPECT_EQ(static_cast<unsigned char>(out[1]), 0x82u);
    EXPECT_EQ(static_cast<unsigned char>(out[2]), 0xACu);
}

TEST(ExpandEntities, MultiByte_Euro_Hex) {
    // &#x20AC; → U+20AC '€'
    std::vector<char> out;
    expand_entities_inline("&#x20AC;", out);
    ASSERT_EQ(out.size(), 3u);
    EXPECT_EQ(static_cast<unsigned char>(out[0]), 0xE2u);
    EXPECT_EQ(static_cast<unsigned char>(out[1]), 0x82u);
    EXPECT_EQ(static_cast<unsigned char>(out[2]), 0xACu);
}

TEST(ExpandEntities, MultiByte_TwoByte) {
    // &#xE9; → U+00E9 'é' (Latin small letter e with acute)
    // 2-byte UTF-8: 0xC3 0xA9
    std::vector<char> out;
    expand_entities_inline("&#xE9;", out);
    ASSERT_EQ(out.size(), 2u);
    EXPECT_EQ(static_cast<unsigned char>(out[0]), 0xC3u);
    EXPECT_EQ(static_cast<unsigned char>(out[1]), 0xA9u);
}

TEST(ExpandEntities, FourByte_Emoji) {
    // &#x1F600; → U+1F600 😀 (4-byte UTF-8)
    std::vector<char> out;
    expand_entities_inline("&#x1F600;", out);
    ASSERT_EQ(out.size(), 4u);
    EXPECT_EQ(static_cast<unsigned char>(out[0]), 0xF0u);
    EXPECT_EQ(static_cast<unsigned char>(out[1]), 0x9Fu);
    EXPECT_EQ(static_cast<unsigned char>(out[2]), 0x98u);
    EXPECT_EQ(static_cast<unsigned char>(out[3]), 0x80u);
}

TEST(ExpandEntities, UnknownEntity_PassThrough) {
    // &foo; should be preserved as-is
    std::vector<char> out;
    expand_entities_inline("&foo;", out);
    EXPECT_EQ(std::string(out.begin(), out.end()), "&foo;");
}

TEST(ExpandEntities, UnknownEntity_Longer) {
    // &notanentity; — also preserved
    std::vector<char> out;
    expand_entities_inline("&notanentity;", out);
    EXPECT_EQ(std::string(out.begin(), out.end()), "&notanentity;");
}

TEST(ExpandEntities, PlainText_Unchanged) {
    std::vector<char> out;
    expand_entities_inline("hello world", out);
    EXPECT_EQ(std::string(out.begin(), out.end()), "hello world");
}

TEST(ExpandEntities, MixedContent) {
    // "a &lt; b &amp; c"  → "a < b & c"
    std::vector<char> out;
    expand_entities_inline("a &lt; b &amp; c", out);
    EXPECT_EQ(std::string(out.begin(), out.end()), "a < b & c");
}

TEST(ExpandEntities, MultipleEntities_Sequential) {
    // "&#65;&#x42;&#67;" → "ABC"
    std::vector<char> out;
    expand_entities_inline("&#65;&#x42;&#67;", out);
    EXPECT_EQ(std::string(out.begin(), out.end()), "ABC");
}

TEST(ExpandEntities, AppendMode) {
    // Should append to existing content
    std::vector<char> out;
    out.push_back('X');
    expand_entities_inline("&lt;", out);
    EXPECT_EQ(std::string(out.begin(), out.end()), "X<");
}

// ── Unit tests for normalize_attr_value ──────────────────────────────

TEST(NormalizeAttr, NoChange) {
    std::vector<char> out;
    auto [off, len] = normalize_attr_value("hello", out);
    std::string result(out.data() + off, len);
    EXPECT_EQ(result, "hello");
}

TEST(NormalizeAttr, TabReplaced) {
    std::vector<char> out;
    auto [off, len] = normalize_attr_value("a\tb", out);
    std::string result(out.data() + off, len);
    EXPECT_EQ(result, "a b");
}

TEST(NormalizeAttr, NewlineReplaced) {
    std::vector<char> out;
    auto [off, len] = normalize_attr_value("a\nb", out);
    std::string result(out.data() + off, len);
    EXPECT_EQ(result, "a b");
}

TEST(NormalizeAttr, CarriageReturnReplaced) {
    std::vector<char> out;
    auto [off, len] = normalize_attr_value("a\rb", out);
    std::string result(out.data() + off, len);
    EXPECT_EQ(result, "a b");
}

TEST(NormalizeAttr, MultipleWhitespaceChars) {
    std::vector<char> out;
    auto [off, len] = normalize_attr_value("a\t\n\rb", out);
    std::string result(out.data() + off, len);
    EXPECT_EQ(result, "a   b");  // three spaces, one per replaced char
}

TEST(NormalizeAttr, EntitiesExpanded) {
    std::vector<char> out;
    auto [off, len] = normalize_attr_value("a&amp;b", out);
    std::string result(out.data() + off, len);
    EXPECT_EQ(result, "a&b");
}

TEST(NormalizeAttr, WhitespaceAndEntities) {
    std::vector<char> out;
    auto [off, len] = normalize_attr_value("a\t&lt;b", out);
    std::string result(out.data() + off, len);
    EXPECT_EQ(result, "a <b");
}

TEST(NormalizeAttr, AppendMode_OffsetCorrect) {
    // Ensure the returned offset is correct when out already has data
    std::vector<char> out;
    out.insert(out.end(), {'X', 'Y', 'Z'});  // pre-existing 3 bytes
    auto [off, len] = normalize_attr_value("hi", out);
    EXPECT_EQ(off, 3u);
    EXPECT_EQ(len, 2u);
    std::string result(out.data() + off, len);
    EXPECT_EQ(result, "hi");
}

// ── Diagnostic test to understand node layout ─────────────────────────

TEST(FdomNormalizeDiag, NodeLayout_EntityOnlyText) {
    // Verify the full node layout for a minimal entity-only text
    std::string xml = "<root>&lt;</root>";
    auto dom = fast_dom_parse<FDOM_NORMALIZE>(xml.data(), xml.size());

    // We expect: sentinel(0), document(1), root(2), text(3)
    ASSERT_GE(dom.node_count, 4u);

    // Node 1: document
    EXPECT_EQ(dom.nodes[1].type, 0u);
    EXPECT_EQ(dom.nodes[1].first_child, 2u);  // root element

    // Node 2: root element
    EXPECT_EQ(dom.nodes[2].type, 1u);
    EXPECT_EQ(std::string(dom.name(dom.nodes[2])), "root");
    EXPECT_EQ(dom.nodes[2].first_child, 3u);  // text node

    // Node 3: text node
    EXPECT_EQ(dom.nodes[3].type, 2u);
    EXPECT_EQ(dom.nodes[3].flags & 0x01u, 1u);  // value in values buffer
    EXPECT_GE(dom.values.size(), 1u);
    EXPECT_EQ(dom.nodes[3].value_len, 1u);       // "<" is 1 byte
    EXPECT_EQ(dom.nodes[3].value_offset, 0u);

    // Value via dom.value()
    auto v = dom.value(dom.nodes[3]);
    EXPECT_EQ(v.size(), 1u);
    EXPECT_EQ(v[0], '<');

    // Now test evaluate and evaluate_string exactly as the failing test does
    auto results = evaluate(dom, "/root/text()");
    EXPECT_EQ(results.size(), 1u);
    if (!results.empty()) {
        auto sv = dom.value(dom.nodes[results[0]]);
        EXPECT_EQ(std::string(sv), "<");
    }

    // This is EXACTLY what TextEntity_Lt does — check if it fails here too
    auto text_via_es = evaluate_string(dom, "/root/text()");
    EXPECT_EQ(text_via_es, "<");

    // Also check evaluate_strings
    auto strings = evaluate_strings(dom, "/root/text()");
    EXPECT_EQ(strings.size(), 1u);
    if (!strings.empty()) { EXPECT_EQ(strings[0], "<"); }
}

// ── Integration tests: entity expansion in text nodes ─────────────────

TEST(FdomNormalize, TextEntity_Lt) {
    auto pfd  = parse_normalized("<root>&lt;</root>");
    auto& dom = pfd.dom;
    auto text = evaluate_string(dom, "/root/text()");
    EXPECT_EQ(text, "<");
}

TEST(FdomNormalize, TextEntity_Gt) {
    auto pfd  = parse_normalized("<root>&gt;</root>");
    auto& dom = pfd.dom;
    auto text = evaluate_string(dom, "/root/text()");
    EXPECT_EQ(text, ">");
}

TEST(FdomNormalize, TextEntity_Amp) {
    auto pfd  = parse_normalized("<root>&amp;</root>");
    auto& dom = pfd.dom;
    auto text = evaluate_string(dom, "/root/text()");
    EXPECT_EQ(text, "&");
}

TEST(FdomNormalize, TextEntity_Apos) {
    auto pfd  = parse_normalized("<root>&apos;</root>");
    auto& dom = pfd.dom;
    auto text = evaluate_string(dom, "/root/text()");
    EXPECT_EQ(text, "'");
}

TEST(FdomNormalize, TextEntity_Quot) {
    auto pfd  = parse_normalized("<root>&quot;</root>");
    auto& dom = pfd.dom;
    auto text = evaluate_string(dom, "/root/text()");
    EXPECT_EQ(text, "\"");
}

TEST(FdomNormalize, TextEntities_Multiple) {
    // "a &amp; b &lt; c"
    auto pfd  = parse_normalized("<root>a &amp; b &lt; c</root>");
    auto& dom = pfd.dom;
    auto text = get_text_content(dom, dom.root_idx);
    EXPECT_EQ(text, "a & b < c");
}

TEST(FdomNormalize, TextEntity_DecimalRef) {
    // &#65; → 'A'
    auto pfd  = parse_normalized("<root>&#65;</root>");
    auto& dom = pfd.dom;
    auto text = evaluate_string(dom, "/root/text()");
    EXPECT_EQ(text, "A");
}

TEST(FdomNormalize, TextEntity_HexRef) {
    // &#x41; → 'A'
    auto pfd  = parse_normalized("<root>&#x41;</root>");
    auto& dom = pfd.dom;
    auto text = evaluate_string(dom, "/root/text()");
    EXPECT_EQ(text, "A");
}

TEST(FdomNormalize, TextEntity_MultiByteUnicode) {
    // &#8364; → U+20AC '€' (3-byte UTF-8: 0xE2 0x82 0xAC)
    auto pfd  = parse_normalized("<root>&#8364;</root>");
    auto& dom = pfd.dom;
    auto text = evaluate_string(dom, "/root/text()");
    ASSERT_EQ(text.size(), 3u);
    EXPECT_EQ(static_cast<unsigned char>(text[0]), 0xE2u);
    EXPECT_EQ(static_cast<unsigned char>(text[1]), 0x82u);
    EXPECT_EQ(static_cast<unsigned char>(text[2]), 0xACu);
}

TEST(FdomNormalize, TextEntity_MultiByteHex) {
    // &#x20AC; → U+20AC '€'
    auto pfd  = parse_normalized("<root>&#x20AC;</root>");
    auto& dom = pfd.dom;
    auto text = evaluate_string(dom, "/root/text()");
    ASSERT_EQ(text.size(), 3u);
    EXPECT_EQ(static_cast<unsigned char>(text[0]), 0xE2u);
    EXPECT_EQ(static_cast<unsigned char>(text[1]), 0x82u);
    EXPECT_EQ(static_cast<unsigned char>(text[2]), 0xACu);
}

TEST(FdomNormalize, TextEntity_UnknownPassThrough) {
    // &foo; — unknown entity, passed through as-is
    auto pfd  = parse_normalized("<root>&foo;</root>");
    auto& dom = pfd.dom;
    auto text = evaluate_string(dom, "/root/text()");
    EXPECT_EQ(text, "&foo;");
}

TEST(FdomNormalize, TextEntity_MixedWithLiteral) {
    auto pfd  = parse_normalized("<root>Hello &amp; World</root>");
    auto& dom = pfd.dom;
    auto text = get_text_content(dom, dom.root_idx);
    EXPECT_EQ(text, "Hello & World");
}

TEST(FdomNormalize, TextEntity_SequentialRefs) {
    // &#65;&#x42;&#67; → "ABC"
    auto pfd  = parse_normalized("<root>&#65;&#x42;&#67;</root>");
    auto& dom = pfd.dom;
    auto text = get_text_content(dom, dom.root_idx);
    EXPECT_EQ(text, "ABC");
}

// ── Integration tests: attribute normalization ─────────────────────────

TEST(FdomNormalize, AttrEntity_Amp) {
    auto pfd  = parse_normalized(R"(<root val="a&amp;b"/>)");
    auto& dom = pfd.dom;
    auto v = dom.attr(dom.root(), "val");
    EXPECT_EQ(v, "a&b");
}

TEST(FdomNormalize, AttrEntity_Lt) {
    auto pfd  = parse_normalized(R"(<root val="a&lt;b"/>)");
    auto& dom = pfd.dom;
    auto v = dom.attr(dom.root(), "val");
    EXPECT_EQ(v, "a<b");
}

TEST(FdomNormalize, AttrEntity_Quot) {
    auto pfd  = parse_normalized(R"(<root val="&quot;quoted&quot;"/>)");
    auto& dom = pfd.dom;
    auto v = dom.attr(dom.root(), "val");
    EXPECT_EQ(v, "\"quoted\"");
}

TEST(FdomNormalize, AttrWhitespace_Tab) {
    // Attribute containing literal tab → replaced by space
    auto pfd  = parse_normalized("<root val=\"a\tb\"/>");
    auto& dom = pfd.dom;
    auto v = dom.attr(dom.root(), "val");
    EXPECT_EQ(v, "a b");
}

TEST(FdomNormalize, AttrWhitespace_Newline) {
    auto pfd  = parse_normalized("<root val=\"a\nb\"/>");
    auto& dom = pfd.dom;
    auto v = dom.attr(dom.root(), "val");
    EXPECT_EQ(v, "a b");
}

TEST(FdomNormalize, AttrWhitespace_CR) {
    auto pfd  = parse_normalized("<root val=\"a\rb\"/>");
    auto& dom = pfd.dom;
    auto v = dom.attr(dom.root(), "val");
    EXPECT_EQ(v, "a b");
}

TEST(FdomNormalize, AttrEntity_And_Whitespace) {
    auto pfd  = parse_normalized("<root val=\"a\t&amp;b\"/>");
    auto& dom = pfd.dom;
    auto v = dom.attr(dom.root(), "val");
    EXPECT_EQ(v, "a &b");
}

TEST(FdomNormalize, AttrDecimalRef) {
    auto pfd  = parse_normalized(R"(<root val="&#65;"/>)");
    auto& dom = pfd.dom;
    auto v = dom.attr(dom.root(), "val");
    EXPECT_EQ(v, "A");
}

TEST(FdomNormalize, AttrHexRef) {
    auto pfd  = parse_normalized(R"(<root val="&#x41;"/>)");
    auto& dom = pfd.dom;
    auto v = dom.attr(dom.root(), "val");
    EXPECT_EQ(v, "A");
}

TEST(FdomNormalize, AttrMultiByte_Euro) {
    auto pfd  = parse_normalized(R"(<root val="&#8364;"/>)");
    auto& dom = pfd.dom;
    auto v = dom.attr(dom.root(), "val");
    ASSERT_EQ(v.size(), 3u);
    EXPECT_EQ(static_cast<unsigned char>(v[0]), 0xE2u);
    EXPECT_EQ(static_cast<unsigned char>(v[1]), 0x82u);
    EXPECT_EQ(static_cast<unsigned char>(v[2]), 0xACu);
}

TEST(FdomNormalize, AttrUnknownEntity_PassThrough) {
    auto pfd  = parse_normalized(R"(<root val="&foo;"/>)");
    auto& dom = pfd.dom;
    auto v = dom.attr(dom.root(), "val");
    EXPECT_EQ(v, "&foo;");
}

// ── XPath integration ─────────────────────────────────────────────────

TEST(FdomNormalize, XPath_EvaluateString_Attr) {
    auto pfd  = parse_normalized(R"(<config db="a&amp;b"/>)");
    auto& dom = pfd.dom;
    EXPECT_EQ(evaluate_string(dom, "/config/@db"), "a&b");
}

TEST(FdomNormalize, XPath_EvaluateString_Text) {
    auto pfd  = parse_normalized("<root><item>a &lt; b</item></root>");
    auto& dom = pfd.dom;
    EXPECT_EQ(evaluate_string(dom, "/root/item/text()"), "a < b");
}

TEST(FdomNormalize, XPath_GetTextContent) {
    auto pfd  = parse_normalized("<root><p>Hello &amp; World</p></root>");
    auto& dom = pfd.dom;
    auto results = evaluate(dom, "/root/p");
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(get_text_content(dom, results[0]), "Hello & World");
}

TEST(FdomNormalize, XPath_GetTextContent_MixedChildren) {
    // Mixed text and sub-elements — get_text_content concatenates all text descendants
    auto pfd  = parse_normalized("<root><p>Hello <b>&amp;</b> World</p></root>");
    auto& dom = pfd.dom;
    auto results = evaluate(dom, "/root/p");
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(get_text_content(dom, results[0]), "Hello & World");
}

TEST(FdomNormalize, XPath_AttrEqualsPredicate_WithEntity) {
    // A predicate that matches a normalized attribute value
    auto pfd  = parse_normalized(R"(<root><item id="a&amp;b"/><item id="c"/></root>)");
    auto& dom = pfd.dom;
    auto matches = evaluate(dom, "/root/item[@id='a&b']");
    EXPECT_EQ(matches.size(), 1u);
}

// ── Mode comparison: FDOM_FASTEST preserves raw bytes, FDOM_NORMALIZE expands ──

TEST(FdomModeComparison, Fastest_PreservesRawEntities) {
    // FDOM_FASTEST skips text nodes entirely — verify the element structure
    // is correct and no crash occurs on entity-laden input.
    // Note: in FASTEST mode, attrs on child elements are accessible via
    // navigation, not on the root. We verify the root element parses without crash,
    // and the child <a>'s attr is accessible via dom.attr on the first_child.
    auto pfd  = parse_fastest("<root><a id=\"1&amp;2\"/></root>");
    auto& dom = pfd.dom;
    // Navigate to the <a> child (first child of root)
    const auto* a_elem = dom.first_child(dom.root());
    ASSERT_NE(a_elem, nullptr);
    EXPECT_EQ(std::string(dom.name(*a_elem)), "a");
    // In FASTEST mode attributes are zero-copy: value points into source, raw "&amp;"
    auto v = dom.attr(*a_elem, "id");
    EXPECT_EQ(v, "1&amp;2");  // raw, not expanded
}

TEST(FdomModeComparison, Default_PreservesRawEntities_InText) {
    // With Flags=0 (no FDOM_NORMALIZE), text is raw
    auto pfd  = parse_default("<root>a &amp; b</root>");
    auto& dom = pfd.dom;
    auto text = evaluate_string(dom, "/root/text()");
    // Raw text — entity NOT expanded
    EXPECT_EQ(text, "a &amp; b");
}

TEST(FdomModeComparison, Default_PreservesRawEntities_InAttr) {
    auto pfd  = parse_default(R"(<root val="a&amp;b"/>)");
    auto& dom = pfd.dom;
    auto v = dom.attr(dom.root(), "val");
    EXPECT_EQ(v, "a&amp;b");  // raw, not expanded
}

TEST(FdomModeComparison, Normalize_ExpandsEntities_InText) {
    auto pfd  = parse_normalized("<root>a &amp; b</root>");
    auto& dom = pfd.dom;
    auto text = evaluate_string(dom, "/root/text()");
    EXPECT_EQ(text, "a & b");  // expanded
}

TEST(FdomModeComparison, Normalize_ExpandsEntities_InAttr) {
    auto pfd  = parse_normalized(R"(<root val="a&amp;b"/>)");
    auto& dom = pfd.dom;
    auto v = dom.attr(dom.root(), "val");
    EXPECT_EQ(v, "a&b");  // expanded
}

TEST(FdomModeComparison, Normalize_DoesNotBreakStructure) {
    // Verify the DOM structure (elements, attributes, nesting) is intact
    // after normalization.
    auto pfd = parse_normalized(R"(
        <root>
            <child id="1" name="a&amp;b">text &lt; here</child>
            <child id="2" name="plain">more text</child>
        </root>
    )");
    auto& dom = pfd.dom;

    // Two child elements
    EXPECT_EQ(evaluate_count(dom, "/root/child"), 2u);

    // First child's attributes
    EXPECT_EQ(evaluate_string(dom, "/root/child[1]/@id"), "1");
    EXPECT_EQ(evaluate_string(dom, "/root/child[1]/@name"), "a&b");

    // First child's text content
    auto text1 = evaluate_string(dom, "/root/child[1]/text()");
    EXPECT_EQ(text1, "text < here");

    // Second child
    EXPECT_EQ(evaluate_string(dom, "/root/child[2]/@id"), "2");
    EXPECT_EQ(evaluate_string(dom, "/root/child[2]/@name"), "plain");
    auto text2 = evaluate_string(dom, "/root/child[2]/text()");
    EXPECT_EQ(text2, "more text");
}

// ── Edge cases ────────────────────────────────────────────────────────

TEST(FdomNormalize, EmptyText) {
    // An element with no text — no crash
    auto pfd  = parse_normalized("<root/>");
    auto& dom = pfd.dom;
    EXPECT_EQ(dom.root_idx, 2u);  // sentinel(0), document(1), root(2)
    auto text = evaluate_string(dom, "/root/text()");
    EXPECT_EQ(text, "");
}

TEST(FdomNormalize, EmptyAttr) {
    auto pfd  = parse_normalized(R"(<root val=""/>)");
    auto& dom = pfd.dom;
    auto v = dom.attr(dom.root(), "val");
    EXPECT_EQ(v, "");
}

TEST(FdomNormalize, EntityAtEndOfText) {
    // Entity at the very end of text content (no trailing plain text)
    auto pfd  = parse_normalized("<root>hello &amp;</root>");
    auto& dom = pfd.dom;
    auto text = get_text_content(dom, dom.root_idx);
    EXPECT_EQ(text, "hello &");
}

TEST(FdomNormalize, EntityAtStartOfText) {
    auto pfd  = parse_normalized("<root>&lt;hello</root>");
    auto& dom = pfd.dom;
    auto text = get_text_content(dom, dom.root_idx);
    EXPECT_EQ(text, "<hello");
}

TEST(FdomNormalize, OnlyEntity) {
    auto pfd  = parse_normalized("<root>&amp;</root>");
    auto& dom = pfd.dom;
    auto text = get_text_content(dom, dom.root_idx);
    EXPECT_EQ(text, "&");
}

TEST(FdomNormalize, ConsecutiveEntities) {
    // "&lt;&gt;&amp;&apos;&quot;" → "<>&'\""
    auto pfd  = parse_normalized("<root>&lt;&gt;&amp;&apos;&quot;</root>");
    auto& dom = pfd.dom;
    auto text = get_text_content(dom, dom.root_idx);
    EXPECT_EQ(text, "<>&'\"");
}

TEST(FdomNormalize, ManyElements_Normalized) {
    // Stress: many elements with entities in text and attributes
    std::string xml = "<root>";
    for (int i = 0; i < 500; ++i) {
        xml += "<item id=\"val&amp;";
        xml += std::to_string(i);
        xml += "\">text &lt; ";
        xml += std::to_string(i);
        xml += "</item>";
    }
    xml += "</root>";

    auto pfd  = parse_normalized(std::move(xml));
    auto& dom = pfd.dom;
    EXPECT_EQ(evaluate_count(dom, "/root/item"), 500u);

    // Spot-check first and last
    auto first_id = evaluate_string(dom, "/root/item[1]/@id");
    EXPECT_EQ(first_id, "val&0");

    auto first_text = evaluate_string(dom, "/root/item[1]/text()");
    EXPECT_EQ(first_text, "text < 0");
}
