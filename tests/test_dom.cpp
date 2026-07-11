/// @file test_dom.cpp
/// @brief Unit tests for the DOM parser.

#include <parshred/dom_parser.hpp>
#include <gtest/gtest.h>
#include <string>
#include <vector>

using namespace parshred;

// Helper struct that owns the input string alongside the parse result.
// This ensures the string_views in the DOM tree remain valid.
struct ParsedDoc {
    std::string source;
    DomParseResult result;

    XmlNode* root() { return result.doc.root(); }
};

// Helper: parse XML string in default (safe) mode
static ParsedDoc parse_safe(std::string xml) {
    ParsedDoc pd;
    pd.source = std::move(xml);
    pd.result = dom_parse<DOM_DEFAULT>(pd.source.data(), pd.source.size());
    return pd;
}

// Helper: parse XML in fastest mode (needs mutable buffer)
static ParsedDoc parse_fastest(std::string xml) {
    ParsedDoc pd;
    pd.source = std::move(xml);
    pd.source.push_back('\0');
    pd.result = dom_parse<DOM_FASTEST>(pd.source.data(), pd.source.size() - 1);
    return pd;
}

// ── Basic Parsing ────────────────────────────────────────────────────

TEST(Dom, EmptyDocument) {
    auto pd = parse_safe("<root/>");
    ASSERT_NE(pd.root(), nullptr);
    EXPECT_EQ(pd.root()->name, "root");
    EXPECT_EQ(pd.root()->type, NodeType::Element);
}

TEST(Dom, SingleElement) {
    auto pd = parse_safe("<hello>world</hello>");
    auto* root = pd.root();
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->name, "hello");

    // Text child
    auto* text = root->first_child;
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(text->type, NodeType::Text);
    EXPECT_EQ(text->value, "world");
}

TEST(Dom, NestedElements) {
    auto pd = parse_safe("<a><b><c>deep</c></b></a>");
    auto* a = pd.root();
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->name, "a");

    auto* b = XmlDocument::first_element(a);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->name, "b");

    auto* c = XmlDocument::first_element(b);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->name, "c");

    auto* text = c->first_child;
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(text->value, "deep");
}

TEST(Dom, Siblings) {
    auto pd = parse_safe("<root><a/><b/><c/></root>");
    auto* root = pd.root();

    auto* a = XmlDocument::first_element(root);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->name, "a");

    auto* b = XmlDocument::next_element(a);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->name, "b");

    auto* c = XmlDocument::next_element(b);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->name, "c");

    EXPECT_EQ(XmlDocument::next_element(c), nullptr);
}

// ── Attributes ───────────────────────────────────────────────────────

TEST(Dom, SingleAttribute) {
    auto pd = parse_safe(R"(<item id="42"/>)");
    auto* root = pd.root();
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->name, "item");

    EXPECT_EQ(XmlDocument::attr(root, "id"), "42");
}

TEST(Dom, MultipleAttributes) {
    auto pd = parse_safe(R"(<tag a="1" b="2" c="3"/>)");
    auto* root = pd.root();

    EXPECT_EQ(XmlDocument::attr(root, "a"), "1");
    EXPECT_EQ(XmlDocument::attr(root, "b"), "2");
    EXPECT_EQ(XmlDocument::attr(root, "c"), "3");
    EXPECT_EQ(XmlDocument::attr(root, "d"), "");
}

TEST(Dom, AttributeWithSingleQuotes) {
    auto pd = parse_safe("<tag name='hello'/>");
    auto* root = pd.root();
    EXPECT_EQ(XmlDocument::attr(root, "name"), "hello");
}

// ── Entity Expansion ─────────────────────────────────────────────────

TEST(Dom, EntityExpansionText) {
    auto pd = parse_safe("<root>a &amp; b &lt; c</root>");
    auto* root = pd.root();
    auto* text = root->first_child;
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(text->value, "a & b < c");
}

TEST(Dom, EntityExpansionAttr) {
    auto pd = parse_safe(R"(<tag val="a&amp;b"/>)");
    auto* root = pd.root();
    EXPECT_EQ(XmlDocument::attr(root, "val"), "a&b");
}

TEST(Dom, NumericEntity) {
    auto pd = parse_safe("<root>&#65;&#x42;</root>");
    auto* root = pd.root();
    auto* text = root->first_child;
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(text->value, "AB");
}

