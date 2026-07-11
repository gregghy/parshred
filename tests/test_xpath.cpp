/// @file test_xpath.cpp
/// @brief Unit tests for XPath 1.0 engine.

#include <parshred/xpath.hpp>
#include <gtest/gtest.h>
#include <string>
#include <vector>

using namespace parshred;
using namespace parshred::xpath;

// Helper: parse XML into FastDom for testing (with text + attrs).
static FastDom parse_test_xml(const char* xml, size_t len) {
    return fast_dom_parse<0>(xml, len);  // No flags = full parse
}

static FastDom parse_test_xml(const std::string& xml) {
    return parse_test_xml(xml.data(), xml.size());
}

// ── Basic Path Expressions ───────────────────────────────────────────

TEST(XPath, AbsolutePathSingleElement) {
    std::string xml = "<root><child/></root>";
    auto dom = parse_test_xml(xml);
    auto results = evaluate(dom, "/root/child");
    EXPECT_EQ(results.size(), 1u);
    EXPECT_EQ(dom.name(dom.nodes[results[0]]), "child");
}

TEST(XPath, AbsolutePathMultipleChildren) {
    std::string xml = "<root><a/><b/><c/></root>";
    auto dom = parse_test_xml(xml);
    auto results = evaluate(dom, "/root/*");
    EXPECT_EQ(results.size(), 3u);
}

TEST(XPath, NestedPath) {
    std::string xml = "<root><parent><child><grandchild/></child></parent></root>";
    auto dom = parse_test_xml(xml);
    auto results = evaluate(dom, "/root/parent/child/grandchild");
    EXPECT_EQ(results.size(), 1u);
    EXPECT_EQ(dom.name(dom.nodes[results[0]]), "grandchild");
}

TEST(XPath, NoMatch) {
    std::string xml = "<root><child/></root>";
    auto dom = parse_test_xml(xml);
    auto results = evaluate(dom, "/root/nonexistent");
    EXPECT_EQ(results.size(), 0u);
}

// ── Wildcards ────────────────────────────────────────────────────────

TEST(XPath, WildcardChildren) {
    std::string xml = "<root><a/><b/><c/></root>";
    auto dom = parse_test_xml(xml);
    auto results = evaluate(dom, "/root/*");
    EXPECT_EQ(results.size(), 3u);
}

// ── Descendant (//) ──────────────────────────────────────────────────

TEST(XPath, DescendantAll) {
    std::string xml = "<root><a><b/></a><c><d><e/></d></c></root>";
    auto dom = parse_test_xml(xml);
    auto results = evaluate(dom, "//e");
    EXPECT_EQ(results.size(), 1u);
    EXPECT_EQ(dom.name(dom.nodes[results[0]]), "e");
}

TEST(XPath, DescendantMultiple) {
    std::string xml = "<root><item/><sub><item/></sub><item/></root>";
    auto dom = parse_test_xml(xml);
    auto results = evaluate(dom, "//item");
    EXPECT_EQ(results.size(), 3u);
}

TEST(XPath, DescendantFromPath) {
    std::string xml = "<root><a><x/></a><b><x/></b></root>";
    auto dom = parse_test_xml(xml);
    auto results = evaluate(dom, "/root/a//x");
    EXPECT_EQ(results.size(), 1u);
}

// ── Attributes ───────────────────────────────────────────────────────

TEST(XPath, AttributeAccess) {
    std::string xml = R"(<root><item id="1"/><item id="2"/></root>)";
    auto dom = parse_test_xml(xml);
    auto results = evaluate(dom, "/root/item/@id");
    EXPECT_EQ(results.size(), 2u);
    // Attribute nodes — their values should be "1" and "2"
    auto strings = evaluate_strings(dom, "/root/item/@id");
    EXPECT_EQ(strings.size(), 2u);
    EXPECT_EQ(strings[0], "1");
    EXPECT_EQ(strings[1], "2");
}

// ── Predicates ───────────────────────────────────────────────────────

TEST(XPath, PositionPredicate) {
    std::string xml = "<root><item/><item/><item/></root>";
    auto dom = parse_test_xml(xml);
    auto results = evaluate(dom, "/root/item[1]");
    EXPECT_EQ(results.size(), 1u);
    // Should be the first item
}

TEST(XPath, LastPositionPredicate) {
    std::string xml = "<root><item/><item/><item/></root>";
    auto dom = parse_test_xml(xml);
    auto results = evaluate(dom, "/root/item[last()]");
    EXPECT_EQ(results.size(), 1u);
}

TEST(XPath, AttrExistsPredicate) {
    std::string xml = R"(<root><item/><item id="1"/><item/></root>)";
    auto dom = parse_test_xml(xml);
    auto results = evaluate(dom, "/root/item[@id]");
    EXPECT_EQ(results.size(), 1u);
}

