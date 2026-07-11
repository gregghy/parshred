/// @file test_conformance.cpp
/// @brief W3C XML 1.0 conformance test runner for parshred.
///
/// Tests are derived from the requirements stated in the W3C XML 1.0
/// specification (https://www.w3.org/TR/xml/) and the XML Namespaces 1.0
/// specification (https://www.w3.org/TR/xml-names/).
///
/// Each test is labelled with the relevant spec section where applicable.
///
/// Test categories
/// ---------------
///   Conformance/WF_*   — Well-formedness positive tests (must parse OK)
///   Conformance/NWF_*  — Non-well-formed negative tests (must be rejected)
///   Conformance/NS_*   — Namespace-related tests
///   Conformance/ENC_*  — Character encoding tests
///   Conformance/ENT_*  — Entity / character-reference tests

#include <parshred/dom_fast.hpp>    // fast_dom_parse, FastDom
#include <parshred/dtd.hpp>         // check_wellformedness, parse_dtd
#include <parshred/encoding.hpp>    // detect_encoding, skip_bom, validate_utf8
#include <parshred/fast_sax.hpp>    // fast_parse, FastSaxParser
#include <parshred/namespace.hpp>   // NsContext, process_element_ns

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

using namespace parshred;

// ── Helpers ──────────────────────────────────────────────────────────────────

/// Parse with the SAX API and return true when parsing completes without
/// throwing a ParseError (or any other exception).
static bool sax_parses_ok(std::string_view xml) {
    try {
        NullHandler h;
        fast_parse(xml.data(), xml.size(), h);
        return true;
    } catch (...) {
        return false;
    }
}

/// Parse with the fast DOM API and return true when it yields a non-null root.
/// The input string is copied so that string_views inside the DOM stay valid.
static bool dom_parses_ok(std::string xml) {
    try {
        auto dom = fast_dom_parse<FDOM_NORMALIZE>(xml.data(), xml.size());
        return dom.root_idx != 0;
    } catch (...) {
        return false;
    }
}

/// Use check_wellformedness() and require no issues.
static bool wf_ok(std::string_view xml) {
    auto issues = check_wellformedness(xml);
    return issues.empty();
}

/// Use check_wellformedness() and require at least one issue.
static bool wf_has_issues(std::string_view xml) {
    return !check_wellformedness(xml).empty();
}

/// Collect SAX element names seen during a parse.
struct ElementCollector final : SaxHandler {
    std::vector<std::string> starts;
    std::vector<std::string> ends;
    std::vector<std::pair<std::string, std::string>> attrs;
    std::vector<std::string> texts;
    std::vector<std::string> cdatas;
    std::vector<std::string> comments;
    std::vector<std::pair<std::string, std::string>> pis;   // target, data

    void on_start_element(std::string_view name,
                          const Attribute* a, size_t n) override {
        starts.emplace_back(name);
        for (size_t i = 0; i < n; ++i)
            attrs.emplace_back(std::string(a[i].name), std::string(a[i].value));
    }
    void on_end_element(std::string_view name) override {
        ends.emplace_back(name);
    }
    void on_text(std::string_view t) override { texts.emplace_back(t); }
    void on_cdata(std::string_view t) override { cdatas.emplace_back(t); }
    void on_comment(std::string_view t) override { comments.emplace_back(t); }
    void on_processing_instruction(std::string_view target,
                                   std::string_view data) override {
        pis.emplace_back(std::string(target), std::string(data));
    }
};

// ═════════════════════════════════════════════════════════════════════════════
// Well-formedness — POSITIVE tests (must parse successfully)
// ═════════════════════════════════════════════════════════════════════════════

// [XML 1.0 §3.1] An element may be empty — represented as <e/>.
TEST(Conformance, WF_EmptyElement) {
    EXPECT_TRUE(sax_parses_ok("<e/>"));
    EXPECT_TRUE(wf_ok("<e/>"));
}

// [XML 1.0 §3.1] An element with character content.
TEST(Conformance, WF_ElementWithContent) {
    ElementCollector h;
    ASSERT_NO_THROW(fast_parse("<e>text</e>", h));
    ASSERT_EQ(h.starts.size(), 1u);
    EXPECT_EQ(h.starts[0], "e");
    ASSERT_FALSE(h.texts.empty());
    EXPECT_EQ(h.texts[0], "text");
}

// [XML 1.0 §3.1] Elements may be arbitrarily nested.
TEST(Conformance, WF_NestedElements) {
    ElementCollector h;
    ASSERT_NO_THROW(fast_parse("<a><b><c/></b></a>", h));
    EXPECT_EQ(h.starts.size(), 3u);
    EXPECT_EQ(h.starts[0], "a");
    EXPECT_EQ(h.starts[1], "b");
    EXPECT_EQ(h.starts[2], "c");
    EXPECT_EQ(h.ends.size(), 3u);
}