// ── Fastest Mode (no text nodes, no entities) ────────────────────────

TEST(Dom, FastestMode) {
    auto pd = parse_fastest("<root><a id=\"1\"/><b id=\"2\"/></root>");
    auto* root = pd.root();
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->name, "root");

    auto* a = XmlDocument::first_element(root);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->name, "a");
    EXPECT_EQ(XmlDocument::attr(a, "id"), "1");

    auto* b = XmlDocument::next_element(a);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->name, "b");
    EXPECT_EQ(XmlDocument::attr(b, "id"), "2");
}

TEST(Dom, FastestNoTextNodes) {
    auto pd = parse_fastest("<root>ignored text<child/></root>");
    auto* root = pd.root();
    // Text should be skipped in fastest mode
    auto* child = root->first_child;
    ASSERT_NE(child, nullptr);
    EXPECT_EQ(child->type, NodeType::Element);
    EXPECT_EQ(child->name, "child");
}

// ── Comments and PIs ─────────────────────────────────────────────────

TEST(Dom, Comments) {
    auto pd = parse_safe("<root><!-- hello --><child/></root>");
    auto* root = pd.root();
    auto* comment = root->first_child;
    ASSERT_NE(comment, nullptr);
    EXPECT_EQ(comment->type, NodeType::Comment);
    EXPECT_EQ(comment->value, " hello ");
}

TEST(Dom, CDATA) {
    auto pd = parse_safe("<root><![CDATA[raw <data> here]]></root>");
    auto* root = pd.root();
    auto* cdata = root->first_child;
    ASSERT_NE(cdata, nullptr);
    EXPECT_EQ(cdata->type, NodeType::CData);
    EXPECT_EQ(cdata->value, "raw <data> here");
}

// ── Navigation API ───────────────────────────────────────────────────

TEST(Dom, FindChild) {
    auto pd = parse_safe("<root><a/><b/><target/><c/></root>");
    auto* root = pd.root();
    auto* target = XmlDocument::find_child(root, "target");
    ASSERT_NE(target, nullptr);
    EXPECT_EQ(target->name, "target");
}

TEST(Dom, ChildRange) {
    auto pd = parse_safe("<root><a/><b/><c/></root>");
    auto* root = pd.root();
    int count = 0;
    for (auto* child : XmlDocument::children(root)) {
        EXPECT_EQ(child->type, NodeType::Element);
        ++count;
    }
    EXPECT_EQ(count, 3);
}

TEST(Dom, AttrRange) {
    auto pd = parse_safe(R"(<tag x="1" y="2" z="3"/>)");
    auto* root = pd.root();
    int count = 0;
    for (auto* attr : XmlDocument::attributes(root)) {
        EXPECT_EQ(attr->type, NodeType::Attribute);
        ++count;
    }
    EXPECT_EQ(count, 3);
}

// ── Pool Allocator ───────────────────────────────────────────────────

TEST(Dom, NodeCountAccurate) {
    auto pd = parse_safe("<root><a x=\"1\"/><b/><c>text</c></root>");
    // Nodes: root, a, attr(x), b, c, text(text) = 6
    EXPECT_EQ(pd.result.doc.node_count(), 6u);
}

// ── Stress Test ──────────────────────────────────────────────────────

TEST(Dom, ManyElements) {
    std::string xml = "<root>";
    for (int i = 0; i < 10000; ++i) {
        xml += "<item id=\"" + std::to_string(i) + "\"/>";
    }
    xml += "</root>";

    auto pd = parse_safe(std::move(xml));
    auto* root = pd.root();
    ASSERT_NE(root, nullptr);

    int count = 0;
    for (auto* child : XmlDocument::elements(root)) {
        (void)child;
        ++count;
    }
    EXPECT_EQ(count, 10000);
}

TEST(Dom, ManyElementsFastest) {
    std::string xml = "<root>";
    for (int i = 0; i < 10000; ++i) {
        xml += "<item id=\"" + std::to_string(i) + "\"/>";
    }
    xml += "</root>";

    auto pd = parse_fastest(std::move(xml));
    auto* root = pd.root();
    ASSERT_NE(root, nullptr);

    int count = 0;
    for (auto* child : XmlDocument::elements(root)) {
        (void)child;
        ++count;
    }
    EXPECT_EQ(count, 10000);
}
