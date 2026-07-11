/// @file test_security.cpp
/// @brief Security tests — XML bombs, excessive nesting, entity limits.

#include <parshred/sax_parser.hpp>
#include <gtest/gtest.h>

#include <string>

using namespace parshred;

TEST(Security, MaxDepthExceeded) {
    // Build a deeply nested XML string
    std::string xml;
    int depth = 600; // exceeds DEFAULT_MAX_DEPTH (512)
    for (int i = 0; i < depth; ++i) {
        xml += "<d" + std::to_string(i) + ">";
    }
    xml += "deep";
    for (int i = depth - 1; i >= 0; --i) {
        xml += "</d" + std::to_string(i) + ">";
    }

    SaxParser parser;
    EXPECT_THROW(parser.parse_string(xml), SecurityError);
}

TEST(Security, MaxDepthCustom) {
    // Set a low max depth
    std::string xml = "<a><b><c><d>text</d></c></b></a>";

    SaxParser parser;
    parser.set_max_depth(3);
    EXPECT_THROW(parser.parse_string(xml), SecurityError);
}

TEST(Security, MaxDepthOk) {
    std::string xml = "<a><b><c>text</c></b></a>";

    SaxParser parser;
    parser.set_max_depth(10);
    EXPECT_NO_THROW(parser.parse_string(xml));
}

TEST(Security, EntityExpansionLimit) {
    // Create XML with many entity references
    std::string xml = "<root>";
    for (int i = 0; i < 20000; ++i) {
        xml += "&amp;";
    }
    xml += "</root>";

    SaxParser parser;
    parser.set_max_entity_expansions(10000);

    // Set up a text callback so entities actually get expanded
    parser.on_text([](std::string_view) {});

    EXPECT_THROW(parser.parse_string(xml), SecurityError);
}

TEST(Security, MalformedUnclosedTag) {
    std::string xml = "<root><unclosed>";

    SaxParser parser;
    // This should parse without crashing (the parser doesn't strictly
    // validate tag matching in Phase 1 SAX mode — it just fires events)
    EXPECT_NO_THROW(parser.parse_string(xml));
}

TEST(Security, EmptyTagName) {
    std::string xml = "<>text</>";

    SaxParser parser;
    EXPECT_THROW(parser.parse_string(xml), ParseError);
}

TEST(Security, VeryLongAttributeValue) {
    // Test handling of very long attribute values (potential DoS)
    std::string long_val(100000, 'x');
    std::string xml = "<root attr=\"" + long_val + "\"/>";

    SaxParser parser;
    std::string captured;
    parser.on_start_element([&](std::string_view, std::span<const Attribute> attrs) {
        if (!attrs.empty()) {
            captured = std::string(attrs[0].value);
        }
    });

    EXPECT_NO_THROW(parser.parse_string(xml));
    EXPECT_EQ(captured.size(), long_val.size());
}

TEST(Security, TooManyAttributes) {
    // Create an element with too many attributes
    std::string xml = "<root";
    for (int i = 0; i < 1500; ++i) {
        xml += " attr" + std::to_string(i) + "=\"val\"";
    }
    xml += "/>";

    SaxParser parser;
    parser.set_max_attribute_count(1000);
    EXPECT_THROW(parser.parse_string(xml), SecurityError);
}

TEST(Security, TooManyAttributesStartTag) {
    // Same test but with a non-self-closing start tag
    std::string xml = "<root";
    for (int i = 0; i < 1500; ++i) {
        xml += " attr" + std::to_string(i) + "=\"val\"";
    }
    xml += ">text</root>";

    SaxParser parser;
    parser.set_max_attribute_count(1000);
    EXPECT_THROW(parser.parse_string(xml), SecurityError);
}

TEST(Security, BillionLaughsPrevention) {
    // Classic "billion laughs" attack uses nested entity definitions.
    // Since we don't support DTD entity definitions (only predefined entities),
    // this should be inherently safe. Test that we handle unknown entities gracefully.
    std::string xml = "<root>&custom_entity;</root>";

    SaxParser parser;
    std::vector<std::string> texts;
    parser.on_text([&](std::string_view text) {
        texts.emplace_back(text);
    });

    EXPECT_NO_THROW(parser.parse_string(xml));
    // Unknown entity passed through as-is
}