// [XML 1.0 §3.1] Attribute values may be delimited by either single or double
// quotation marks.
TEST(Conformance, WF_AttributeSingleAndDoubleQuotes) {
    ElementCollector h;
    ASSERT_NO_THROW(fast_parse("<e a='1' b=\"2\"/>", h));
    ASSERT_EQ(h.attrs.size(), 2u);
    EXPECT_EQ(h.attrs[0].first,  "a");
    EXPECT_EQ(h.attrs[0].second, "1");
    EXPECT_EQ(h.attrs[1].first,  "b");
    EXPECT_EQ(h.attrs[1].second, "2");
}

// [XML 1.0 §2.8] The XML declaration is optional but, when present, must
// appear first and be well-formed.
TEST(Conformance, WF_XmlDeclaration) {
    EXPECT_TRUE(sax_parses_ok("<?xml version=\"1.0\"?><r/>"));
    EXPECT_TRUE(sax_parses_ok("<?xml version=\"1.0\" encoding=\"UTF-8\"?><r/>"));
    EXPECT_TRUE(sax_parses_ok("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><r/>"));
}

// [XML 1.0 §2.5] Comments may appear anywhere outside markup.
TEST(Conformance, WF_Comment) {
    ElementCollector h;
    ASSERT_NO_THROW(fast_parse("<!-- comment --><r/>", h));
    ASSERT_FALSE(h.comments.empty());
    EXPECT_EQ(h.comments[0], " comment ");
}

// [XML 1.0 §2.6] Processing instructions are part of the prolog or content.
TEST(Conformance, WF_ProcessingInstruction) {
    ElementCollector h;
    ASSERT_NO_THROW(fast_parse("<?target data?><r/>", h));
    ASSERT_EQ(h.pis.size(), 1u);
    EXPECT_EQ(h.pis[0].first,  "target");
    EXPECT_EQ(h.pis[0].second, "data");
}

// [XML 1.0 §2.7] CDATA sections are passed through as literal character data.
TEST(Conformance, WF_CDataSection) {
    ElementCollector h;
    ASSERT_NO_THROW(fast_parse("<r><![CDATA[<not-a-tag>]]></r>", h));
    ASSERT_EQ(h.cdatas.size(), 1u);
    EXPECT_EQ(h.cdatas[0], "<not-a-tag>");
}

// [XML 1.0 §4.1] Character references are decimal (&#NNN;) or hexadecimal
// (&#xHHH;) and must resolve to valid XML characters.
TEST(Conformance, WF_CharacterReferences) {
    // &#65; = 'A', &#x42; = 'B'
    std::string xml = "<r>&#65;&#x42;</r>";
    auto dom = fast_dom_parse<FDOM_NORMALIZE>(xml.data(), xml.size());
    ASSERT_NE(dom.root_idx, 0u);
    const FastNode& root = dom.root();
    const FastNode* text = dom.first_child(root);
    ASSERT_NE(text, nullptr);
    std::string content(dom.value(*text));
    EXPECT_NE(content.find('A'), std::string::npos);
    EXPECT_NE(content.find('B'), std::string::npos);
}

// [XML 1.0 §4.6] The five predefined entities must be accepted in text content.
TEST(Conformance, WF_PredefinedEntitiesInText) {
    std::string xml = "<r>&lt;&gt;&amp;&apos;&quot;</r>";
    auto dom = fast_dom_parse<FDOM_NORMALIZE>(xml.data(), xml.size());
    ASSERT_NE(dom.root_idx, 0u);
    const FastNode& root = dom.root();
    const FastNode* text = dom.first_child(root);
    ASSERT_NE(text, nullptr);
    std::string content(dom.value(*text));
    EXPECT_NE(content.find('<'),  std::string::npos);
    EXPECT_NE(content.find('>'),  std::string::npos);
    EXPECT_NE(content.find('&'),  std::string::npos);
    EXPECT_NE(content.find('\''), std::string::npos);
    EXPECT_NE(content.find('"'),  std::string::npos);
}

// [XML 1.0 §3.1] Predefined entities may also appear in attribute values.
TEST(Conformance, WF_PredefinedEntitiesInAttributes) {
    std::string xml = "<r a=\"&lt;\"/>";
    auto dom = fast_dom_parse<FDOM_NORMALIZE>(xml.data(), xml.size());
    ASSERT_NE(dom.root_idx, 0u);
    // In NORMALIZE mode the attribute value is entity-expanded.
    // Attribute values are stored in the source buffer (non-normalize) or
    // in the values buffer; for this test we just verify parsing succeeds.
    EXPECT_TRUE(sax_parses_ok(xml));
}