TEST(XPath, AttrEqualsPredicate) {
    std::string xml = R"(<root><item id="a"/><item id="b"/><item id="c"/></root>)";
    auto dom = parse_test_xml(xml);
    auto results = evaluate(dom, "/root/item[@id='b']");
    EXPECT_EQ(results.size(), 1u);
    auto val = dom.attr(dom.nodes[results[0]], "id");
    EXPECT_EQ(val, "b");
}

// ── Text Nodes ───────────────────────────────────────────────────────

TEST(XPath, TextNode) {
    std::string xml = "<root><item>hello</item><item>world</item></root>";
    auto dom = parse_test_xml(xml);
    auto results = evaluate(dom, "/root/item/text()");
    EXPECT_EQ(results.size(), 2u);
    auto strings = evaluate_strings(dom, "/root/item/text()");
    EXPECT_EQ(strings[0], "hello");
    EXPECT_EQ(strings[1], "world");
}

TEST(XPath, TextContentHelper) {
    std::string xml = "<root><p>Hello <b>world</b>!</p></root>";
    auto dom = parse_test_xml(xml);
    auto results = evaluate(dom, "/root/p");
    ASSERT_EQ(results.size(), 1u);
    auto text = get_text_content(dom, results[0]);
    EXPECT_EQ(text, "Hello world!");
}

// ── evaluate_string convenience ──────────────────────────────────────

TEST(XPath, EvaluateString) {
    std::string xml = R"(<config><db host="localhost" port="5432"/></config>)";
    auto dom = parse_test_xml(xml);
    EXPECT_EQ(evaluate_string(dom, "/config/db/@host"), "localhost");
    EXPECT_EQ(evaluate_string(dom, "/config/db/@port"), "5432");
    EXPECT_EQ(evaluate_string(dom, "/config/db/@missing"), "");
}

// ── evaluate_count convenience ───────────────────────────────────────

TEST(XPath, EvaluateCount) {
    std::string xml = "<root><a/><a/><b/><a/></root>";
    auto dom = parse_test_xml(xml);
    EXPECT_EQ(evaluate_count(dom, "/root/a"), 3u);
    EXPECT_EQ(evaluate_count(dom, "/root/b"), 1u);
    EXPECT_EQ(evaluate_count(dom, "/root/c"), 0u);
}

// ── Self (.) and Parent (..) ─────────────────────────────────────────

TEST(XPath, SelfAxis) {
    std::string xml = "<root><child/></root>";
    auto dom = parse_test_xml(xml);
    auto results = evaluate(dom, "/root/.");
    EXPECT_EQ(results.size(), 1u);
    EXPECT_EQ(dom.name(dom.nodes[results[0]]), "root");
}

// ── Complex expressions ──────────────────────────────────────────────

TEST(XPath, BookstoreExample) {
    std::string xml = R"(
        <bookstore>
            <book category="cooking">
                <title>Italian Cooking</title>
                <price>30.00</price>
            </book>
            <book category="fiction">
                <title>Harry Potter</title>
                <price>29.99</price>
            </book>
            <book category="fiction">
                <title>Lord of the Rings</title>
                <price>39.95</price>
            </book>
        </bookstore>
    )";
    auto dom = parse_test_xml(xml);
    
    // All books
    EXPECT_EQ(evaluate_count(dom, "/bookstore/book"), 3u);
    
    // Fiction books
    auto fiction = evaluate(dom, "/bookstore/book[@category='fiction']");
    EXPECT_EQ(fiction.size(), 2u);
    
    // All titles
    auto titles = evaluate_strings(dom, "/bookstore/book/title/text()");
    EXPECT_EQ(titles.size(), 3u);
    EXPECT_EQ(titles[0], "Italian Cooking");
    EXPECT_EQ(titles[1], "Harry Potter");
    EXPECT_EQ(titles[2], "Lord of the Rings");
    
    // First book's title
    auto first_title = evaluate_string(dom, "/bookstore/book[1]/title/text()");
    EXPECT_EQ(first_title, "Italian Cooking");
}

TEST(XPath, DescendantWithPredicate) {
    std::string xml = R"(
        <root>
            <div class="main">
                <p>Hello</p>
            </div>
            <div class="sidebar">
                <p>World</p>
            </div>
        </root>
    )";
    auto dom = parse_test_xml(xml);
    
    auto main_div = evaluate(dom, "//div[@class='main']");
    EXPECT_EQ(main_div.size(), 1u);
    
    auto all_p = evaluate(dom, "//p");
    EXPECT_EQ(all_p.size(), 2u);
}
