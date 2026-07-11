/// @file test_sax.cpp
/// @brief Unit tests for the SAX parser.

#include <parshred/sax_parser.hpp>
#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace parshred;

TEST(SaxParser, SimpleElement) {
    std::string xml = "<root>hello</root>";

    std::vector<std::string> starts, ends, texts;

    SaxParser parser;
    parser.on_start_element([&](std::string_view name, std::span<const Attribute>) {
        starts.emplace_back(name);
    });
    parser.on_end_element([&](std::string_view name) {
        ends.emplace_back(name);
    });
    parser.on_text([&](std::string_view text) {
        texts.emplace_back(text);
    });

    parser.parse_string(xml);

    ASSERT_EQ(starts.size(), 1u);
    EXPECT_EQ(starts[0], "root");
    ASSERT_EQ(ends.size(), 1u);
    EXPECT_EQ(ends[0], "root");
    ASSERT_GE(texts.size(), 1u);
    // Text might include "hello"
    bool found = false;
    for (const auto& t : texts) {
        if (t.find("hello") != std::string::npos) found = true;
    }
    EXPECT_TRUE(found);
}

TEST(SaxParser, Attributes) {
    std::string xml = R"(<div class="main" id="content">text</div>)";

    std::vector<std::pair<std::string, std::string>> captured_attrs;

    SaxParser parser;
    parser.on_start_element([&](std::string_view, std::span<const Attribute> attrs) {
        for (const auto& a : attrs) {
            captured_attrs.emplace_back(std::string(a.name), std::string(a.value));
        }
    });

    parser.parse_string(xml);

    ASSERT_EQ(captured_attrs.size(), 2u);
    EXPECT_EQ(captured_attrs[0].first, "class");
    EXPECT_EQ(captured_attrs[0].second, "main");
    EXPECT_EQ(captured_attrs[1].first, "id");
    EXPECT_EQ(captured_attrs[1].second, "content");
}

TEST(SaxParser, SelfClosingElement) {
    std::string xml = "<root><br/></root>";

    std::vector<std::string> starts, ends;

    SaxParser parser;
    parser.on_start_element([&](std::string_view name, std::span<const Attribute>) {
        starts.emplace_back(name);
    });
    parser.on_end_element([&](std::string_view name) {
        ends.emplace_back(name);
    });

    parser.parse_string(xml);

    // br should appear in both starts and ends (self-closing)
    EXPECT_EQ(starts.size(), 2u); // root + br
    EXPECT_EQ(ends.size(), 2u);   // br + root
}

TEST(SaxParser, NestedElements) {
    std::string xml = "<a><b><c>text</c></b></a>";

    std::vector<std::string> starts, ends;

    SaxParser parser;
    parser.on_start_element([&](std::string_view name, std::span<const Attribute>) {
        starts.emplace_back(name);
    });
    parser.on_end_element([&](std::string_view name) {
        ends.emplace_back(name);
    });

    parser.parse_string(xml);

    ASSERT_EQ(starts.size(), 3u);
    EXPECT_EQ(starts[0], "a");
    EXPECT_EQ(starts[1], "b");
    EXPECT_EQ(starts[2], "c");

    ASSERT_EQ(ends.size(), 3u);
    EXPECT_EQ(ends[0], "c");
    EXPECT_EQ(ends[1], "b");
    EXPECT_EQ(ends[2], "a");
}

TEST(SaxParser, Comment) {
    std::string xml = "<!-- hello --><root/>";

    std::vector<std::string> comments;
    SaxParser parser;
    parser.on_comment([&](std::string_view text) {
        comments.emplace_back(text);
    });

    parser.parse_string(xml);

    ASSERT_EQ(comments.size(), 1u);
    EXPECT_EQ(comments[0], " hello ");
}

TEST(SaxParser, EntityExpansion) {
    std::string xml = "<p>&lt;hello&gt;</p>";

    std::vector<std::string> texts;
    SaxParser parser;
    parser.on_text([&](std::string_view text) {
        texts.emplace_back(text);
    });

    parser.parse_string(xml);

    // After entity expansion, we should get "<" and ">"
    bool found_lt = false, found_gt = false;
    for (const auto& t : texts) {
        if (t.find('<') != std::string::npos) found_lt = true;
        if (t.find('>') != std::string::npos) found_gt = true;
    }
    EXPECT_TRUE(found_lt);
    EXPECT_TRUE(found_gt);
}

TEST(SaxParser, Stats) {
    std::string xml = R"(<root a="1" b="2"><child>text</child><!-- comment --></root>)";

    SaxParser parser;
    parser.parse_string(xml);

    const auto& stats = parser.stats();
    EXPECT_EQ(stats.elements, 2u);     // root + child
    EXPECT_EQ(stats.attributes, 2u);   // a, b
    EXPECT_GE(stats.text_nodes, 1u);   // "text"
    EXPECT_EQ(stats.comments, 1u);     // <!-- comment -->
    EXPECT_EQ(stats.bytes_parsed, xml.size());
}

TEST(SaxParser, XmlDeclaration) {
    std::string xml = R"(<?xml version="1.0" encoding="UTF-8"?><root/>)";

    std::string version, encoding;
    SaxParser parser;
    parser.on_xml_declaration([&](std::string_view v, std::string_view e, std::string_view) {
        version = v;
        encoding = e;
    });

    parser.parse_string(xml);

    EXPECT_EQ(version, "1.0");
    EXPECT_EQ(encoding, "UTF-8");
}

TEST(SaxParser, CData) {
    std::string xml = "<root><![CDATA[raw <content> & stuff]]></root>";

    std::vector<std::string> cdata;
    SaxParser parser;
    parser.on_cdata([&](std::string_view text) {
        cdata.emplace_back(text);
    });

    parser.parse_string(xml);

    ASSERT_EQ(cdata.size(), 1u);
    EXPECT_EQ(cdata[0], "raw <content> & stuff");
}

TEST(SaxParser, EmptyElement) {
    std::string xml = "<root></root>";

    SaxParser parser;
    std::vector<std::string> texts;
    parser.on_text([&](std::string_view text) {
        texts.emplace_back(text);
    });

    parser.parse_string(xml);

    // No text should be emitted (or only whitespace)
    for (const auto& t : texts) {
        // All text should be whitespace-only
        bool all_ws = true;
        for (char c : t) {
            if (c != ' ' && c != '\n' && c != '\r' && c != '\t') all_ws = false;
        }
        EXPECT_TRUE(all_ws) << "Unexpected non-whitespace text: " << t;
    }
}

TEST(SaxParser, MultipleRootLevelPIs) {
    std::string xml = R"(<?xml version="1.0"?><?custom data?><root/>)";

    int pi_count = 0;
    SaxParser parser;
    parser.on_processing_instruction([&](std::string_view, std::string_view) {
        ++pi_count;
    });

    parser.parse_string(xml);

    EXPECT_EQ(pi_count, 1); // only the <?custom?> PI, not the xml declaration
}