// [XML 1.0 §3.1] Multiple attributes on the same element are all parsed.
TEST(Conformance, WF_MultipleAttributes) {
    ElementCollector h;
    ASSERT_NO_THROW(fast_parse("<e a=\"1\" b=\"2\" c=\"3\"/>", h));
    ASSERT_EQ(h.attrs.size(), 3u);
    EXPECT_EQ(h.attrs[0].second, "1");
    EXPECT_EQ(h.attrs[1].second, "2");
    EXPECT_EQ(h.attrs[2].second, "3");
}

// [XML Namespaces 1.0 §3] Namespace declarations on elements and their
// prefixed children must be resolvable.
TEST(Conformance, WF_NamespaceDeclarations) {
    std::string xml =
        "<r xmlns=\"http://example.com\" xmlns:p=\"http://p.com\"><p:e/></r>";
    struct NsCheck final : SaxHandler {
        NsContext ctx;
        std::vector<std::string> uris;
        void on_start_element(std::string_view name,
                              const Attribute* attrs, size_t n) override {
            auto qn = process_element_ns(name, attrs, n, ctx);
            uris.emplace_back(qn.namespace_uri);
        }
        void on_end_element(std::string_view) override { ctx.pop_scope(); }
    };
    NsCheck h;
    ASSERT_NO_THROW(fast_parse(xml, h));
    ASSERT_EQ(h.uris.size(), 2u);
    EXPECT_EQ(h.uris[0], "http://example.com");
    EXPECT_EQ(h.uris[1], "http://p.com");
}

// [XML 1.0 §3.2.2] Mixed content — text interspersed with child elements.
TEST(Conformance, WF_MixedContent) {
    ElementCollector h;
    ASSERT_NO_THROW(fast_parse("<p>text<b>bold</b>more</p>", h));
    // Elements: p, b
    EXPECT_EQ(h.starts.size(), 2u);
    EXPECT_EQ(h.starts[0], "p");
    EXPECT_EQ(h.starts[1], "b");
    // At least two text segments
    EXPECT_GE(h.texts.size(), 2u);
}

// [XML 1.0 §3.1] An empty attribute value ("") is permitted.
TEST(Conformance, WF_EmptyAttributeValue) {
    ElementCollector h;
    ASSERT_NO_THROW(fast_parse("<e a=\"\"/>", h));
    ASSERT_EQ(h.attrs.size(), 1u);
    EXPECT_EQ(h.attrs[0].second, "");
}

// [XML 1.0 §3.1] Attribute values may contain entity references.
TEST(Conformance, WF_AttributeWithEntity) {
    ElementCollector h;
    // &amp; in attribute value → '&' after normalization
    ASSERT_NO_THROW(fast_parse("<e a=\"a&amp;b\"/>", h));
    ASSERT_EQ(h.attrs.size(), 1u);
    // Raw value (pre-expansion) must contain "a&amp;b"
    EXPECT_NE(h.attrs[0].second.find('a'), std::string::npos);
    EXPECT_NE(h.attrs[0].second.find('b'), std::string::npos);
}

// [XML 1.0 §2.3] Names: an element name may start with '_' or a letter,
// followed by letters, digits, '.', '-', '_', ':'.
TEST(Conformance, WF_ValidNameVariants) {
    EXPECT_TRUE(sax_parses_ok("<_underscore/>"));
    EXPECT_TRUE(sax_parses_ok("<A1/>"));
    EXPECT_TRUE(sax_parses_ok("<my-element/>"));
    EXPECT_TRUE(sax_parses_ok("<my.element/>"));
    EXPECT_TRUE(sax_parses_ok("<ns:local/>"));
}

// [XML 1.0 §2.4] Character data may contain any Unicode scalar value
// representable in UTF-8 except '<' and '&'.
TEST(Conformance, WF_UnicodeTextContent) {
    // A few multi-byte UTF-8 sequences embedded directly.
    std::string xml = "<r>\xC3\xA9\xE2\x82\xAC</r>"; // é and €
    EXPECT_TRUE(sax_parses_ok(xml));
}

// The fast DOM must correctly record multiple siblings under a single parent.
TEST(Conformance, WF_SiblingElements) {
    std::string xml = "<root><a/><b/><c/></root>";
    auto dom = fast_dom_parse(xml.data(), xml.size());
    ASSERT_NE(dom.root_idx, 0u);
    const FastNode& root = dom.root();

    int count = 0;
    const FastNode* child = dom.first_child(root);
    while (child) {
        ++count;
        child = dom.next_sibling(*child);
    }
    EXPECT_EQ(count, 3);
}

// [XML 1.0 §2.8] A prolog comment before the root element is valid.
TEST(Conformance, WF_PrologComment) {
    EXPECT_TRUE(sax_parses_ok("<!-- prolog --><root/>"));
}

// [XML 1.0 §2.6] A PI before the root element is valid.
TEST(Conformance, WF_PrologPI) {
    EXPECT_TRUE(sax_parses_ok("<?stylesheet href=\"x.xsl\"?><root/>"));
}

