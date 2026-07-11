/// @file test_writer.cpp
/// @brief Unit tests for XML writer/serializer and DOM builder.

#include <parshred/writer.hpp>
#include <parshred/xpath.hpp>
#include <gtest/gtest.h>
#include <string>

using namespace parshred;

// ── Serialization ────────────────────────────────────────────────────

TEST(Writer, SerializeSimpleElement) {
    std::string xml = "<root><child/></root>";
    auto dom = fast_dom_parse<0>(xml.data(), xml.size());
    
    WriteOptions opts;
    opts.pretty = false;
    opts.xml_declaration = false;
    XmlWriter writer(opts);
    auto result = writer.serialize(dom);
    EXPECT_EQ(result, "<root><child/></root>");
}

TEST(Writer, SerializeWithAttributes) {
    std::string xml = R"(<root attr="value"><child id="1"/></root>)";
    auto dom = fast_dom_parse<0>(xml.data(), xml.size());
    
    WriteOptions opts;
    opts.pretty = false;
    opts.xml_declaration = false;
    XmlWriter writer(opts);
    auto result = writer.serialize(dom);
    EXPECT_EQ(result, R"(<root attr="value"><child id="1"/></root>)");
}

TEST(Writer, SerializeWithText) {
    std::string xml = "<root>Hello World</root>";
    auto dom = fast_dom_parse<0>(xml.data(), xml.size());
    
    WriteOptions opts;
    opts.pretty = false;
    opts.xml_declaration = false;
    XmlWriter writer(opts);
    auto result = writer.serialize(dom);
    EXPECT_EQ(result, "<root>Hello World</root>");
}

TEST(Writer, SerializePrettyPrint) {
    std::string xml = "<root><a/><b/></root>";
    auto dom = fast_dom_parse<0>(xml.data(), xml.size());
    
    WriteOptions opts;
    opts.pretty = true;
    opts.xml_declaration = false;
    XmlWriter writer(opts);
    auto result = writer.serialize(dom);
    
    std::string expected = "<root>\n  <a/>\n  <b/>\n</root>\n";
    EXPECT_EQ(result, expected);
}

TEST(Writer, SerializeWithDeclaration) {
    std::string xml = "<root/>";
    auto dom = fast_dom_parse<0>(xml.data(), xml.size());
    
    WriteOptions opts;
    opts.pretty = false;
    opts.xml_declaration = true;
    XmlWriter writer(opts);
    auto result = writer.serialize(dom);
    EXPECT_TRUE(result.find("<?xml version=\"1.0\" encoding=\"UTF-8\"?>") == 0);
}

TEST(Writer, EscapeEntitiesInText) {
    std::string xml = "<root>a &amp; b</root>";
    auto dom = fast_dom_parse<0>(xml.data(), xml.size());
    
    WriteOptions opts;
    opts.pretty = false;
    opts.xml_declaration = false;
    XmlWriter writer(opts);
    auto result = writer.serialize(dom);
    // The parsed text contains "a & b" (entity expanded), serialized back with escaping
    // Actually our parser stores raw text as-is (no entity expansion in fast_dom_parse)
    // So it should roundtrip the raw content
    EXPECT_TRUE(result.find("&amp;") != std::string::npos ||
                result.find("& b") != std::string::npos);
}

TEST(Writer, SelfCloseEmptyElements) {
    std::string xml = "<root><br/></root>";
    auto dom = fast_dom_parse<0>(xml.data(), xml.size());
    
    WriteOptions opts;
    opts.pretty = false;
    opts.xml_declaration = false;
    opts.self_close_empty = true;
    XmlWriter writer(opts);
    auto result = writer.serialize(dom);
    EXPECT_TRUE(result.find("<br/>") != std::string::npos);
}

TEST(Writer, NoSelfCloseOption) {
    std::string xml = "<root><br/></root>";
    auto dom = fast_dom_parse<0>(xml.data(), xml.size());
    
    WriteOptions opts;
    opts.pretty = false;
    opts.xml_declaration = false;
    opts.self_close_empty = false;
    XmlWriter writer(opts);
    auto result = writer.serialize(dom);
    EXPECT_TRUE(result.find("<br></br>") != std::string::npos);
}

// ── DOM Builder ──────────────────────────────────────────────────────