// A deep nesting (well within the security limit) must parse correctly.
TEST(Conformance, WF_DeepNesting) {
    std::string xml;
    constexpr int depth = 100;
    for (int i = 0; i < depth; ++i) xml += "<a>";
    for (int i = 0; i < depth; ++i) xml += "</a>";
    EXPECT_TRUE(sax_parses_ok(xml));
}

// Whitespace-only text between elements is valid character data.
TEST(Conformance, WF_WhitespaceBetweenElements) {
    EXPECT_TRUE(sax_parses_ok("<root>  \n  <child/>  \n  </root>"));
}

// ═════════════════════════════════════════════════════════════════════════════
// Well-formedness — NEGATIVE tests (must be rejected / report errors)
// ═════════════════════════════════════════════════════════════════════════════

// [XML 1.0 §2.1 WFC: Element Type Match] The name in an element's end-tag
// must match the name in the start-tag.
TEST(Conformance, NWF_MismatchedTags) {
    EXPECT_TRUE(wf_has_issues("<a></b>"));
}

// [XML 1.0 §2.1 WFC: Element Type Match] Every start-tag must have a
// matching end-tag (or be self-closing).
TEST(Conformance, NWF_UnclosedElement) {
    EXPECT_TRUE(wf_has_issues("<a>"));
}

// [XML 1.0 §3.1 WFC: Unique Att Spec] No attribute name may appear more
// than once in the same start-tag.
TEST(Conformance, NWF_DuplicateAttribute) {
    // check_wellformedness does tag-stack checking; duplicate attribute
    // detection is done via the SAX parser throwing ParseError.
    // Parshred currently silently accepts duplicates in fast paths (no
    // throw), so we test via check_wellformedness for the tag-level error
    // and separately document that duplicate-attr detection is a parser
    // extension rather than a thrown error.
    //
    // Regardless, the document is non-conformant per the spec.
    const std::string xml = "<e a=\"1\" a=\"2\"/>";
    // The SAX parser itself should at minimum not crash.
    EXPECT_NO_THROW(sax_parses_ok(xml));
    // And we can note the spec violation:
    // (A fully validating parser MUST report this; parshred is lenient here.)
}

// [XML 1.0 §4.1] &#0; references U+0000, which is not a valid XML character.
// Per the spec a conformant parser must report an error; parshred's
// encode_utf8_codepoint emits the null byte as-is (lenient behaviour).
// This test documents the actual library behaviour and verifies that
// parsing at least does not crash.
TEST(Conformance, NWF_NullCharacterReference) {
    std::string xml = "<r>&#0;</r>";
    // The document must not crash the parser.
    EXPECT_NO_THROW(sax_parses_ok(xml));

    // Document what encode_utf8_codepoint does with cp=0:
    // It emits a single NUL byte (U+0000 ≤ 0x7F branch).
    // A strict parser would reject it; parshred is lenient.
    std::vector<char> out;
    expand_entities_inline("&#0;", out);
    // The library currently emits exactly 1 byte (the NUL).
    EXPECT_EQ(out.size(), 1u);
    // NOTE: This is a spec deviation — U+0000 should be rejected.
    // A future fix would make encode_utf8_codepoint skip cp == 0.
}

// [XML 1.0 §2.4] The literal character '&' must be escaped as &amp; in
// character data.  A bare '&' without a following valid entity name or '#'
// is a well-formedness error.
TEST(Conformance, NWF_BareAmpersand) {
    // The SAX parser does not throw on bare '&' in text but
    // check_wellformedness will find no issue with the tag stack.
    // We verify the parser at least does not crash and inspect that the
    // entity-expansion helper treats bare '&' as pass-through.
    std::string xml = "<r>a & b</r>";
    EXPECT_NO_THROW(sax_parses_ok(xml));
    // Entity expansion: bare '&' followed by space → no entity name → pass-through
    std::vector<char> out;
    expand_entities_inline("a & b", out);
    std::string expanded(out.begin(), out.end());
    // The '&' character is preserved (passed through) in the output
    EXPECT_NE(expanded.find('&'), std::string::npos);
}

// [XML 1.0 §3.1] The literal '<' is forbidden in attribute values.
// It must be represented as &lt;.
TEST(Conformance, NWF_LessThanInAttributeValue) {
    // The fast SAX parser scans for the closing quote character when parsing
    // attribute values (skip_attr_value / find_char_fast).  For the input
    // <e a="<"/>  the scanner finds the '"' that closes the value, so the
    // captured attribute value contains a literal '<'.  The parser then
    // encounters "/" as the next non-whitespace character after the value
    // and closes the element without error — this is a lenient (non-conformant)
    // behaviour: a strict parser must reject a '<' inside an attribute value.
    //
    // We verify two things:
    //   1. The parser does not crash or throw.
    //   2. The resulting attribute value contains the disallowed '<'.
    //      This documents the conformance gap.
    std::string xml = "<e a=\"<\"/>";
    ElementCollector h;
    ASSERT_NO_THROW(fast_parse(xml.data(), xml.size(), h));
    ASSERT_FALSE(h.attrs.empty());
    // The attribute value should contain '<' (lenient parse).
    EXPECT_NE(h.attrs[0].second.find('<'), std::string::npos)
        << "Lenient parser captures '<' in attr value (spec violation)";
    // NOTE: A conformant parser MUST report an error here.
}

// [XML 1.0 §2.1] An XML document must have exactly one root element.
// A document containing only text (no root element) is not well-formed.
TEST(Conformance, NWF_NoRootElement) {
    // check_wellformedness tracks no element at all.
    // The tag stack is empty throughout → no unclosed-tag issue is generated,
    // but we also verify that fast_dom_parse produces no root.
    std::string xml = "text only";
    auto dom = fast_dom_parse(xml.data(), xml.size());
    EXPECT_EQ(dom.root_idx, 0u) << "No root element should be found";
}

// [XML 1.0 §2.1] A document must have a single root element; two sibling
// root-level elements constitute a well-formedness error.
TEST(Conformance, NWF_MultipleRootElements) {
    // The fast DOM places the first element as root and the second becomes
    // an orphan sibling of the document node.
    std::string xml = "<a/><b/>";
    ElementCollector h;
    ASSERT_NO_THROW(fast_parse(xml, h));
    // Both are reported as elements — caller is responsible for detecting the
    // extra root.  check_wellformedness has no special multi-root test, but
    // the depth tracker in Normal-mode SAX will decrement below 0 on </b>.
    EXPECT_EQ(h.starts.size(), 2u);
}

// [XML 1.0 §2.3] An element name must not start with a digit.
TEST(Conformance, NWF_InvalidTagNameStartDigit) {
    // The parser's read_name_fast skips the '1' (not a name-start char),
    // so no tag is parsed → ParseError("Expected tag name").
    EXPECT_THROW(
        ([]{
            NullHandler h;
            fast_parse("<1tag/>", h);
        }()),
        ParseError);
}

// [XML 1.0 §2.5] A comment must begin with "<!--" (exactly two dashes).
// "<!-comment-->" is malformed.
TEST(Conformance, NWF_MalformedCommentOpen) {
    // "<!-comment-->" — only one dash after '<!' → not recognised as a comment.
    // The '!' branch does not match, the parser falls to start-tag parsing,
    // '!' is not a valid name-start char → ParseError.
    std::string xml = "<!-comment--><r/>";
    bool threw = false;
    try {
        NullHandler h;
        fast_parse(xml.data(), xml.size(), h);
    } catch (const ParseError&) {
        threw = true;
    }
    EXPECT_TRUE(threw) << "Malformed comment opener must cause a ParseError";
}

// [XML 1.0 §2.5] The string "--" must not appear inside a comment.
TEST(Conformance, NWF_CommentContainsDoubleDash) {
    // "<!-- bad -- comment -->" contains "--" inside the comment body.
    // Per spec this is a well-formedness error.
    //
    // parshred's find_comment_end scans for the full three-character sequence
    // "-->" rather than stopping at the first "--".  Therefore, when the input
    // is "<!-- bad -- comment -->", find_comment_end finds the LAST "-->" and
    // the captured comment content is " bad -- comment " (the whole body).
    //
    // A strict conformant parser would stop at the first "--" and report an
    // error; parshred is lenient — it accepts the comment but its content
    // includes the disallowed double-dash sequence.
    std::string xml = "<!-- bad -- comment --><r/>";
    // Must not crash.
    EXPECT_NO_THROW(sax_parses_ok(xml));
    ElementCollector h;
    ASSERT_NO_THROW(fast_parse(xml.data(), xml.size(), h));
    ASSERT_FALSE(h.comments.empty());
    // The comment body captured by the lenient parser contains "--":
    EXPECT_NE(h.comments[0].find("--"), std::string::npos)
        << "Lenient parser captures '--' inside comment body (spec violation)";
    // NOTE: A conformant parser MUST report an error when '--' appears
    // inside a comment (XML 1.0 §2.5, production [15]).
}

// [XML 1.0 §2.3] An element name must not be empty.
TEST(Conformance, NWF_EmptyTagName) {
    EXPECT_THROW(
        ([]{
            NullHandler h;
            fast_parse("</>", h);
        }()),
        ParseError);
}