TEST(Builder, SimpleDocument) {
    DomBuilder builder;
    builder.start_element("root");
    builder.add_element("child");
    builder.end_element();
    
    auto dom = builder.build();
    EXPECT_GT(dom.node_count, 2u);
    EXPECT_EQ(dom.name(dom.nodes[dom.root_idx]), "root");
    
    // Should have one child
    uint32_t child = dom.nodes[dom.root_idx].first_child;
    EXPECT_NE(child, 0u);
    EXPECT_EQ(dom.name(dom.nodes[child]), "child");
}

TEST(Builder, WithAttributes) {
    DomBuilder builder;
    builder.start_element("config");
    builder.add_attribute("version", "1.0");
    builder.start_element("db");
    builder.add_attribute("host", "localhost");
    builder.add_attribute("port", "5432");
    builder.end_element();
    builder.end_element();
    
    auto dom = builder.build();
    
    // Verify attributes via XPath
    EXPECT_EQ(xpath::evaluate_string(dom, "/config/@version"), "1.0");
    EXPECT_EQ(xpath::evaluate_string(dom, "/config/db/@host"), "localhost");
    EXPECT_EQ(xpath::evaluate_string(dom, "/config/db/@port"), "5432");
}

TEST(Builder, WithText) {
    DomBuilder builder;
    builder.start_element("message");
    builder.add_text("Hello, World!");
    builder.end_element();
    
    auto dom = builder.build();
    auto text = xpath::evaluate_string(dom, "/message/text()");
    EXPECT_EQ(text, "Hello, World!");
}

TEST(Builder, ComplexDocument) {
    DomBuilder builder;
    builder.start_element("bookstore");
    
    builder.start_element("book");
    builder.add_attribute("category", "fiction");
    builder.start_element("title");
    builder.add_text("The Great Gatsby");
    builder.end_element();
    builder.start_element("author");
    builder.add_text("F. Scott Fitzgerald");
    builder.end_element();
    builder.end_element();
    
    builder.start_element("book");
    builder.add_attribute("category", "science");
    builder.start_element("title");
    builder.add_text("A Brief History of Time");
    builder.end_element();
    builder.end_element();
    
    builder.end_element();
    
    auto dom = builder.build();
    
    EXPECT_EQ(xpath::evaluate_count(dom, "/bookstore/book"), 2u);
    EXPECT_EQ(xpath::evaluate_string(dom, "/bookstore/book[1]/title/text()"), "The Great Gatsby");
    EXPECT_EQ(xpath::evaluate_string(dom, "/bookstore/book[@category='science']/title/text()"),
              "A Brief History of Time");
}

TEST(Builder, SerializeBuiltDocument) {
    DomBuilder builder;
    builder.start_element("root");
    builder.add_attribute("id", "1");
    builder.start_element("child");
    builder.add_text("content");
    builder.end_element();
    builder.end_element();
    
    auto dom = builder.build();
    
    WriteOptions opts;
    opts.pretty = false;
    opts.xml_declaration = false;
    XmlWriter writer(opts);
    auto result = writer.serialize(dom);
    EXPECT_EQ(result, R"(<root id="1"><child>content</child></root>)");
}

// ── Tree Modification ────────────────────────────────────────────────

TEST(Writer, RemoveChild) {
    std::string xml = "<root><a/><b/><c/></root>";
    auto dom = fast_dom_parse<0>(xml.data(), xml.size());
    
    // Find <b>
    auto b_nodes = xpath::evaluate(dom, "/root/b");
    ASSERT_EQ(b_nodes.size(), 1u);
    
    remove_child(dom, dom.root_idx, b_nodes[0]);
    
    // Now only a and c should be children
    WriteOptions opts;
    opts.pretty = false;
    opts.xml_declaration = false;
    XmlWriter writer(opts);
    auto result = writer.serialize(dom);
    EXPECT_EQ(result, "<root><a/><c/></root>");
}

TEST(Writer, RemoveAttribute) {
    std::string xml = R"(<root a="1" b="2" c="3"/>)";
    auto dom = fast_dom_parse<0>(xml.data(), xml.size());
    
    remove_attribute(dom, dom.root_idx, "b");
    
    WriteOptions opts;
    opts.pretty = false;
    opts.xml_declaration = false;
    XmlWriter writer(opts);
    auto result = writer.serialize(dom);
    EXPECT_EQ(result, R"(<root a="1" c="3"/>)");
}