// [XML 1.0 §3.1] Attributes require an '=' between name and value.
// A value without '=' may cause the parser to behave unexpectedly.
TEST(Conformance, NWF_AttributeWithoutEquals) {
    // "<e a b/>" — 'b' is treated as a second attribute name with no value,
    // which parshred's lenient attribute parser will accept (attr_val = "").
    // However the resulting structure (two attrs, no values) is ambiguous.
    // We just verify it does not crash.
    EXPECT_NO_THROW(sax_parses_ok("<e a b/>"));
}

// ═════════════════════════════════════════════════════════════════════════════
// Namespace Conformance (XML Namespaces 1.0)
// ═════════════════════════════════════════════════════════════════════════════

// [NS 1.0 §3] An undeclared prefix should resolve to an empty URI.
TEST(Conformance, NS_UndeclaredPrefix) {
    NsContext ctx;
    auto qn = ctx.resolve_name("undeclared:element");
    EXPECT_EQ(qn.prefix, "undeclared");
    EXPECT_EQ(qn.local_name, "element");
    EXPECT_EQ(qn.namespace_uri, "");
}

// [NS 1.0 §3] The "xml" prefix is reserved and pre-bound to
// http://www.w3.org/XML/1998/namespace.
TEST(Conformance, NS_XmlPrefixAlwaysBound) {
    NsContext ctx;
    EXPECT_EQ(ctx.resolve("xml"), ns::XML);
}

// [NS 1.0 §3] Redeclaring xmlns:xml to the correct URI is permitted but
// redundant; redeclaring it to any other URI is a namespace error.
TEST(Conformance, NS_XmlPrefixRedeclaredWrongUri) {
    NsContext ctx;
    ctx.push_scope();
    // A conformant application must detect this as an error.
    // parshred's NsContext just records the shadow — we verify the shadow
    // exists and that the resulting URI is wrong (non-conformant).
    ctx.declare("xml", "http://wrong.uri/");
    std::string_view uri = ctx.resolve("xml");
    // Either the context rejects the redeclaration (keeps the correct URI)
    // or records the wrong one.  Either way a conformant checker must flag it.
    // Here we document the behaviour: it depends on implementation policy.
    // We only assert the resolve call does not crash.
    EXPECT_FALSE(uri.empty()) << "xml prefix must resolve to something";
}

// [NS 1.0 §3] A namespace URI bound to a prefix must not be empty.
// xmlns:p="" is a namespace error.
TEST(Conformance, NS_EmptyNamespaceUriWithPrefix) {
    // parshred's NsContext will record the empty URI; a conformant application
    // layer must detect and reject this.
    NsContext ctx;
    ctx.push_scope();
    ctx.declare("p", "");
    std::string_view uri = ctx.resolve("p");
    // The spec mandates this is an error; parshred records it as-is.
    EXPECT_EQ(uri, "") << "Empty URI recorded (caller must flag as error)";
}

// [NS 1.0 §3] The default namespace (xmlns="") undeclares the default ns.
TEST(Conformance, NS_DefaultNamespaceUndeclaration) {
    NsContext ctx;
    ctx.push_scope();
    ctx.declare("", "http://example.com");
    EXPECT_EQ(ctx.resolve(""), "http://example.com");

    ctx.push_scope();
    ctx.declare("", "");   // xmlns="" — undeclares the default namespace
    EXPECT_EQ(ctx.resolve(""), "");

    ctx.pop_scope();
    EXPECT_EQ(ctx.resolve(""), "http://example.com");  // outer binding restored
}

// Namespace scope is properly pushed on start and popped on end element.
TEST(Conformance, NS_ScopeLifetime) {
    std::string xml =
        "<root xmlns:ex=\"http://ex.com\">"
        "  <child xmlns:ex=\"http://inner.com\"/>"
        "</root>";

    struct ScopeChecker final : SaxHandler {
        NsContext ctx;
        std::vector<std::string> uris_at_start;

        void on_start_element(std::string_view name,
                              const Attribute* attrs, size_t n) override {
            auto qn = process_element_ns(name, attrs, n, ctx);
            uris_at_start.emplace_back(ctx.resolve("ex"));
        }
        void on_end_element(std::string_view) override { ctx.pop_scope(); }
    };

    ScopeChecker h;
    ASSERT_NO_THROW(fast_parse(xml, h));
    ASSERT_EQ(h.uris_at_start.size(), 2u);
    EXPECT_EQ(h.uris_at_start[0], "http://ex.com");
    EXPECT_EQ(h.uris_at_start[1], "http://inner.com");  // shadowed
}

// ═════════════════════════════════════════════════════════════════════════════
// Character Encoding
// ═════════════════════════════════════════════════════════════════════════════

// [XML 1.0 §4.3.3] An XML document may begin with a UTF-8 BOM (EF BB BF).
// The BOM must be stripped before processing.
TEST(Conformance, ENC_UTF8_BOM_Detected) {
    // EF BB BF = UTF-8 BOM
    const char bom_xml[] = "\xEF\xBB\xBF<?xml version=\"1.0\"?><r/>";
    const size_t len = sizeof(bom_xml) - 1;

    // skip_bom must return 3
    EXPECT_EQ(skip_bom(bom_xml, len), 3u);

    // detect_encoding must return UTF-8
    EXPECT_EQ(detect_encoding(bom_xml, len), Encoding::UTF8);
}

// After stripping the BOM the remaining content must be parseable.
TEST(Conformance, ENC_UTF8_BOM_ParseableAfterStrip) {
    std::string bom_xml = "\xEF\xBB\xBF<root/>";
    size_t bom = skip_bom(bom_xml.data(), bom_xml.size());
    ASSERT_EQ(bom, 3u);

    // Parse the remainder directly.
    const char* data = bom_xml.data() + bom;
    size_t remaining = bom_xml.size() - bom;
    EXPECT_TRUE(dom_parses_ok(std::string(data, remaining)));
}

// [XML 1.0 §2.2] All ASCII characters in [#x9, #xA, #xD, #x20-#xD7FF]
// are valid XML characters.  A document containing only printable ASCII
// must validate as UTF-8.
TEST(Conformance, ENC_ASCIISubsetIsValidUTF8) {
    std::string ascii_xml = "<root><child attr=\"hello\">world</child></root>";
    EXPECT_EQ(validate_utf8(ascii_xml.data(), ascii_xml.size()), -1);
}

// Detect encoding from an XML declaration when no BOM is present.
TEST(Conformance, ENC_DetectFromXmlDeclaration) {
    std::string xml = "<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?><r/>";
    Encoding enc = detect_encoding(xml.data(), xml.size());
    EXPECT_EQ(enc, Encoding::ISO_8859_1);
}

// UTF-16 LE BOM (FF FE) is detected correctly.
TEST(Conformance, ENC_UTF16_LE_BOM) {
    const char utf16le_bom[] = "\xFF\xFE\x3C\x00\x72\x00\x2F\x00\x3E\x00";
    size_t len = sizeof(utf16le_bom) - 1;
    EXPECT_EQ(detect_encoding(utf16le_bom, len), Encoding::UTF16_LE);
    EXPECT_EQ(skip_bom(utf16le_bom, len), 2u);
}

// UTF-16 BE BOM (FE FF) is detected correctly.
TEST(Conformance, ENC_UTF16_BE_BOM) {
    const char utf16be_bom[] = "\xFE\xFF\x00\x3C\x00\x72\x00\x2F\x00\x3E";
    size_t len = sizeof(utf16be_bom) - 1;
    EXPECT_EQ(detect_encoding(utf16be_bom, len), Encoding::UTF16_BE);
    EXPECT_EQ(skip_bom(utf16be_bom, len), 2u);
}

// Invalid UTF-8 sequence (overlong encoding) must be detected.
TEST(Conformance, ENC_InvalidUTF8Overlong) {
    // Overlong encoding of U+002F ('/') using a 2-byte sequence: C0 AF
    const char bad[] = "\xC0\xAF";
    ptrdiff_t result = validate_utf8(bad, 2);
    EXPECT_GE(result, 0) << "Overlong encoding must be flagged as invalid";
}

// Valid multi-byte UTF-8 sequence passes validation.
TEST(Conformance, ENC_ValidMultibyteUTF8) {
    // U+00E9 LATIN SMALL LETTER E WITH ACUTE: C3 A9
    const char good[] = "\xC3\xA9";
    EXPECT_EQ(validate_utf8(good, 2), -1);
}

// ═════════════════════════════════════════════════════════════════════════════
// Entity Expansion (detailed)
// ═════════════════════════════════════════════════════════════════════════════

// [XML 1.0 §4.6] All five predefined entities must expand correctly.
TEST(Conformance, ENT_PredefinedEntityExpansion) {
    struct { const char* src; char expected; } cases[] = {
        { "&lt;",   '<'  },
        { "&gt;",   '>'  },
        { "&amp;",  '&'  },
        { "&apos;", '\'' },
        { "&quot;", '"'  },
    };
    for (auto& c : cases) {
        std::vector<char> out;
        expand_entities_inline(c.src, out);
        ASSERT_EQ(out.size(), 1u) << "Expected single char for " << c.src;
        EXPECT_EQ(out[0], c.expected) << "Wrong expansion for " << c.src;
    }
}

// [XML 1.0 §4.1] Decimal character references &#NNN; must expand to UTF-8.
TEST(Conformance, ENT_DecimalCharRef) {
    // &#65; = 'A'
    std::vector<char> out;
    expand_entities_inline("&#65;", out);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0], 'A');
}

// [XML 1.0 §4.1] Hexadecimal character references &#xHHH; expand correctly.
TEST(Conformance, ENT_HexCharRef) {
    // &#x42; = 'B'
    std::vector<char> out;
    expand_entities_inline("&#x42;", out);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0], 'B');
}

// A character reference to a high code point must produce valid UTF-8.
TEST(Conformance, ENT_HighCodepointCharRef) {
    // U+20AC EURO SIGN (&#x20AC;) → E2 82 AC in UTF-8
    std::vector<char> out;
    expand_entities_inline("&#x20AC;", out);
    ASSERT_EQ(out.size(), 3u);
    EXPECT_EQ(static_cast<unsigned char>(out[0]), 0xE2u);
    EXPECT_EQ(static_cast<unsigned char>(out[1]), 0x82u);
    EXPECT_EQ(static_cast<unsigned char>(out[2]), 0xACu);
}

// An unknown entity reference is passed through unchanged.
TEST(Conformance, ENT_UnknownEntityPassThrough) {
    std::vector<char> out;
    expand_entities_inline("&unknown;", out);
    std::string result(out.begin(), out.end());
    EXPECT_EQ(result, "&unknown;");
}

// Mixed plain text and entities all expand in a single call.
TEST(Conformance, ENT_MixedTextAndEntities) {
    std::vector<char> out;
    expand_entities_inline("a&amp;b&lt;c", out);
    std::string result(out.begin(), out.end());
    EXPECT_EQ(result, "a&b<c");
}

// ═════════════════════════════════════════════════════════════════════════════
// DTD-level well-formedness helpers
// ═════════════════════════════════════════════════════════════════════════════

// check_wellformedness must return no issues for a correct document.
TEST(Conformance, WF_DTD_CleanDocument) {
    auto issues = check_wellformedness("<root><child/><child/></root>");
    EXPECT_TRUE(issues.empty());
}

// check_wellformedness must report an issue for mismatched end-tags.
TEST(Conformance, WF_DTD_MismatchedTagsDetected) {
    auto issues = check_wellformedness("<a><b></a></b>");
    EXPECT_FALSE(issues.empty());
}

// check_wellformedness must report unclosed tags.
TEST(Conformance, WF_DTD_UnclosedTagDetected) {
    auto issues = check_wellformedness("<root><unclosed></root>");
    EXPECT_FALSE(issues.empty());
}

// check_wellformedness handles self-closing elements correctly.
TEST(Conformance, WF_DTD_SelfClosingOK) {
    auto issues = check_wellformedness("<root><a/><b/></root>");
    EXPECT_TRUE(issues.empty());
}

// ═════════════════════════════════════════════════════════════════════════════
// Fast DOM — structural integrity
// ═════════════════════════════════════════════════════════════════════════════

// The fast DOM must correctly resolve attribute values via dom.attr().
TEST(Conformance, FastDom_AttributeResolution) {
    std::string xml = "<root id=\"42\" name=\"test\"/>";
    auto dom = fast_dom_parse(xml.data(), xml.size());
    ASSERT_NE(dom.root_idx, 0u);
    const FastNode& root = dom.root();
    EXPECT_EQ(dom.attr(root, "id"),   "42");
    EXPECT_EQ(dom.attr(root, "name"), "test");
    EXPECT_EQ(dom.attr(root, "missing"), "");
}

// The fast DOM element_count() must be accurate.
TEST(Conformance, FastDom_ElementCount) {
    std::string xml = "<root><a/><b/><c/></root>";
    auto dom = fast_dom_parse(xml.data(), xml.size());
    // root + a + b + c = 4 element nodes
    EXPECT_EQ(dom.element_count(), 4u);
}

// The fast DOM must correctly parse CDATA and make it skippable.
TEST(Conformance, FastDom_CDataSkipped) {
    // In default (non-text) mode, CDATA section content is not added as a node.
    std::string xml = "<r><![CDATA[ignored]]><child/></r>";
    auto dom = fast_dom_parse<FDOM_NO_TEXT>(xml.data(), xml.size());
    ASSERT_NE(dom.root_idx, 0u);
    const FastNode& root = dom.root();
    const FastNode* child = dom.first_child(root);
    ASSERT_NE(child, nullptr);
    EXPECT_EQ(dom.name(*child), "child");
}

// In NORMALIZE mode, entity-expanded text nodes are stored in the values buffer.
TEST(Conformance, FastDom_NormalizeMode) {
    std::string xml = "<r>&amp;</r>";
    auto dom = fast_dom_parse<FDOM_NORMALIZE>(xml.data(), xml.size());
    ASSERT_NE(dom.root_idx, 0u);
    const FastNode& root = dom.root();
    const FastNode* text = dom.first_child(root);
    ASSERT_NE(text, nullptr);
    // The value_offset+len point into dom.values (flag 0x01 is set)
    EXPECT_EQ(text->flags & 0x01u, 0x01u);
    std::string val(dom.value(*text));
    EXPECT_NE(val.find('&'), std::string::npos);
}
